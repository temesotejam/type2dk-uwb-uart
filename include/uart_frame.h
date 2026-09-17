/* UART wire format shared by the C transmitter and C++ receiver. */
#ifndef TYPE2DK_UART_FRAME_H
#define TYPE2DK_UART_FRAME_H
#include <stdint.h>
#include <stddef.h>
#define UART_TEST_BAUD 38400u
#define UART_TEST_PERIOD_MS 20u
#define UART_TEST_FRAME_SIZE 20u
static inline uint16_t uart_test_crc(const uint8_t *p, size_t n) {
    uint16_t crc=0xffffu;
    while(n--) {
        crc^=(uint16_t)*p++<<8;
        for(unsigned bit=0;bit<8;bit++)
            crc=(uint16_t)((crc<<1)^((crc&0x8000u)?0x1021u:0u));
    }
    return crc;
}
static inline void uart_test_put32(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i));
}
static inline uint32_t uart_test_get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static inline void uart_test_encode(uint8_t *p,uint32_t seq,uint32_t ms) {
    p[0]=0xa5;p[1]=0x5a;p[2]='2';p[3]='U';p[4]=1;p[5]=1; /* version, test-data flag */
    uart_test_put32(p+6,seq);uart_test_put32(p+10,ms);
    p[14]=0x00;p[15]=0xff;p[16]=0x55;p[17]=0xaa;
    const uint16_t crc=uart_test_crc(p,18);
    p[18]=(uint8_t)crc;p[19]=(uint8_t)(crc>>8);
}
static inline int uart_test_header(const uint8_t *p) {
    return p[0]==0xa5 && p[1]==0x5a && p[2]=='2' && p[3]=='U';
}
static inline int uart_test_valid(const uint8_t *p) {
    return uart_test_header(p) && p[4]==1 && p[5]==1 &&
        p[14]==0 && p[15]==0xff && p[16]==0x55 && p[17]==0xaa &&
        uart_test_crc(p,18)==((uint16_t)p[18]|((uint16_t)p[19]<<8));
}
#endif
