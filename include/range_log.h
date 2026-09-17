#pragma once
#include <cstdio>
#include "range_stream.h"

// Log each accepted UART snapshot. Repeated/skipped UWB samples can be identified
// from updates and uci_seq; snapshots are NOT a lossless per-ranging-event stream.
inline int range_data_line(char *out,size_t size,const range_sample_t &r,uint32_t rx_ms) {
    unsigned valid=0;
    for(unsigned i=0;i<2;i++)if(ranging::fresh(r,i,0))valid|=1u<<i;
    return std::snprintf(out,size,
        "RANGE_DATA,v=2,rx_ms=%lu,seq=%lu,boot=%08lx,tx_ms=%lu,ready=%u,valid=%u,d19_21_cm=%u,d19_22_cm=%u,age_at_tx_ms=%u/%u,updates=%lu/%lu,r_ok=%lu,r_bad=%lu\n",
        (unsigned long)rx_ms,(unsigned long)r.sequence,(unsigned long)r.boot,(unsigned long)r.uptime,
        r.ready,valid,r.cm[0],r.cm[1],r.age[0],r.age[1],(unsigned long)r.updates[0],(unsigned long)r.updates[1],
        (unsigned long)r.range_ok,(unsigned long)r.range_bad);
}
inline const char *range_result_state(const range_sample_t &r,unsigned i) {
    if(!(r.seen&(1u<<i)))return "WAITING";
    if(!r.ready || (uint32_t)r.result_age[i]+500u>RANGE_STALE_MS)return "STALE";
    if(r.status[i])return "FAILED";
    return r.result_cm[i]==65535u?"NO_DISTANCE":"OK";
}
inline int range_link_line(char *out,size_t size,const range_sample_t &r,unsigned i,uint32_t rx_ms) {
    return std::snprintf(out,size,
        "RANGE_LINK,v=2,rx_ms=%lu,seq=%lu,boot=%08lx,peer=%u,result_seen=%u,result=%s,status=0x%02x,nlos_raw=%u,result_cm=%u,result_age_ms=%u,uci_seq=%lu,saved_nlos_raw=%u,ok=%lu,fail=%lu,max_gap_ms=%u,session=%u,reason=0x%02x,start_count=%u\n",
        (unsigned long)rx_ms,(unsigned long)r.sequence,(unsigned long)r.boot,i?22u:21u,(r.seen>>i)&1u,
        range_result_state(r,i),r.status[i],r.result_nlos[i],r.result_cm[i],r.result_age[i],
        (unsigned long)r.result_seq[i],r.nlos[i],(unsigned long)r.updates[i],(unsigned long)r.failures[i],
        r.max_gap[i],r.state[i],r.reason[i],r.starts[i]);
}
