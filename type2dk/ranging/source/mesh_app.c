/* Three-node Type2DK collector, SR040 v04.03.14.
 * UWB commands run ONLY on the application task. Notifications copy state only.
 * 21 initiates to 19 and 22; 22 initiates to 19; 19 collects both uplinks.
 */
#ifdef MESH_HOST_TEST
#include "mock_sdk.h"
#else
#include "phUwb_BuildConfig.h"
#include "UwbApi.h"
#include "AppInternal.h"
#include "AppRecovery.h"
#include "Demo_SR040_RadioConfigAndGroupDelay.h"
#include "phOsalUwb_Thread.h"
#include "FreeRTOS.h"
#include "task.h"
#include "fsl_debug_console.h"
#include "fsl_reset.h"
#include "RNG_Interface.h"
#endif
#include "mesh_port.h"
#include "mesh_wire.h"
#include "range_uart.h"
#include <string.h>
#ifndef MESH_NODE
#error MESH_NODE must be 19,21,22
#endif
#define S_AB 0x19210001u
#define S_AC 0x19220001u
#define S_BC 0x21220001u
#define ADDR_A 0x1111u
#define ADDR_B 0x2222u
#define ADDR_C 0x3333u
#define COUNT(x) (sizeof(x)/sizeof((x)[0]))
typedef struct {uint16_t cm;uint32_t at;uint8_t valid,status;} edge_t;
typedef struct {int16_t xyz[3];uint32_t at,seq,uptime,rxAt,boot;uint8_t valid,seen;} accel_t;
typedef struct {uint32_t id,interval;uint16_t peer;uint8_t init,offset;} session_t;
#if MESH_NODE==19
#define LOCAL_INDEX 0
#define LOCAL_ADDR ADDR_A
static const session_t sessions[]={{S_AB,200,ADDR_B,0,0},{S_AC,200,ADDR_C,0,0}};
#elif MESH_NODE==21
#define LOCAL_INDEX 1
#define LOCAL_ADDR ADDR_B
static const session_t sessions[]={{S_AB,200,ADDR_A,1,0},{S_BC,200,ADDR_C,1,120}};
#elif MESH_NODE==22
#define LOCAL_INDEX 2
#define LOCAL_ADDR ADDR_C
static const session_t sessions[]={{S_BC,200,ADDR_B,0,0},{S_AC,200,ADDR_A,1,60}};
#else
#error Bad MESH_NODE
#endif
static edge_t edges[3];
static uint32_t edge_updates[3];
static accel_t accels[3];
static volatile uint32_t range_ok,range_bad,rx_ok,rx_bad,tx_ok,tx_bad;
static volatile uint8_t session_state[2],session_reason[2];
static volatile uint8_t reset_seen, runtime_ready;
static volatile uint32_t heartbeat_ms,last_range[2],last_progress[2];
static uint32_t last_attempt[2],boot_id;
static uint8_t api_errors[2];
#if MESH_NODE==19
static uint32_t print_seq;
#endif
static uint32_t now_ms(void){return (uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS);}
static int session_index(uint32_t id){for(unsigned i=0;i<COUNT(sessions);i++)if(sessions[i].id==id)return (int)i;return -1;}
static void on_range(const phRangingData_t *r)
{
    if(!r || session_index(r->sessionId)<0 || r->no_of_measurements>MAX_NUM_RESPONDERS)return;
    int si=session_index(r->sessionId);
    /* Collector owns its two directly observed edges; no payload dependency.
     * Only 21--22 needs a remote state packet. */
    int e=-1;uint16_t expected=sessions[si].peer;
#if MESH_NODE==19
    if(r->sessionId==S_AB)e=0;
    if(r->sessionId==S_AC)e=1;
#elif MESH_NODE==21
    if(r->sessionId==S_AB)e=0;
    if(r->sessionId==S_BC)e=2;
#elif MESH_NODE==22
    if(r->sessionId==S_AC)e=1;
#endif
    /* Responder-only link progress is also useful for reconnection. */
    for(unsigned j=0;j<r->no_of_measurements;j++){
        const phRangingMesr_t *m=&r->ranging_meas.range_meas_twr[j];
        if(mesh_u16(m->mac_addr)==expected && m->status==UWBAPI_STATUS_OK && m->distance!=0xffff)
            last_range[si]=now_ms();
    }
    if(e<0)return;
    for(unsigned i=0;i<r->no_of_measurements;i++){
        const phRangingMesr_t *m=&r->ranging_meas.range_meas_twr[i];
        if(mesh_u16(m->mac_addr)!=expected)continue;
        taskENTER_CRITICAL();
        edges[e].status=m->status;
        if(m->status==UWBAPI_STATUS_OK && m->distance!=0xffff){
            edges[e].cm=m->distance;edges[e].at=now_ms();edges[e].valid=1;range_ok++;edge_updates[e]++;
        }else{range_bad++;} /* Keep the last good sample until its original time expires. */
        taskEXIT_CRITICAL();
    }
}
static void on_data(const phUwbRcvDataPkt_t *p)
{
#if MESH_NODE==19
    if(!p)return;
    const uint8_t *b=p->data;
    int n=mesh_u16(p->src_address)==ADDR_B?1:mesh_u16(p->src_address)==ADDR_C?2:-1;
    if(p->status!=UWBAPI_STATUS_OK || n<0 || p->dst_endpoint!=0 ||
       p->session_id!=(n==1?S_AB:S_AC) || !mesh_packet_ok(b,p->data_size) || b[3]!=(n==1?21:22)){
        rx_bad++;return;
    }
    uint32_t seq=mesh_u32(b+4),uptime=mesh_u32(b+8),boot=mesh_u32(b+34),now=now_ms();
    taskENTER_CRITICAL();
    accel_t *a=&accels[n];
    /* A per-boot random ID allows sequence restart following power loss/reset. */
    if(!mesh_sequence_new(a->seen,a->boot,boot,a->seq,seq)){
        rx_bad++;taskEXIT_CRITICAL();return;
    }
    a->seq=seq;a->boot=boot;a->uptime=uptime;a->rxAt=now;a->seen=1;
    for(unsigned j=0;j<3;j++)a->xyz[j]=(int16_t)mesh_u16(b+12+2*j);
    a->valid=(b[32]&1)!=0 && mesh_u16(b+18)<=MESH_STALE_MS;
    a->at=now-mesh_u16(b+18);
    for(unsigned e=0;e<3;e++){
        if(n!=1 || e!=2)continue; /* Never overwrite collector-local AB/AC. */
        edges[e].cm=mesh_u16(b+20+e*4);
        edges[e].at=now-mesh_u16(b+22+e*4);
        edges[e].valid=(b[32]&(2u<<e))!=0 && edges[e].cm!=0xffff && mesh_u16(b+22+e*4)<=MESH_STALE_MS;
    }
    last_progress[session_index(p->session_id)]=now;
    rx_ok++;
    taskEXIT_CRITICAL();
#else
    (void)p;
#endif
}
static void mesh_callback(eNotificationType type,void *data)
{
    if(type==UWBD_RANGING_DATA){on_range((const phRangingData_t*)data);return;}
    if(type==UWBD_DATA_RCV_NTF){on_data((const phUwbRcvDataPkt_t*)data);return;}
    if(type==UWBD_DATA_TRANSMIT_NTF)return;
    if(type==UWBD_SESSION_DATA && data){
        const phUwbSessionInfo_t *s=(const phUwbSessionInfo_t*)data;
        int i=session_index(s->session_id);if(i>=0){session_state[i]=s->state;session_reason[i]=s->reason_code;}
        return;
    }
    if(type==UWBD_DEVICE_RESET || type==UWBD_RECOVERY_NTF){reset_seen=1;return;}
    AppCallback(type,data);
}
static void expire_state(uint32_t now)
{
    /* Latch expiry so a disconnected value cannot become fresh at tick wrap. */
    taskENTER_CRITICAL();
    for(unsigned i=0;i<3;i++){
        if(!mesh_fresh(edges[i].valid,now,edges[i].at))edges[i].valid=0;
        if(!mesh_fresh(accels[i].valid,now,accels[i].at))accels[i].valid=0;
    }
    taskEXIT_CRITICAL();
}
static void copy_state(edge_t e[3],accel_t a[3]){
    taskENTER_CRITICAL();memcpy(e,edges,sizeof(edges));memcpy(a,accels,sizeof(accels));taskEXIT_CRITICAL();
}
#if MESH_NODE==19
extern void *mLogMutex;
static void print_mesh(void)
{
    edge_t e[3];accel_t a[3];copy_state(e,a);uint32_t t=now_ms();
    unsigned mask=0;for(unsigned i=0;i<3;i++)if(mesh_fresh(e[i].valid,t,e[i].at))mask|=1u<<i;
    /* Keep the SDK's small per-call buffer; hold its log mutex across chunks. */
    phOsalUwb_LockMutex(mLogMutex);
    PRINTF("MESH,SEQ=%lu,MS=%lu,D19_21=%u,D19_22=%u,D21_22=%u,DV=%u,"
           "DAGE=%u/%u/%u,A19=%d/%d/%d,V19=%u,A21=%d/%d/%d,V21=%u,"
           "A22=%d/%d/%d,V22=%u,AAGE=%u/%u/%u\r\n",
        (unsigned long)++print_seq,(unsigned long)t,e[0].cm,e[1].cm,e[2].cm,mask,
        mesh_age16(t,e[0].at),mesh_age16(t,e[1].at),mesh_age16(t,e[2].at),
        a[0].xyz[0],a[0].xyz[1],a[0].xyz[2],mesh_fresh(a[0].valid,t,a[0].at),
        a[1].xyz[0],a[1].xyz[1],a[1].xyz[2],mesh_fresh(a[1].valid,t,a[1].at),
        a[2].xyz[0],a[2].xyz[1],a[2].xyz[2],mesh_fresh(a[2].valid,t,a[2].at),
        mesh_age16(t,a[0].at),mesh_age16(t,a[1].at),mesh_age16(t,a[2].at));
    phOsalUwb_UnlockMutex(mLogMutex);
}
#endif
static bool step_ok(const char *step,tUWBAPI_STATUS status,uint32_t sid){
    PRINTF("INIT,NODE=%d,STEP=%s,SID=%08lx,STATUS=%u\r\n",MESH_NODE,step,(unsigned long)sid,(unsigned)status);
    return status==UWBAPI_STATUS_OK;
}
static bool configure(const session_t *s)
{
    phRangingParams_t r={0},check={0};
    const UWB_AppParams_List_t cfg[]={
        UWB_SET_APP_PARAM_VALUE(RANGING_ROUND_USAGE,kUWB_RangingRoundUsage_DS_TWR),
        UWB_SET_APP_PARAM_VALUE(RFRAME_CONFIG,kUWB_RfFrameConfig_SP1),
        UWB_SET_APP_PARAM_VALUE(SLOTS_PER_RR,25),
        UWB_SET_APP_PARAM_VALUE(SLOT_DURATION,2400),
        UWB_SET_APP_PARAM_VALUE(RANGING_INTERVAL,s->interval),
        UWB_SET_APP_PARAM_VALUE(MAX_RR_RETRY,0),
        UWB_SET_APP_PARAM_VALUE(CHANNEL_NUMBER,5),
        UWB_SET_APP_PARAM_VALUE(SFD_ID,2),
        UWB_SET_APP_PARAM_VALUE(PREAMBLE_CODE_INDEX,10),
        UWB_SET_APP_PARAM_VALUE(PRF_MODE,kUWB_PrfMode_62_4MHz),
        UWB_SET_APP_PARAM_VALUE(TX_ADAPTIVE_PAYLOAD_POWER,1),
        UWB_SET_APP_PARAM_VALUE(AOA_RESULT_REQ,0),
        UWB_SET_APP_PARAM_VALUE(RNG_DATA_NTF,1),
        UWB_SET_APP_PARAM_VALUE(DATA_TRANSFER_MODE,Data_Transfer_Mode_Raw),
        UWB_SET_APP_PARAM_VALUE(RANGING_START_OFFSET,s->offset),
    };
    if(!step_ok("SESSION",UwbApi_SessionInit(s->id,UWBD_RANGING_SESSION),s->id))return false;
    if(!step_ok("CONFIG",UwbApi_SetAppConfigMultipleParams(s->id,COUNT(cfg),cfg),s->id))return false;
    r.deviceRole=s->init?kUWB_DeviceRole_Initiator:kUWB_DeviceRole_Responder;
    r.deviceType=s->init?kUWB_DeviceType_Controller:kUWB_DeviceType_Controlee;
    r.multiNodeMode=kUWB_MultiNodeMode_UniCast;r.noOfControlees=1;r.macAddrMode=0;
    mesh_p16(r.deviceMacAddr,LOCAL_ADDR);mesh_p16(r.dstMacAddr,s->peer);
    if(!step_ok("PEERS",UwbApi_SetRangingParams(s->id,&r),s->id))return false;
    if(!step_ok("READBACK",UwbApi_GetRangingParams(s->id,&check),s->id))return false;
    if(check.deviceRole!=r.deviceRole || check.deviceType!=r.deviceType || check.multiNodeMode!=r.multiNodeMode ||
       check.noOfControlees!=1 || check.macAddrMode!=0 || memcmp(check.deviceMacAddr,r.deviceMacAddr,2) ||
       memcmp(check.dstMacAddr,r.dstMacAddr,2))return false;
    /* Read actual radio/timing values before START, not just our requested values. */
    const struct {eAppConfig id;uint32_t expected;const char *name;} verify[]={
        {SLOT_DURATION,2400,"SLOT"},{RANGING_INTERVAL,s->interval,"INTERVAL"},
        {SLOTS_PER_RR,25,"SLOTS"},{RFRAME_CONFIG,kUWB_RfFrameConfig_SP1,"RFRAME"},
        {SFD_ID,2,"SFD"},{RANGING_START_OFFSET,s->offset,"OFFSET"},
    };
    for(unsigned i=0;i<COUNT(verify);i++){
        uint32_t value=0;
        tUWBAPI_STATUS st=UwbApi_GetAppConfig(s->id,verify[i].id,&value);
        PRINTF("CONFIG,NODE=%d,SID=%08lx,PARAM=%s,VALUE=%lu,EXPECTED=%lu,STATUS=%u\r\n",
            MESH_NODE,(unsigned long)s->id,verify[i].name,(unsigned long)value,
            (unsigned long)verify[i].expected,(unsigned)st);
        if(st!=UWBAPI_STATUS_OK || value!=verify[i].expected)return false;
    }
    PRINTF("SESSION,NODE=%d,SID=%08lx,INIT=%u,SELF=%04x,PEER=%04x,INTERVAL_MS=%lu\r\n",
        MESH_NODE,(unsigned long)s->id,s->init,LOCAL_ADDR,s->peer,(unsigned long)s->interval);
    return true;
}
/* START rejection is session-local. Keep collecting and retry with backoff.
 * A successful command RSP alone does not mean that the session became Active. */
static bool start_session(unsigned i,const char *step)
{
    uint8_t state=UWBAPI_SESSION_ERROR;
    session_reason[i]=0xff; /* No reason notification received yet. */
    tUWBAPI_STATUS st=UwbApi_StartRangingSession(sessions[i].id);
    last_attempt[i]=now_ms();
    tUWBAPI_STATUS query=UwbApi_GetSessionState(sessions[i].id,&state);
    if(query!=UWBAPI_STATUS_OK){
        PRINTF("SESSION_WAIT,NODE=%d,SID=%08lx,QUERY=%u,STATUS=%u\r\n",
            MESH_NODE,(unsigned long)sessions[i].id,(unsigned)query,(unsigned)st);
        return ++api_errors[i]<3;
    }
    api_errors[i]=0;session_state[i]=state;
    if(state==UWBAPI_SESSION_ACTIVATED)session_reason[i]=0;
    PRINTF("START_RESULT,NODE=%d,SID=%08lx,STEP=%s,STATUS=%u,STATE=%u,REASON=%02x\r\n",
        MESH_NODE,(unsigned long)sessions[i].id,step,(unsigned)st,state,session_reason[i]);
    if(state==UWBAPI_SESSION_ACTIVATED){
        session_reason[i]=0;
        /* Grace period for peer synchronization, not a valid measurement. */
        last_range[i]=last_progress[i]=now_ms();
        return true;
    }
    if(state==UWBAPI_SESSION_IDLE){
        PRINTF("SESSION_WAIT,NODE=%d,SID=%08lx,REASON=%02x,ACTION=RETRY_NO_REBOOT\r\n",
            MESH_NODE,(unsigned long)sessions[i].id,session_reason[i]);
        return true;
    }
    return false; /* Missing/corrupt session needs full reinitialization. */
}
static bool recover_sessions(void)
{
    for(unsigned i=0;i<COUNT(sessions);i++){
        uint8_t state=UWBAPI_SESSION_ERROR;
        tUWBAPI_STATUS st=UwbApi_GetSessionState(sessions[i].id,&state);
        if(st!=UWBAPI_STATUS_OK){if(++api_errors[i]>=3)return false;continue;}
        api_errors[i]=0;session_state[i]=state;
        uint32_t t=now_ms(),interval=12000u+MESH_NODE*137u+i*1009u;
        bool stale=false;
        /* Ranging health is independent of UWB application-payload delivery. */
        stale=mesh_retry_due(t,last_range[i],last_attempt[i],interval);
        if(state==UWBAPI_SESSION_ACTIVATED && stale){
            PRINTF("RECOVER,NODE=%d,SID=%08lx,REASON=NO_PROGRESS,ACTION=RESTART_SESSION\r\n",MESH_NODE,(unsigned long)sessions[i].id);
            last_attempt[i]=t;
            st=UwbApi_StopRangingSession(sessions[i].id);
            if(st!=UWBAPI_STATUS_OK)continue;
            if(!start_session(i,"RESTART"))return false;
        }else if(state==UWBAPI_SESSION_IDLE){
            uint32_t retry_ms=5000u+MESH_NODE*53u+i*211u;
            if((uint32_t)(t-last_attempt[i])>=retry_ms && !start_session(i,"RETRY"))return false;
        }else if(state!=UWBAPI_SESSION_ACTIVATED){return false;}
    }
    return true;
}
static OSAL_TASK_RETURN_TYPE mesh_guard(void *unused)
{
    (void)unused;
    for(;;){
        phOsalUwb_Delay(250);
        /* Separate task: recover even if the application is blocked in an API/mutex.
         * This software guard requires the RTOS scheduler/interrupts to be alive. */
        uint32_t limit=runtime_ready?45000u:180000u;
        if((uint32_t)(now_ms()-heartbeat_ms)>limit)RESET_SystemReset();
    }
}
static OSAL_TASK_RETURN_TYPE mesh_task(void *unused)
{
    (void)unused;
    heartbeat_ms=now_ms();
    if(RNG_Init()!=gRngSuccess_d || RNG_HwGetRandomNo(&boot_id)!=gRngSuccess_d)goto fail;
    for(unsigned i=0;i<3;i++){edges[i].cm=0xffff;edges[i].at=now_ms()-65535u;accels[i].at=now_ms()-65535u;}
    PRINTF("BOOT,TYPE2DK_RANGE_UART_V1,NODE=%d,ADDR=%04x,UART=3000000\r\n",MESH_NODE,LOCAL_ADDR);
    if(!step_ok("UWB",UwbApi_Init(mesh_callback),0))goto fail;
    if(!step_ok("RADIO",RadioConfigFull_GroupDelay(FALSE),0))goto fail;
    if(!step_ok("SWUP",demo_sr040_swup_update_safe(),0))goto fail;
    reset_seen=0;
    PRINTF("IDENTITY,NODE=%d,BOOT_ID=%08lx\r\n",MESH_NODE,(unsigned long)boot_id);
    for(unsigned i=0;i<COUNT(sessions);i++)if(!configure(&sessions[i]))goto fail;
    uint8_t who=0;bool sensor=mesh_accel_init(&who);
    PRINTF("ACCEL,NODE=%d,WHO=%02x,OK=%u,FSR_G=4,ODR_HZ=100\r\n",MESH_NODE,who,sensor);
    for(unsigned i=0;i<COUNT(sessions);i++){
        if(!start_session(i,"START"))goto fail;
    }
    runtime_ready=1;
    range_uart_start();
    uint32_t sample_at=0,health_at=now_ms(),retry_at=now_ms();
#if MESH_NODE==19
    uint32_t print_at=0;
#endif
    for(;;){
        uint32_t t=now_ms();
        heartbeat_ms=t;
        expire_state(t);
        if(reset_seen)goto fail;
        if(sensor && (uint32_t)(t-retry_at)>=5000 && !mesh_fresh(accels[LOCAL_INDEX].valid,t,accels[LOCAL_INDEX].at))sensor=false;
        if(!sensor && (uint32_t)(t-retry_at)>=5000){
            sensor=mesh_accel_init(&who);retry_at=t;
            PRINTF("ACCEL,NODE=%d,WHO=%02x,OK=%u\r\n",MESH_NODE,who,sensor);
        }
        if(sensor && (uint32_t)(t-sample_at)>=50){
            int16_t v[3];sample_at=t;
            if(mesh_accel_sample(v)){
                taskENTER_CRITICAL();memcpy(accels[LOCAL_INDEX].xyz,v,sizeof(v));
                accels[LOCAL_INDEX].at=now_ms();accels[LOCAL_INDEX].valid=1;taskEXIT_CRITICAL();
            }
        }
#if MESH_NODE==19
        if((uint32_t)(t-print_at)>=100){print_mesh();print_at=t;}

#endif
        if((uint32_t)(now_ms()-health_at)>=2000){
            health_at=now_ms();
            PRINTF("HEALTH,NODE=%d,R_OK=%lu,R_BAD=%lu,TX_OK=%lu,TX_BAD=%lu,RX_OK=%lu,RX_BAD=%lu,S=%u/%u,WHY=%02x/%02x,HEAP=%u\r\n",
                MESH_NODE,(unsigned long)range_ok,(unsigned long)range_bad,(unsigned long)tx_ok,
                (unsigned long)tx_bad,(unsigned long)rx_ok,(unsigned long)rx_bad,session_state[0],session_state[1],session_reason[0],session_reason[1],(unsigned)xPortGetFreeHeapSize());
            range_uart_log();
            if(!recover_sessions())goto fail;
        }
        phOsalUwb_Delay(10);
    }
fail:
    runtime_ready=0;
    taskENTER_CRITICAL();
    for(unsigned i=0;i<3;i++){edges[i].valid=0;accels[i].valid=0;}
    taskEXIT_CRITICAL();
    PRINTF("RECOVER,NODE=%d,REASON=INIT_OR_API_ERROR,ACTION=AUTO_REBOOT\r\n",MESH_NODE);
    phOsalUwb_Delay(3000u+MESH_NODE*53u);
    RESET_SystemReset();
    for(;;){}
}
UWBOSAL_TASK_HANDLE uwb_demo_start(void)
{
    phOsalUwb_ThreadCreationParams_t p={0};UWBOSAL_TASK_HANDLE h=NULL;
    heartbeat_ms=now_ms();runtime_ready=0;
    UWBOSAL_TASK_HANDLE guard=NULL;
    p.stackdepth=512;PHOSALUWB_SET_TASKNAME(p,"MeshGuard");p.priority=5;p.pContext=NULL;
    if(phOsalUwb_Thread_Create((void**)&guard,mesh_guard,&p)!=0)RESET_SystemReset();
    p.stackdepth=2048;PHOSALUWB_SET_TASKNAME(p,"Type2DKMesh");p.priority=4;p.pContext=NULL;
    if(phOsalUwb_Thread_Create((void**)&h,mesh_task,&p)!=0)RESET_SystemReset();
    return h;
}

/* Atomic snapshot; no UART or UWB commands inside the callback/critical section. */
void mesh_range_snapshot(range_sample_t *s)
{
    memset(s,0,sizeof(*s));
    taskENTER_CRITICAL();
    uint32_t t=now_ms();s->uptime=t;s->boot=boot_id;s->ready=runtime_ready && !reset_seen;
    for(unsigned i=0;i<2;i++) {
        s->cm[i]=edges[i].cm;s->age[i]=mesh_age16(t,edges[i].at);s->updates[i]=edge_updates[i];
        if(s->ready && mesh_fresh(edges[i].valid,t,edges[i].at))s->valid|=1u<<i;
        s->state[i]=session_state[i];s->reason[i]=session_reason[i];
    }
    for(unsigned i=0;i<3;i++)s->accel[i]=accels[0].xyz[i];
    s->accel_age=mesh_age16(t,accels[0].at);
    if(s->ready && mesh_fresh(accels[0].valid,t,accels[0].at))s->valid|=4u;
    s->range_ok=range_ok;s->range_bad=range_bad;
    taskEXIT_CRITICAL();
}
