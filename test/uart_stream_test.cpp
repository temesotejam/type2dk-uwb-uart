#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "uart_stream.h"
static void send(uarttest::Stream& s,uint32_t seq,uint32_t ms) {
    uint8_t b[20];uart_test_encode(b,seq,ms);
    for(auto c:b) s.feed(c);
}
int main() {
    assert(uart_test_crc((const uint8_t*)"123456789",9)==0x29b1);
    const uint8_t golden[]={0xa5,0x5a,0x32,0x55,0x01,0x01,0x78,0x56,0x34,0x12,0x04,0x03,0x02,0x01,0x00,0xff,0x55,0xaa,0xd0,0xeb};
    uint8_t b[20];uart_test_encode(b,0x12345678,0x01020304);
    assert(!memcmp(b,golden,20));
    uarttest::Stream s;
    for(size_t i=7;i<20;i++) s.feed(golden[i]); // attach mid-frame
    for(auto c:golden) s.feed(c);
    assert(s.ok==1 && s.sequence==0x12345678 && s.discarded==13);
    s={};send(s,0,2000);
    uart_test_encode(b,1,2020);b[12]^=0x80;
    for(auto c:b) s.feed(c);
    send(s,2,2040);
    assert(s.ok==2 && s.bad==1 && s.missing==1);
    // Corrupt every position, including sync. Recover at next frame.
    for(unsigned pos=0;pos<20;pos++) {
        s={};send(s,10,3000);uart_test_encode(b,11,3020);b[pos]^=0x01;
        for(auto c:b) s.feed(c);
        send(s,12,3040);
        assert(s.ok==2 && s.sequence==12 && s.missing==1);
    }
    // A removed/inserted byte must not permanently offset the stream.
    for(unsigned pos=0;pos<20;pos++) for(bool insert:{false,true}) {
        s={};send(s,30,5000);uart_test_encode(b,31,5020);
        for(unsigned i=0;i<20;i++) {
            if(i==pos && insert) s.feed(0x73);
            if(i!=pos || insert) s.feed(b[i]);
        }
        send(s,32,5040);send(s,33,5060);
        const bool prefixOnly=insert && pos==0;
        assert(s.sequence==33 && s.ok==(prefixOnly?4u:3u) && s.missing==(prefixOnly?0u:1u));
    }
    s={};send(s,0xffffffffu,0xfffffff0u);send(s,0,4);
    assert(s.missing==0 && s.restarts==0 && s.backwards==0);
    send(s,0,4);assert(s.duplicates==1);
    s={};send(s,500,12000);send(s,0,2000);send(s,1,2020);
    assert(s.restarts==1 && s.missing==0);
    // FIFO loss clears partial bytes but retains sequence history.
    s={};send(s,10,2000);s.feed(0xa5);s.feed(0x5a);s.loseSync();send(s,14,2080);
    assert(s.ok==2 && s.missing==3);
    // Noise plus false prefixes; bounds checked under sanitizers.
    s={};uint32_t random=1;
    for(unsigned i=0;i<100000;i++) {random=random*1664525u+1013904223u;s.feed(random>>24);assert(s.used<20);}
    send(s,42,9000);assert(s.sequence==42 && s.ok==1);
    puts("UART golden CRC, corruption, insertion/deletion, resync, wrap and restart tests passed");
}
