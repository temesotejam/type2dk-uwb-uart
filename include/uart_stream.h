#pragma once
#include "uart_frame.h"
namespace uarttest {
struct Stream {
    uint8_t buffer[UART_TEST_FRAME_SIZE]={};
    size_t used=0;
    uint32_t bytes=0,ok=0,bad=0,discarded=0,missing=0,duplicates=0,restarts=0,backwards=0;
    uint32_t sequence=0,uptime=0;
    bool haveSequence=false;
    void drop() {
        for(size_t i=1;i<used;i++) buffer[i-1]=buffer[i];
        --used;++discarded;
    }
    void loseSync() { used=0; }
    bool feed(uint8_t c) {
        ++bytes;buffer[used++]=c;
        while(used>=4 && !uart_test_header(buffer)) drop();
        if(used<UART_TEST_FRAME_SIZE) return false;
        if(!uart_test_valid(buffer)) { ++bad;drop();return false; }
        const uint32_t next=uart_test_get32(buffer+6),ms=uart_test_get32(buffer+10);
        if(haveSequence) {
            const uint32_t delta=next-sequence;
            // Unsigned subtraction accepts normal sequence and uptime wrap.
            // A backwards uptime plus low sequence is treated as a sender reboot.
            if((uint32_t)(ms-uptime)>0x80000000u && next<sequence) ++restarts;
            else if(delta==0) ++duplicates;
            else if(delta<0x80000000u) missing+=delta-1u;
            else ++backwards;
        }
        sequence=next;uptime=ms;haveSequence=true;++ok;used=0;
        return true;
    }
};
}
