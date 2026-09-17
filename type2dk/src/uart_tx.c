/* Type2DK UART TX diagnostic v1. PIO13 -> CoreS3 GPIO2.
 * Standalone test data; no UWB/BLE, no I2C, no flash writes.
 * GPIO bit timing uses the same 32 MHz FRO clock as diagnostic v5.
 */
#include "QN9090.h"
#include "uart_frame.h"
#define CPU_HZ 32000000u
#define TIMER_MASK 0x00ffffffu
#define BIT_TICKS ((CPU_HZ+UART_TEST_BAUD/2)/UART_TEST_BAUD)
static uint32_t tick_ms,last_timer,remainder,sequence,frames;
static int uart_ready;
static void poll_tick(void) {
    const uint32_t now=SysTick->VAL;
    const uint32_t cycles=((last_timer-now)&TIMER_MASK)+remainder;
    last_timer=now;tick_ms+=cycles/(CPU_HZ/1000u);remainder=cycles%(CPU_HZ/1000u);
}
static void wait_ms(uint32_t delay) {
    const uint32_t start=tick_ms;
    while((uint32_t)(tick_ms-start)<delay) poll_tick();
}
static void putch(char c) {
    for(unsigned n=0;n<100000u;n++) {
        poll_tick();
        if(USART0->FIFOSTAT&USART_FIFOSTAT_TXNOTFULL_MASK) {USART0->FIFOWR=(uint8_t)c;return;}
    }
}
static void puts_uart(const char *s) {while(*s) putch(*s++);}
static void dec(uint32_t n) {
    char b[10];unsigned k=0;
    do {b[k++]=(char)('0'+n%10u);n/=10u;} while(n);
    while(k) putch(b[--k]);
}
static void usb_uart_init(void) {
    SYSCON->USARTCLKSEL=0;
    SYSCON->AHBCLKCTRLSET[1]=SYSCON_AHBCLKCTRL1_USART0_MASK;
    SYSCON->PRESETCTRLSET[1]=SYSCON_PRESETCTRL1_USART0_RST_MASK;
    while(!(SYSCON->PRESETCTRL[1]&SYSCON_PRESETCTRL1_USART0_RST_MASK)) {}
    SYSCON->PRESETCTRLCLR[1]=SYSCON_PRESETCTRL1_USART0_RST_MASK;
    while(SYSCON->PRESETCTRL[1]&SYSCON_PRESETCTRL1_USART0_RST_MASK) {}
    const uint32_t config=IOCON_PIO_FUNC(2)|IOCON_PIO_MODE(0)|IOCON_PIO_DIGIMODE(1)|IOCON_PIO_FILTEROFF(1);
    IOCON->PIO[0][8]=config;IOCON->PIO[0][9]=config;
    FLEXCOMM0->PSELID=FLEXCOMM_PSELID_PERSEL(1);
    USART0->CFG=0;USART0->CTL=0;
    USART0->FIFOCFG=USART_FIFOCFG_EMPTYTX_MASK|USART_FIFOCFG_ENABLETX_MASK;
    uint32_t diff_best=0xffffffffu,osr_best=15,brg_best=0;
    for(int osr=15;osr>=8;--osr) {
        uint32_t brg=(((CPU_HZ*10u)/((uint32_t)(osr+1)*115200u))-5u)/10u;
        uint32_t baud=CPU_HZ/((uint32_t)(osr+1)*(brg+1));
        uint32_t diff=baud>115200u?baud-115200u:115200u-baud;
        if(diff<diff_best) {diff_best=diff;osr_best=(uint32_t)osr;brg_best=brg;}
    }
    USART0->OSR=osr_best;USART0->BRG=brg_best;
    USART0->CFG=USART_CFG_DATALEN(1)|USART_CFG_ENABLE_MASK;uart_ready=1;
}
static void tx_init(void) {
    GPIO->DIRCLR[0]=(1u<<12)|(1u<<13);
    GPIO->SET[0]=1u<<13; /* preload idle HIGH before output enable */
    const uint32_t config=IOCON_PIO_FUNC(0)|IOCON_PIO_MODE(0)|IOCON_PIO_DIGIMODE(1)|IOCON_PIO_FILTEROFF(1);
    IOCON->PIO[0][12]=config; /* unused, high impedance, no pulls */
    IOCON->PIO[0][13]=config; /* push-pull GPIO, no open-drain, no pulls */
    GPIO->DIRSET[0]=1u<<13;
}
static inline void wait_bit(uint32_t start,uint32_t deadline) {
    while(((start-SysTick->VAL)&TIMER_MASK)<deadline) {}
}
static void tx_byte(uint8_t byte) {
    const uint32_t mask=__get_PRIMASK();__disable_irq();
    const uint32_t start=SysTick->VAL;
    GPIO->B[0][13]=0;
    for(unsigned i=0;i<8;i++) {
        wait_bit(start,(i+1u)*BIT_TICKS);
        GPIO->B[0][13]=(uint8_t)((byte>>i)&1u);
    }
    wait_bit(start,9u*BIT_TICKS);GPIO->B[0][13]=1;
    wait_bit(start,10u*BIT_TICKS);__set_PRIMASK(mask);
    poll_tick();
}
void I2C1_Handler(void) {} /* shared startup vector; peripheral stays disabled */
void app_fault(void) {
    if(uart_ready) {puts_uart("FAULT,2DK_UART_TX_V1,exception=");dec(__get_IPSR());puts_uart("\r\n");}
    for(;;) __NOP();
}
void app_main(void) {
    PMC->FRO192M|=PMC_FRO192M_DIVSEL(1u<<1);
    SYSCON->MAINCLKSEL=3;SYSCON->AHBCLKDIV=0;
    SYSCON->OSC32CLKSEL&=~SYSCON_OSC32CLKSEL_SEL32MHZ_MASK;
    SYSCON->ASYNCAPBCTRL|=SYSCON_ASYNCAPBCTRL_ENABLE_MASK;
    ASYNC_SYSCON->ASYNCAPBCLKSELA=ASYNC_SYSCON_ASYNCAPBCLKSELA_SEL(0);
    __DSB();
    SYSCON->AHBCLKCTRLSET[0]=SYSCON_AHBCLKCTRL0_IOCON_MASK|SYSCON_AHBCLKCTRL0_GPIO_MASK;
    SYSCON->SYSTICKCLKDIV=0;
    SysTick->LOAD=TIMER_MASK;SysTick->VAL=0;
    SysTick->CTRL=SysTick_CTRL_CLKSOURCE_Msk|SysTick_CTRL_ENABLE_Msk;
    __DSB();__ISB();last_timer=SysTick->VAL;
    usb_uart_init();
    puts_uart("BOOT,2DK_UART_TX_V1,usb_baud=115200,tx=PIO13,baud=38400,format=8N1,test_data=1\r\n");
    wait_ms(2000);tx_init();
    puts_uart("READY,PIO13=TX,PIO12=INPUT,external_pullups=NONE,period_ms=20,bytes=20\r\n");
    uint32_t next=tick_ms+20u,last_log=tick_ms;
    for(;;) {
        poll_tick();
        if((int32_t)(tick_ms-next)>=0) {
            uint8_t frame[UART_TEST_FRAME_SIZE];
            uart_test_encode(frame,sequence++,tick_ms);
            for(unsigned i=0;i<sizeof frame;i++) tx_byte(frame[i]);
            ++frames;next+=UART_TEST_PERIOD_MS;
            if((int32_t)(tick_ms-next)>=0) next=tick_ms+UART_TEST_PERIOD_MS;
        }
        if((uint32_t)(tick_ms-last_log)>=1000u) {
            last_log=tick_ms;puts_uart("UART_TX,v=1,baud=38400,frames=");dec(frames);
            puts_uart(",seq=");dec(sequence-1u);puts_uart(",uptime_ms=");dec(tick_ms);
            puts_uart(",pio13=");dec((GPIO->PIN[0]>>13)&1u);puts_uart("\r\n");
        }
    }
}
