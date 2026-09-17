#include <M5Unified.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include "range_stream.h"

namespace {
constexpr uart_port_t port=UART_NUM_1;
constexpr uint32_t bg=0x08121f,white=0xe7f1ff,cyan=0x45d6d0,amber=0xffc66d;
M5Canvas canvas(&M5.Display);
ranging::Stream stats;
QueueHandle_t events=nullptr;
esp_err_t initError=ESP_OK;
bool ready=false,seen=false;
uint32_t lastGood=0,lastDraw=0,lastLog=0,lastOk=0,rate=0,maxGap=0;
uint32_t framing=0,overflow=0,parity=0,breaks=0;
const char *state() {
    if(!ready) return "INIT ERROR";
    if(!seen) return "WAITING";
    return millis()-lastGood>1000u?"NO DATA":"RECEIVING";
}
void resetStats() {
    if(ready) {uart_flush_input(port);xQueueReset(events);}
    stats={};seen=false;lastGood=0;lastOk=0;rate=0;maxGap=0;
    framing=overflow=parity=breaks=0;lastLog=millis();
}
void draw() {
    const auto &r=stats.sample;
    const uint32_t since=seen?millis()-lastGood:UINT32_MAX;
    canvas.fillScreen(bg);canvas.setTextSize(1);canvas.setTextColor(cyan);
    canvas.drawString("2DK / UWB " FW_VERSION,12,8);
    canvas.setTextColor(white);canvas.drawString("38400 bps / " + String(state()),12,26);
    for(unsigned i=0;i<2;i++) {
        const int y=48+46*i;
        const bool valid=seen && ranging::fresh(r,i,since);
        canvas.setTextColor(valid?cyan:amber);canvas.setTextSize(2);canvas.setCursor(12,y);
        canvas.printf("19-%u  ",i?22u:21u);
        if(valid) canvas.printf("%u.%02u m",r.cm[i]/100,r.cm[i]%100);
        else canvas.print("--.-- m");
        canvas.setTextSize(1);canvas.setTextColor(white);canvas.setCursor(12,y+23);
        if(seen) canvas.printf("updates %lu  age@TX %u ms",(unsigned long)r.updates[i],r.age[i]);
        else canvas.print("Waiting for measured distance");
    }
    canvas.setCursor(12,143);canvas.setTextColor(white);
    if(seen && ranging::fresh(r,2,since)) canvas.printf("A19 %d / %d / %d mg",r.accel[0],r.accel[1],r.accel[2]);
    else canvas.print("A19 -- / -- / -- mg");
    canvas.setCursor(12,162);canvas.printf("RX %lu/s  BAD %lu  MISS %lu",(unsigned long)rate,(unsigned long)stats.bad,(unsigned long)stats.missing);
    canvas.setCursor(12,179);canvas.printf("R_OK %lu  R_BAD %lu",(unsigned long)r.range_ok,(unsigned long)r.range_bad);
    canvas.setCursor(12,201);canvas.printf("F_ERR %lu / OVF %lu",(unsigned long)framing,(unsigned long)overflow);
    canvas.setCursor(12,220);canvas.setTextColor(amber);canvas.print("21-22: not transferred");
    canvas.fillRoundRect(216,202,96,30,6,cyan);canvas.setTextColor(bg);
    canvas.setTextDatum(middle_center);canvas.drawString("CLEAR",264,217);canvas.setTextDatum(top_left);
    canvas.pushSprite(0,0);
}
void receive() {
    uart_event_t event;
    // DATA events are hints; drain the ring buffer below regardless of event coalescing.
    for(unsigned n=0;n<32 && xQueueReceive(events,&event,0)==pdTRUE;n++) {
        if(event.type==UART_FIFO_OVF || event.type==UART_BUFFER_FULL) {
            ++overflow;uart_flush_input(port);stats.loseSync();xQueueReset(events);
        } else if(event.type==UART_FRAME_ERR) ++framing;
        else if(event.type==UART_PARITY_ERR) ++parity;
        else if(event.type==UART_BREAK) ++breaks;
    }
    uint8_t b[256];
    // Bound each loop so touch and screen updates cannot be starved by noise.
    for(unsigned block=0;block<16;block++) {
        const int count=uart_read_bytes(port,b,sizeof b,0);
        if(count<=0) break;
        for(int i=0;i<count;i++) if(stats.feed(b[i])) {
            const uint32_t now=millis();
            if(seen && now-lastGood>maxGap) maxGap=now-lastGood;
            seen=true;lastGood=now;
        }
    }
}
void log() {
    const uint32_t now=millis(),elapsed=now-lastLog;
    rate=elapsed?(stats.ok-lastOk)*1000u/elapsed:0;lastOk=stats.ok;lastLog=now;
    if(!Serial) return;
    const auto &r=stats.sample;
    const uint32_t age=seen?now-lastGood:UINT32_MAX;
    unsigned valid=0;for(unsigned i=0;i<3;i++)if(seen && ranging::fresh(r,i,age))valid|=1u<<i;
    Serial.printf("RANGE_STAT,fw=%s,baud=38400,state=%s,ok=%lu,bad=%lu,missing=%lu,duplicate=%lu,restarts=%lu,backwards=%lu,discarded=%lu,rate=%lu,age_ms=%lu,max_gap_ms=%lu,frame_error=%lu,overflow=%lu,parity=%lu,breaks=%lu,init=%s\n",
      FW_VERSION,state(),(unsigned long)stats.ok,(unsigned long)stats.bad,(unsigned long)stats.missing,
      (unsigned long)stats.duplicates,(unsigned long)stats.restarts,(unsigned long)stats.backwards,
      (unsigned long)stats.discarded,(unsigned long)rate,(unsigned long)age,(unsigned long)maxGap,
      (unsigned long)framing,(unsigned long)overflow,(unsigned long)parity,(unsigned long)breaks,esp_err_to_name(initError));
    Serial.printf("RANGE_DATA,seq=%lu,boot=%08lx,tx_ms=%lu,ready=%u,valid=%u,d19_21_cm=%u,d19_22_cm=%u,age_at_tx_ms=%u/%u,updates=%lu/%lu,a19_mg=%d/%d/%d,a19_age_at_tx_ms=%u,r_ok=%lu,r_bad=%lu,session=%u/%u,reason=%02x/%02x,irq_us=%u,tx_frame_ms=%u,tx_overruns=%u\n",
      (unsigned long)r.sequence,(unsigned long)r.boot,(unsigned long)r.uptime,r.ready,valid,r.cm[0],r.cm[1],r.age[0],r.age[1],
      (unsigned long)r.updates[0],(unsigned long)r.updates[1],r.accel[0],r.accel[1],r.accel[2],r.accel_age,
      (unsigned long)r.range_ok,(unsigned long)r.range_bad,r.state[0],r.state[1],r.reason[0],r.reason[1],r.irq_us,r.frame_ms,r.overruns);
}
esp_err_t beginRx() {
    // Preserve the internal I2C used by the touch screen.
    if(M5.Ex_I2C.getSDA()!=2 || M5.Ex_I2C.getSCL()!=1 || M5.Ex_I2C.getPort()==M5.In_I2C.getPort())
        return ESP_ERR_INVALID_STATE;
    M5.Ex_I2C.release();
    gpio_config_t pins={};pins.pin_bit_mask=(1ULL<<1)|(1ULL<<2);pins.mode=GPIO_MODE_INPUT;
    pins.pull_up_en=GPIO_PULLUP_DISABLE;pins.pull_down_en=GPIO_PULLDOWN_DISABLE;
    pins.intr_type=GPIO_INTR_DISABLE;
    esp_err_t e=gpio_config(&pins);if(e!=ESP_OK) return e;
    uart_config_t config={};config.baud_rate=UART_TEST_BAUD;config.data_bits=UART_DATA_8_BITS;
    config.parity=UART_PARITY_DISABLE;config.stop_bits=UART_STOP_BITS_1;
    config.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;config.source_clk=UART_SCLK_APB;
    e=uart_param_config(port,&config);if(e!=ESP_OK) return e;
    // No TX signal is assigned or written; GPIO1 remains an input.
    e=uart_set_pin(port,UART_PIN_NO_CHANGE,2,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if(e!=ESP_OK) return e;
    // IDF uart_set_pin enables RX pull-up. Disable it AFTER routing so this
    // 3.3 V receiver does not pull up the transmitter's measured 3.0 V rail.
    e=gpio_set_pull_mode(GPIO_NUM_2,GPIO_FLOATING);if(e!=ESP_OK) return e;
    e=uart_driver_install(port,4096,0,32,&events,0);if(e!=ESP_OK) return e;
    e=uart_set_rx_timeout(port,2);
    if(e!=ESP_OK) {uart_driver_delete(port);events=nullptr;return e;}
    return ESP_OK;
}
}
void setup() {
    auto cfg=M5.config();cfg.serial_baudrate=115200;
    cfg.internal_imu=false;cfg.internal_rtc=false;cfg.internal_mic=false;cfg.internal_spk=false;
    cfg.external_imu=false;cfg.external_rtc=false;cfg.external_display_value=0;cfg.output_power=false;
    cfg.fallback_board=m5::board_t::board_M5StackCoreS3;
    M5.begin(cfg);M5.Display.setRotation(1);M5.Display.setBrightness(160);
    canvas.setColorDepth(16);
    if(!canvas.createSprite(320,240)) {M5.Display.println("Display buffer failed");while(true) delay(1000);}
    initError=beginRx();ready=initError==ESP_OK;lastLog=millis();draw();
}
void loop() {
    if(ready) receive();
    M5.update();
    if(M5.Touch.getCount()) {
        auto t=M5.Touch.getDetail();
        if(t.wasPressed() && t.x>=216 && t.x<312 && t.y>=202 && t.y<232) resetStats();
    }
    if(Serial && Serial.available()) {int c=Serial.read();if(c=='c'||c=='C') resetStats();}
    const uint32_t now=millis();
    if(now-lastLog>=1000u) log();
    if(now-lastDraw>=200u) {lastDraw=now;draw();}
    delay(1);
}
