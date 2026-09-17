/* Executes the actual application loop against a simulated UWB boundary.
 * This proves host recovery/control flow, not RF interoperability. */
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#define MESH_HOST_TEST 1
#include "../../source/mesh_app.c"
static uint32_t tick,limit,release_at;
static uint8_t hwstate[2];
static unsigned starts[2],stops[2],boots,meshes,resets,query_fail;
static bool feed_ranges;
static uint32_t configs[2][15];
static phRangingParams_t params[2];
static jmp_buf done;
static char last_mesh[600];
void *mLogMutex;
void range_uart_start(void){}
void range_uart_log(void){}
uint32_t xTaskGetTickCount(void){return tick;}
size_t xPortGetFreeHeapSize(void){return 10000;}
int mock_printf(const char *fmt,...){
    if(!strncmp(fmt,"BOOT,",5))boots++;
    if(!strncmp(fmt,"MESH,",5)){
        meshes++;va_list args;va_start(args,fmt);vsnprintf(last_mesh,sizeof(last_mesh),fmt,args);va_end(args);
    }
    return 0;
}
void phOsalUwb_LockMutex(void *p){(void)p;}
void phOsalUwb_UnlockMutex(void *p){(void)p;}
void phOsalUwb_Delay(uint32_t ms){
    tick+=ms;
    if(feed_ranges)for(unsigned i=0;i<2;i++)if(hwstate[i]==2){
        phRangingData_t r={0};r.sessionId=sessions[i].id;r.no_of_measurements=1;
        mesh_p16(r.ranging_meas.range_meas_twr[0].mac_addr,sessions[i].peer);
        r.ranging_meas.range_meas_twr[0].distance=123;
        mesh_callback(UWBD_RANGING_DATA,&r);
    }
    if(limit && tick>=limit)longjmp(done,1);
}
void RESET_SystemReset(void){resets++;longjmp(done,2);}
int RNG_Init(void){return 0;}
int RNG_HwGetRandomNo(uint32_t *p){*p=0x11223344;return 0;}
int phOsalUwb_Thread_Create(void **a,void (*b)(void *),void *c){(void)a;(void)b;(void)c;return 0;}
void AppCallback(eNotificationType t,void *p){(void)t;(void)p;}
tUWBAPI_STATUS RadioConfigFull_GroupDelay(bool b){(void)b;return 0;}
tUWBAPI_STATUS demo_sr040_swup_update_safe(void){return 0;}
tUWBAPI_STATUS UwbApi_Init(void (*cb)(eNotificationType,void *)){(void)cb;return 0;}
tUWBAPI_STATUS UwbApi_SessionInit(uint32_t sid,unsigned type){(void)type;hwstate[session_index(sid)]=3;return 0;}
tUWBAPI_STATUS UwbApi_SetAppConfigMultipleParams(uint32_t sid,unsigned n,const UWB_AppParams_List_t *p){for(unsigned j=0;j<n;j++)configs[session_index(sid)][p[j].id]=p[j].value;return 0;}
tUWBAPI_STATUS UwbApi_GetAppConfig(uint32_t sid,eAppConfig id,uint32_t *v){*v=configs[session_index(sid)][id];return 0;}
tUWBAPI_STATUS UwbApi_SetRangingParams(uint32_t sid,phRangingParams_t *p){params[session_index(sid)]=*p;return 0;}
tUWBAPI_STATUS UwbApi_GetRangingParams(uint32_t sid,phRangingParams_t *p){*p=params[session_index(sid)];return 0;}
tUWBAPI_STATUS UwbApi_StartRangingSession(uint32_t sid){
    int i=session_index(sid);starts[i]++;
    /* Recorded trace: second START accepted as a command, then idle/reason20. */
    bool reject=i==1 && tick<release_at;
    hwstate[i]=reject?3:2;
    phUwbSessionInfo_t s={sid,hwstate[i],reject?0x20:0};
    mesh_callback(UWBD_SESSION_DATA,&s);
    return reject?2:0;
}
tUWBAPI_STATUS UwbApi_StopRangingSession(uint32_t sid){int i=session_index(sid);stops[i]++;hwstate[i]=3;return 0;}
tUWBAPI_STATUS UwbApi_GetSessionState(uint32_t sid,uint8_t *p){if(query_fail)return 2;*p=hwstate[session_index(sid)];return 0;}
tUWBAPI_STATUS UwbApi_SendData(phUwbDataPkt_t *p){assert(hwstate[session_index(p->session_id)]==2);return 0;}
#if MESH_NODE==19
#include "replay_v2_log.h"
static void test_log_and_diagnostics(void)
{
    memset(edges,0,sizeof(edges));memset(edge_updates,0,sizeof(edge_updates));
    for(unsigned i=0;i<3;i++){edges[i].cm=0xffff;edges[i].at=0-65535u;}
    range_ok=range_bad=0;
    unsigned visible=0;
    for(unsigned i=0;i<COUNT(replay);i++){
        tick=replay[i].ms;
        if(!replay[i].kind){
            phRangingData_t r={0};r.sessionId=replay[i].sid;r.no_of_measurements=1;
            phRangingMesr_t *m=&r.ranging_meas.range_meas_twr[0];
            mesh_p16(m->mac_addr,replay[i].peer);m->status=replay[i].status;m->distance=replay[i].cm;
            mesh_callback(UWBD_RANGING_DATA,&r);
        }else{
            expire_state(tick);print_mesh();
            if(strstr(last_mesh,",DV=3,"))visible++;
        }
    }
    assert(range_ok==35 && range_bad==25);
    assert(visible>60 && edges[0].valid && edges[1].valid && !edges[2].valid);
    assert(edges[0].cm==18 && edges[1].cm==41);
    printf("PASS node19: real v2 trace replay:35 valid ranges, DV=3 in%u output rows, no data packets needed\n",visible);
    tick=10000;edges[0]=(edge_t){0};
    phRangingData_t r={0};r.sessionId=S_AB;r.no_of_measurements=1;r.seq_ctr=11;
    phRangingMesr_t *m=&r.ranging_meas.range_meas_twr[0];mesh_p16(m->mac_addr,ADDR_B);
    m->distance=57;m->nLos=1;mesh_callback(UWBD_RANGING_DATA,&r);
    range_sample_t snap;mesh_range_snapshot(&snap);
    uint32_t ok=snap.updates[0],bad=snap.failures[0];
    assert(snap.cm[0]==57 && snap.nlos[0]==1 && snap.status[0]==0 && snap.result_seq[0]==11);
    assert(snap.valid&1);assert(snap.seen&1);
    tick+=200;r.seq_ctr=12;m->status=0x21;m->nLos=255;m->distance=0xffff;
    mesh_callback(UWBD_RANGING_DATA,&r);mesh_range_snapshot(&snap);
    assert(snap.cm[0]==57 && snap.nlos[0]==1 && snap.age[0]==200); // Saved pair unchanged.
    assert(snap.status[0]==0x21 && snap.result_nlos[0]==255 && snap.result_cm[0]==65535);
    assert(snap.result_age[0]==0 && snap.result_seq[0]==12 && snap.failures[0]==bad+1 && snap.updates[0]==ok);
    tick+=200;r.seq_ctr=13;m->status=0; // OK status with sentinel still counts as failed.
    mesh_callback(UWBD_RANGING_DATA,&r);mesh_range_snapshot(&snap);
    assert(snap.failures[0]==bad+2 && snap.cm[0]==57 && snap.age[0]==400);
    tick+=200;r.seq_ctr=14;m->distance=58;m->nLos=7; // Preserve unfamiliar raw nLos codes.
    mesh_callback(UWBD_RANGING_DATA,&r);mesh_range_snapshot(&snap);
    assert(snap.nlos[0]==7 && snap.result_nlos[0]==7 && snap.cm[0]==58 && snap.max_gap[0]==600);
    mesh_p16(m->mac_addr,0x9999);m->nLos=0;m->distance=999;
    mesh_callback(UWBD_RANGING_DATA,&r);mesh_range_snapshot(&snap);assert(snap.cm[0]==58);
    tick+=3001;expire_state(tick);mesh_range_snapshot(&snap);assert(!snap.valid && snap.seen==3);
    reset_seen=1;mesh_range_snapshot(&snap);assert(!snap.ready && !snap.valid);reset_seen=0;
    puts("PASS diagnostics: matching peer, saved nLos/distance pairing, failure/sentinel counts, raw unknown, freshness and reset");

}
#endif
int main(void){
    /* Simulate over five minutes, with second-session rejection for first120s. */
    tick=100;limit=360100;release_at=120100;feed_ranges=true;
    int why=setjmp(done);
    if(!why)mesh_task(NULL);
    assert(why==1 && boots==1 && resets==0 && runtime_ready);
    assert(starts[0]==1 && stops[0]==0 && starts[1]>1 && starts[1]<30);
    assert(hwstate[0]==2 && hwstate[1]==2);
#if MESH_NODE==19
    assert(meshes>1500); /* Output continues while the second session is idle. */
#endif
    assert(configs[0][RANGING_INTERVAL]==200 && configs[1][RANGING_INTERVAL]==200);
    printf("PASS node%d: 360s loop, reason20 rejection, bounded retry, later recovery, no reboot\n",MESH_NODE);
    /* Active peer disappears: only its session is restarted. */
    limit=0;feed_ranges=false;tick+=20000;
    unsigned target=MESH_NODE==22?1:0,other=1-target;
    last_range[other]=tick;
    unsigned start0=starts[target],stop0=stops[target];
    assert(recover_sessions());assert(starts[target]==start0+1 && stops[target]==stop0+1);
    /* Waiting/retry deadline crosses the 32-bit ms wrap. */
    hwstate[1]=3;last_attempt[1]=UINT32_MAX-1000u;tick=1000;
    unsigned n=starts[1];assert(recover_sessions());assert(starts[1]==n);
    tick=7000;assert(recover_sessions());assert(starts[1]==n+1);
    /* Transport loss is different: propagate failure after three bad queries. */
    query_fail=1;assert(recover_sessions());assert(recover_sessions());assert(!recover_sessions());
    printf("PASS node%d: no-progress restart, tick-wrap backoff, transport-failure escalation\n",MESH_NODE);
#if MESH_NODE==19
    test_log_and_diagnostics();
#endif
    return 0;
}
