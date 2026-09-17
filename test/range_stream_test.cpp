#include "range_stream.h"
#include <array>
#include <vector>
#include <cassert>
#include <cstdio>
using Frame=std::array<uint8_t,RANGE_FRAME_SIZE>;
static Frame frame(uint32_t seq,uint32_t boot=7) {
    range_sample_t s={};s.sequence=seq;s.boot=boot;s.uptime=123456;s.ready=1;s.valid=7;
    s.cm[0]=123;s.cm[1]=456;s.age[0]=20;s.age[1]=300;
    s.accel[0]=-1000;s.accel[1]=0;s.accel[2]=1000;s.accel_age=25;
    s.range_ok=40;s.range_bad=2;s.updates[0]=19;s.updates[1]=21;
    s.state[0]=2;s.state[1]=2;s.reason[0]=0;s.reason[1]=0;
    s.irq_us=235;s.frame_ms=80;
    Frame b={};range_encode(b.data(),&s);return b;
}
template<class C> static unsigned feed(ranging::Stream &s,const C &bytes) {
    unsigned n=0;for(auto c:bytes)if(s.feed(c))++n;return n;
}
int main() {
    // Independent CCITT-FALSE standard vector and independently packed wire fixture.
    assert(uart_test_crc((const uint8_t*)"123456789",9)==0x29b1);
    auto b=frame(1);assert(range_valid(b.data()));
    assert(b[0]==0xa5 && b[3]=='R' && b[5]==19 && b[8]==1);
    assert(b[20]==123 && b[22]==0xc8 && b[23]==1);
    assert(b[28]==0x18 && b[29]==0xfc); // -1000 mg, little endian.
    ranging::Stream s;assert(feed(s,b)==1);
    assert(s.sample.accel[0]==-1000 && s.sample.cm[1]==456 && s.sample.updates[1]==21);
    assert(feed(s,b)==0 && s.duplicates==1);
    assert(feed(s,frame(0))==0 && s.backwards==1 && s.sample.sequence==1);
    assert(feed(s,frame(4))==1 && s.missing==2);
    assert(feed(s,frame(0,8))==1 && s.restarts==1 && s.missing==2);
    ranging::Stream wrap;assert(feed(wrap,frame(UINT32_MAX))==1);
    assert(feed(wrap,frame(0))==1 && wrap.missing==0 && wrap.backwards==0);
    for(unsigned i=0;i<RANGE_FRAME_SIZE*8;i++) {
        auto broken=b;broken[i/8]^=1u<<(i%8);assert(!range_valid(broken.data()));
        ranging::Stream rx;feed(rx,broken);assert(feed(rx,frame(2))==1 && rx.sample.sequence==2);
    }
    for(unsigned i=0;i<RANGE_FRAME_SIZE;i++) {
        std::vector<uint8_t> deleted(b.begin(),b.end());deleted.erase(deleted.begin()+i);
        ranging::Stream rx;feed(rx,deleted);assert(feed(rx,frame(2))==1 && rx.sample.sequence==2);
        std::vector<uint8_t> inserted(b.begin(),b.end());inserted.insert(inserted.begin()+i,0x55);
        ranging::Stream ri;feed(ri,inserted);assert(feed(ri,frame(2))==1 && ri.sample.sequence==2);
    }
    ranging::Stream noise;
    for(unsigned i=0;i<10000;i++)noise.feed((uint8_t)(i*79u));
    assert(feed(noise,b)==1 && noise.discarded>9900);
    // Diagnostic firmware is a different protocol and must never show a distance.
    uint8_t diagnostic[UART_TEST_FRAME_SIZE];uart_test_encode(diagnostic,1,100);
    ranging::Stream other;for(unsigned i=0;i<10;i++)feed(other,diagnostic);
    assert(other.ok==0 && !other.haveSequence);
    auto sample=s.sample;sample.ready=1;sample.valid=7;sample.age[0]=20;
    assert(ranging::fresh(sample,0,0));assert(!ranging::fresh(sample,0,1001));
    sample.age[0]=2800;assert(!ranging::fresh(sample,0,0));
    sample.ready=0;assert(!ranging::fresh(sample,1,0));
    sample.valid=0;Frame invalid;range_encode(invalid.data(),&sample);assert(range_valid(invalid.data()));
    sample.valid=1;range_encode(invalid.data(),&sample);assert(!range_valid(invalid.data()));
    sample.ready=1;sample.cm[0]=65535;range_encode(invalid.data(),&sample);assert(!range_valid(invalid.data()));
    std::puts("PASS range stream: CRC, all single-bit errors, insertion/deletion recovery, protocol separation, counters and staleness");
}
