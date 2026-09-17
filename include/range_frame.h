/* Type2DK local ranging snapshot, explicit little-endian wire format. */
#ifndef TYPE2DK_RANGE_FRAME_H
#define TYPE2DK_RANGE_FRAME_H
#include "uart_frame.h"
#define RANGE_FRAME_SIZE 64u
#define RANGE_PERIOD_MS 200u
#define RANGE_STALE_MS 3000u
typedef struct {
    uint32_t sequence,uptime,boot,range_ok,range_bad,updates[2];
    uint16_t cm[2],age[2],accel_age,irq_us,frame_ms,overruns;
    int16_t accel[3];
    uint8_t ready,valid,state[2],reason[2];
} range_sample_t;
static inline void range_put16(uint8_t *p,uint16_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static inline uint16_t range_get16(const uint8_t *p) {return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static inline int16_t range_signed16(const uint8_t *p) {
    uint16_t v=range_get16(p);return (int16_t)(v<32768u?(int32_t)v:(int32_t)v-65536);
}
static inline uint16_t range_clamp16(uint32_t n) {return (uint16_t)(n>65535u?65535u:n);}
static inline int range_header(const uint8_t *p) {return p[0]==0xa5 && p[1]==0x5a && p[2]=='2' && p[3]=='R';}
static inline void range_encode(uint8_t *p,const range_sample_t *s) {
    p[0]=0xa5;p[1]=0x5a;p[2]='2';p[3]='R';p[4]=1;p[5]=19;p[6]=s->ready;p[7]=s->valid;
    uart_test_put32(p+8,s->sequence);uart_test_put32(p+12,s->uptime);uart_test_put32(p+16,s->boot);
    for(unsigned i=0;i<2;i++) {range_put16(p+20+2*i,s->cm[i]);range_put16(p+24+2*i,s->age[i]);}
    for(unsigned i=0;i<3;i++) range_put16(p+28+2*i,(uint16_t)s->accel[i]);
    range_put16(p+34,s->accel_age);uart_test_put32(p+36,s->range_ok);uart_test_put32(p+40,s->range_bad);
    for(unsigned i=0;i<2;i++) {uart_test_put32(p+44+4*i,s->updates[i]);p[52+i]=s->state[i];p[54+i]=s->reason[i];}
    range_put16(p+56,s->irq_us);range_put16(p+58,s->frame_ms);range_put16(p+60,s->overruns);
    range_put16(p+62,uart_test_crc(p,62));
}
static inline int range_valid(const uint8_t *p) {
    if(!range_header(p) || p[4]!=1 || p[5]!=19 || p[6]>1 || (p[7]&~7u) ||
       uart_test_crc(p,62)!=range_get16(p+62)) return 0;
    if(!p[6] && p[7]) return 0;
    for(unsigned i=0;i<2;i++) if((p[7]&(1u<<i)) && (range_get16(p+20+2*i)==65535u || range_get16(p+24+2*i)>RANGE_STALE_MS)) return 0;
    return !(p[7]&4u) || range_get16(p+34)<=RANGE_STALE_MS;
}
static inline void range_decode(const uint8_t *p,range_sample_t *s) {
    s->ready=p[6];s->valid=p[7];s->sequence=uart_test_get32(p+8);s->uptime=uart_test_get32(p+12);s->boot=uart_test_get32(p+16);
    for(unsigned i=0;i<2;i++) {s->cm[i]=range_get16(p+20+2*i);s->age[i]=range_get16(p+24+2*i);s->updates[i]=uart_test_get32(p+44+4*i);s->state[i]=p[52+i];s->reason[i]=p[54+i];}
    for(unsigned i=0;i<3;i++) s->accel[i]=range_signed16(p+28+2*i);
    s->accel_age=range_get16(p+34);s->range_ok=uart_test_get32(p+36);s->range_bad=uart_test_get32(p+40);
    s->irq_us=range_get16(p+56);s->frame_ms=range_get16(p+58);s->overruns=range_get16(p+60);
}
#endif
