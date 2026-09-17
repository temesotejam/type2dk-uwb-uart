#ifndef MOCK_SDK_H
#define MOCK_SDK_H
/* Host-only SDK boundary. Never used by the ARM build. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define FALSE 0
#define MAX_NUM_RESPONDERS 5
#define UWBAPI_STATUS_OK 0
#define UWBAPI_STATUS_FAILED 2
#define UWBAPI_SESSION_IDLE 3
#define UWBAPI_SESSION_ACTIVATED 2
#define UWBAPI_SESSION_ERROR 255
#define gRngSuccess_d 0
#define OSAL_TASK_RETURN_TYPE void
#define portTICK_PERIOD_MS 1
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define PHOSALUWB_SET_TASKNAME(p,n) ((void)0)
typedef unsigned tUWBAPI_STATUS;
typedef unsigned eAppConfig;
enum {RANGING_ROUND_USAGE,RFRAME_CONFIG,SLOTS_PER_RR,SLOT_DURATION,RANGING_INTERVAL,MAX_RR_RETRY,CHANNEL_NUMBER,SFD_ID,PREAMBLE_CODE_INDEX,PRF_MODE,TX_ADAPTIVE_PAYLOAD_POWER,AOA_RESULT_REQ,RNG_DATA_NTF,DATA_TRANSFER_MODE,RANGING_START_OFFSET};
enum {kUWB_RangingRoundUsage_DS_TWR=2,kUWB_RfFrameConfig_SP1=1,kUWB_PrfMode_62_4MHz=0,Data_Transfer_Mode_Raw=0,kUWB_DeviceRole_Initiator=1,kUWB_DeviceRole_Responder=0,kUWB_DeviceType_Controller=1,kUWB_DeviceType_Controlee=0,kUWB_MultiNodeMode_UniCast=0,UWBD_RANGING_SESSION=0};
typedef enum {UWBD_RANGING_DATA,UWBD_DATA_RCV_NTF,UWBD_DATA_TRANSMIT_NTF,UWBD_SESSION_DATA,UWBD_DEVICE_RESET,UWBD_RECOVERY_NTF} eNotificationType;
typedef struct {uint8_t mac_addr[8],status;uint16_t distance;} phRangingMesr_t;
typedef struct {uint32_t sessionId;uint8_t no_of_measurements;union {phRangingMesr_t range_meas_twr[5];} ranging_meas;} phRangingData_t;
typedef struct {uint32_t session_id;uint8_t src_address[8],dst_endpoint,status;uint16_t data_size;uint8_t *data;} phUwbRcvDataPkt_t;
typedef struct {uint32_t session_id;uint8_t state,reason_code;} phUwbSessionInfo_t;
typedef struct {uint32_t session_id;uint8_t mac_address[8],sequence_number,dst_endpoint;uint16_t data_size;uint8_t *data;} phUwbDataPkt_t;
typedef struct {uint8_t deviceRole,deviceType,multiNodeMode,noOfControlees,macAddrMode,deviceMacAddr[8],dstMacAddr[40];} phRangingParams_t;
typedef struct {eAppConfig id;uint32_t value;} UWB_AppParams_List_t;
#define UWB_SET_APP_PARAM_VALUE(k,v) {k,v}
typedef void *UWBOSAL_TASK_HANDLE;
typedef struct {unsigned stackdepth,priority;void *pContext;} phOsalUwb_ThreadCreationParams_t;
uint32_t xTaskGetTickCount(void);
size_t xPortGetFreeHeapSize(void);
int mock_printf(const char *,...);
#define PRINTF mock_printf
void phOsalUwb_Delay(uint32_t);
void phOsalUwb_LockMutex(void *);
void phOsalUwb_UnlockMutex(void *);
int phOsalUwb_Thread_Create(void **,void (*)(void *),void *);
void RESET_SystemReset(void);
int RNG_Init(void);
int RNG_HwGetRandomNo(uint32_t *);
void AppCallback(eNotificationType,void *);
tUWBAPI_STATUS RadioConfigFull_GroupDelay(bool);
tUWBAPI_STATUS demo_sr040_swup_update_safe(void);
tUWBAPI_STATUS UwbApi_Init(void (*)(eNotificationType,void *));
tUWBAPI_STATUS UwbApi_SessionInit(uint32_t,unsigned);
tUWBAPI_STATUS UwbApi_SetAppConfigMultipleParams(uint32_t,unsigned,const UWB_AppParams_List_t *);
tUWBAPI_STATUS UwbApi_SetRangingParams(uint32_t,phRangingParams_t *);
tUWBAPI_STATUS UwbApi_GetRangingParams(uint32_t,phRangingParams_t *);
tUWBAPI_STATUS UwbApi_GetAppConfig(uint32_t,eAppConfig,uint32_t *);
tUWBAPI_STATUS UwbApi_StartRangingSession(uint32_t);
tUWBAPI_STATUS UwbApi_StopRangingSession(uint32_t);
tUWBAPI_STATUS UwbApi_GetSessionState(uint32_t,uint8_t *);
tUWBAPI_STATUS UwbApi_SendData(phUwbDataPkt_t *);
#endif
