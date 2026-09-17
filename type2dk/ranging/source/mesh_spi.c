/* Type2DK EVK: one SPI peripheral, distinct UWB/accelerometer modes and CS.
 * The TML mutex protects the WHOLE SR040 handshake, not just individual bytes.
 */
#include "mesh_port.h"
#include "phOsalUwb.h"
#include "fsl_spi.h"
#include "fsl_gpio.h"
#include "driver_config.h"
#include "fxls8962.h"
#include <string.h>
extern void *gMeshTmlMutex;
extern const spi_master_config_t *Mesh_GetUwbSpiConfig(void);

static bool transfer(uint8_t reg, uint8_t *bytes, uint32_t n, bool write)
{
    uint8_t tx[10]={0}, rx[10]={0};
    spi_master_config_t cfg;
    spi_transfer_t x={0};
    status_t st;
    if (!gMeshTmlMutex || !bytes || !n || n>8) return false;
    if (phOsalUwb_LockMutex(gMeshTmlMutex)!=UWBSTATUS_SUCCESS) return false;
    /* Refuse to switch modes if an earlier failed transaction left SR040 selected. */
    if (!GPIO_PinRead(UWB_UWBS_CS_GPIO,UWB_UWBS_CS_PORT,UWB_UWBS_CS_PIN)) {
        phOsalUwb_UnlockMutex(gMeshTmlMutex); return false;
    }
    SPI_MasterGetDefaultConfig(&cfg);
    cfg.baudRate_Bps=ACCEL_SPI_BAUDRATE; cfg.sselNum=ACCEL_SPI_SSEL;
    st=SPI_MasterInit(UWB_SPI_BASEADDR,&cfg,CLOCK_GetFreq(kCLOCK_Fro32M));
    if (st==kStatus_Success) {
        tx[0]=(write?0x00:0x80)|reg;
        if(write) memcpy(tx+2,bytes,n);
        x.txData=tx; x.rxData=rx; x.dataSize=n+2; x.configFlags=kSPI_FrameAssert;
        st=SPI_MasterTransferBlocking(UWB_SPI_BASEADDR,&x);
    }
    /* Restore both register settings AND fsl_spi's software SSEL state. */
    status_t restore=SPI_MasterInit(UWB_SPI_BASEADDR,Mesh_GetUwbSpiConfig(),CLOCK_GetFreq(kCLOCK_Fro32M));
    phOsalUwb_UnlockMutex(gMeshTmlMutex);
    if(st!=kStatus_Success || restore!=kStatus_Success) return false;
    if(!write) memcpy(bytes,rx+2,n);
    return true;
}
static bool setreg(uint8_t r,uint8_t v){return transfer(r,&v,1,true);}
bool mesh_accel_init(uint8_t *who)
{
    uint8_t c=0; *who=0;
    if(!transfer(FXLS8962_WHO_AM_I,who,1,false))return false;
    if(*who!=FXLS8962AF_WHOAMI_VALUE && *who!=FXLS8964AF_WHOAMI_VALUE && *who!=FXLS8967AF_WHOAMI_VALUE)return false;
    /* Continuous 100 Hz, normal 12-bit little endian, +/-4 g, no auto sleep/IRQ. */
    if(!setreg(FXLS8962_SENS_CONFIG1,0x02) || !setreg(FXLS8962_SENS_CONFIG2,0x00) ||
       !setreg(FXLS8962_SENS_CONFIG3,0x55) || !setreg(FXLS8962_SENS_CONFIG4,0x00) ||
       !setreg(FXLS8962_INT_EN,0x00) || !setreg(FXLS8962_SDCD_CONFIG2,0x00) ||
       !setreg(FXLS8962_SENS_CONFIG1,0x03))return false;
    return transfer(FXLS8962_SENS_CONFIG1,&c,1,false) && (c&7)==3;
}
bool mesh_accel_sample(int16_t mg[3])
{
    uint8_t status=0,b[6];
    if(!transfer(FXLS8962_INT_STATUS,&status,1,false) || !(status&0x80))return false;
    if(!transfer(FXLS8962_OUT_X_LSB,b,6,false))return false;
    for(unsigned i=0;i<3;i++) {
        int32_t v=(b[i*2]|((uint16_t)(b[i*2+1]&15)<<8));
        if(v&0x800)v-=4096;
        int32_t n=v*1000; mg[i]=(int16_t)((n+(n>=0?256:-256))/512);
    }
    return true;
}
