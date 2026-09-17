#include <stdint.h>
extern uint32_t _stack_top,_bss_start,_bss_end,_data_start,_data_end,_data_load;
void app_main(void);
void I2C1_Handler(void);
void app_fault(void);
void Default_Handler(void) { app_fault(); for(;;) __asm volatile("nop"); }
void Reset_C(void) {
    uint32_t *src=&_data_load;
    for(uint32_t *p=&_data_start;p<&_data_end;) *p++=*src++;
    for(uint32_t *p=&_bss_start;p<&_bss_end;) *p++=0;
    *(volatile uint32_t*)0xE000ED08=0; /* VTOR */
    app_main();
    Default_Handler();
}
__attribute__((naked)) void Reset_Handler(void) {
    /* Enable SRAM controllers before the first stack access, as SDK startup. */
    __asm volatile("cpsid i\nldr r0, =0x40000220\nmovs r1, #56\nstr r1, [r0]\nb Reset_C");
}
__attribute__((section(".vectors"),used))
const uintptr_t vectors[72]={
    [0]=(uintptr_t)&_stack_top,[1]=(uintptr_t)Reset_Handler,
    [2]=(uintptr_t)Default_Handler,[3]=(uintptr_t)Default_Handler,
    [4]=(uintptr_t)Default_Handler,[5]=(uintptr_t)Default_Handler,
    [6]=(uintptr_t)Default_Handler,
    /* 7..10 are filled by the QN9090 ROM image packer. */
    [11]=(uintptr_t)Default_Handler,[12]=0,[13]=0,
    [14]=(uintptr_t)Default_Handler,[15]=(uintptr_t)Default_Handler,
    [16 ... 29]=(uintptr_t)Default_Handler,
    [30]=(uintptr_t)I2C1_Handler,
    [31 ... 71]=(uintptr_t)Default_Handler
};
