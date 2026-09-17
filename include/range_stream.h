#pragma once
#include "range_frame.h"
namespace ranging {
struct Stream {
    uint8_t buffer[RANGE_FRAME_SIZE]={};
    size_t used=0;
    uint32_t bytes=0,ok=0,bad=0,discarded=0,missing=0,duplicates=0,restarts=0,backwards=0;
    bool haveSequence=false;
    range_sample_t sample={};
    void drop() {for(size_t i=1;i<used;i++) buffer[i-1]=buffer[i];--used;++discarded;}
    void loseSync() {discarded+=(uint32_t)used;used=0;}
    bool feed(uint8_t c) {
        ++bytes;buffer[used++]=c;
        while(used>=4 && !range_header(buffer)) drop();
        if(used<RANGE_FRAME_SIZE) return false;
        if(!range_valid(buffer)) {++bad;drop();return false;}
        range_sample_t next={};range_decode(buffer,&next);used=0;++ok;
        if(haveSequence && next.boot==sample.boot) {
            const uint32_t delta=next.sequence-sample.sequence;
            // Do not let duplicates/reordered frames refresh live measurements.
            if(!delta) {++duplicates;return false;}
            if(delta>=0x80000000u) {++backwards;return false;}
            missing+=delta-1u;
        } else if(haveSequence) ++restarts;
        sample=next;haveSequence=true;return true;
    }
};
// The sender's age is at snapshot time. A 500 ms transport allowance makes
// this display conservative; the wire is only ~90 ms in the nominal build.
inline bool fresh(const range_sample_t &s,unsigned item,uint32_t sinceRx) {
    if(!s.ready || item>2 || !(s.valid&(1u<<item)) || sinceRx>1000u) return false;
    const uint32_t age=item<2?s.age[item]:s.accel_age;
    return age+sinceRx+500u<=RANGE_STALE_MS;
}
}
