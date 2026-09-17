/* Background software UART. Never alter the RTOS SysTick or SDK timers.
 * One byte masks IRQs for start+8 data bits (~235 us); the stop bit is
 * interruptible. Sleep one RTOS tick after every four bytes (~1 ms).
 * This bounds interference; ranging coexistence still needs a board test.
 */
#include "range_uart.h"
#include "QN9090.h"
#include "FreeRTOS.h"
#include "task.h"
#include "fsl_debug_console.h"
#include "phOsalUwb_Thread.h"
#if MESH_UART_ENABLE
static volatile uint32_t frames,max_irq_cycles,frame_ms,overruns,timer_fault;
static uint32_t bit_ticks,sequence;
static inline int wait_at(uint32_t start,uint32_t target) {
    /* A stopped debug counter must not hang with interrupts disabled. */
    for(uint32_t n=0;n<bit_ticks*4u;n++) if((uint32_t)(DWT->CYCCNT-start)>=target) return 1;
    return 0;
}
static int tx_byte(uint8_t byte) {
    const uint32_t mask=__get_PRIMASK();__disable_irq();
    const uint32_t start=DWT->CYCCNT;
    GPIO->B[0][13]=0;
    for(unsigned i=0;i<8;i++) {
        if(!wait_at(start,(i+1u)*bit_ticks)) goto fail;
        GPIO->B[0][13]=(uint8_t)((byte>>i)&1u);
    }
    if(!wait_at(start,9u*bit_ticks)) goto fail;
    GPIO->B[0][13]=1;
    const uint32_t elapsed=DWT->CYCCNT-start;
    __set_PRIMASK(mask);
    if(elapsed>max_irq_cycles) max_irq_cycles=elapsed;
    return wait_at(start,10u*bit_ticks);
fail:
    GPIO->B[0][13]=1;__set_PRIMASK(mask);return 0;
}
static int tx_init(void) {
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;
    if(DWT->CTRL&DWT_CTRL_NOCYCCNT_Msk) return 0;
    DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;__DSB();__ISB();
    uint32_t before=DWT->CYCCNT;
    for(unsigned i=0;i<32;i++) __NOP();
    if(DWT->CYCCNT==before || SystemCoreClock<12000000u) return 0;
    bit_ticks=(SystemCoreClock+UART_TEST_BAUD/2u)/UART_TEST_BAUD;
    SYSCON->AHBCLKCTRLSET[0]=SYSCON_AHBCLKCTRL0_GPIO_MASK|SYSCON_AHBCLKCTRL0_IOCON_MASK;
    GPIO->DIRCLR[0]=(1u<<12)|(1u<<13);GPIO->SET[0]=1u<<13;
    const uint32_t config=IOCON_PIO_FUNC(0)|IOCON_PIO_MODE(0)|IOCON_PIO_DIGIMODE(1)|IOCON_PIO_FILTEROFF(1);
    IOCON->PIO[0][12]=config;IOCON->PIO[0][13]=config;GPIO->DIRSET[0]=1u<<13;
    return 1;
}
static void uart_task(void *unused) {
    (void)unused;
    if(!tx_init()) {timer_fault=1;vTaskDelete(NULL);return;}
    TickType_t last=xTaskGetTickCount();
    for(;;) {
        range_sample_t s={0};uint8_t b[RANGE_FRAME_SIZE];
        mesh_range_snapshot(&s);
        s.sequence=sequence++;s.irq_us=range_clamp16(max_irq_cycles/(SystemCoreClock/1000000u));
        s.frame_ms=range_clamp16(frame_ms);s.overruns=range_clamp16(overruns);
        range_encode(b,&s);
        TickType_t start=xTaskGetTickCount();
        int complete=1;
        for(unsigned i=0;i<sizeof(b);i++) {
            /* Discard an old partial snapshot after prolonged preemption.
             * The receiver can resynchronize at the next complete frame. */
            if((uint32_t)((uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS-s.uptime)>400u) {
                complete=0;break;
            }
            if(!tx_byte(b[i])) {timer_fault=1;vTaskDelete(NULL);return;}
            if((i&3u)==3u) vTaskDelay(1); /* UWB tasks retain priority. */
        }
        if(complete) frames++;
        frame_ms=(uint32_t)(xTaskGetTickCount()-start)*portTICK_PERIOD_MS;
        /* Never transmit a backlog after a long higher-priority UWB operation. */
        if((uint32_t)(xTaskGetTickCount()-last)*portTICK_PERIOD_MS>=RANGE_PERIOD_MS) {
            overruns++;last=xTaskGetTickCount();
        }
        vTaskDelayUntil(&last,pdMS_TO_TICKS(RANGE_PERIOD_MS));
    }
}
#endif
void range_uart_start(void) {
#if MESH_UART_ENABLE
    phOsalUwb_ThreadCreationParams_t p={0};UWBOSAL_TASK_HANDLE h=NULL;
    p.stackdepth=768;PHOSALUWB_SET_TASKNAME(p,"RangeUart");p.priority=1;p.pContext=NULL;
    if(phOsalUwb_Thread_Create((void**)&h,uart_task,&p)!=0) timer_fault=2;
#endif
}
void range_uart_log(void) {
#if MESH_UART_ENABLE
    PRINTF("RANGE_UART,v=1,baud=38400,frames=%lu,irq_us=%lu,frame_ms=%lu,overruns=%lu,fault=%lu\r\n",
       (unsigned long)frames,(unsigned long)(max_irq_cycles/(SystemCoreClock/1000000u)),
       (unsigned long)frame_ms,(unsigned long)overruns,(unsigned long)timer_fault);
#else
    PRINTF("RANGE_UART,v=1,enabled=0\r\n");
#endif
}
