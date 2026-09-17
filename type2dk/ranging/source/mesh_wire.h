#ifndef MESH_WIRE_H
#define MESH_WIRE_H
#include <stdint.h>
#include <stdbool.h>
#define MESH_PACKET_SIZE 40u
#define MESH_STALE_MS 3000u
static inline uint16_t mesh_u16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static inline uint32_t mesh_u32(const uint8_t *p){return mesh_u16(p)|((uint32_t)mesh_u16(p+2)<<16);}
static inline void mesh_p16(uint8_t *p,uint16_t v){p[0]=v;p[1]=v>>8;}
static inline void mesh_p32(uint8_t *p,uint32_t v){mesh_p16(p,v);mesh_p16(p+2,v>>16);}
static inline uint16_t mesh_crc16(const uint8_t *p,unsigned n){
 uint16_t c=0xffff;while(n--){c^=(uint16_t)*p++<<8;for(unsigned i=0;i<8;i++)c=(c&0x8000)?(uint16_t)((c<<1)^0x1021):(uint16_t)(c<<1);}return c;
}
static inline bool mesh_packet_ok(const uint8_t *p,unsigned n){
 return n==MESH_PACKET_SIZE && p[0]==0xd2 && p[1]==0x4d && p[2]==2 && p[33]==0 && !(p[32]&0xf0) && mesh_u16(p+38)==mesh_crc16(p,38);
}
static inline uint16_t mesh_age16(uint32_t now,uint32_t then){uint32_t n=now-then;return n>65535?65535:(uint16_t)n;}
static inline bool mesh_fresh(bool valid,uint32_t now,uint32_t at){return valid && (uint32_t)(now-at)<=MESH_STALE_MS;}
static inline bool mesh_sequence_new(bool seen,uint32_t old_boot,uint32_t boot,uint32_t old_seq,uint32_t seq){
 return !seen || old_boot!=boot || (int32_t)(seq-old_seq)>0;
}
static inline bool mesh_retry_due(uint32_t now,uint32_t progress,uint32_t attempt,uint32_t interval){
 return (uint32_t)(now-progress)>=interval && (uint32_t)(now-attempt)>=interval;
}
#endif
