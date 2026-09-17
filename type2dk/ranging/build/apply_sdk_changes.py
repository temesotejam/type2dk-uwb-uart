#!/usr/bin/env python3
"""Run once on an unmodified UWBIOT_SR040_v04.03.14_MCUx/uwbiot-top."""
import argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('sdk');a=p.parse_args();s=Path(a.sdk)
def replace(rel,old,new,count=1):
 f=s/rel;t=f.read_text()
 if new in t:return
 if t.count(old)!=count:raise RuntimeError('Unexpected SDK contents: '+rel)
 f.write_text(t.replace(old,new))
# Expose the existing complete-transaction TML mutex for sensor bus arbitration.
rel='libs/halimpl/transport/SPI/SR040/uwb_uwbs_tml_interface.c'
replace(rel,'static UWBStatus_t spi_assert_cs','void *gMeshTmlMutex;\n\nstatic UWBStatus_t spi_assert_cs')
replace(rel,'    status = (UWBStatus_t)phOsalUwb_DeleteMutex(&(pCtx->mSyncMutex));','    gMeshTmlMutex = NULL;\n    status = (UWBStatus_t)phOsalUwb_DeleteMutex(&(pCtx->mSyncMutex));')
replace(rel,'    status = (UWBStatus_t)phOsalUwb_CreateMutex(&(pCtx->mSyncMutex));','    status = (UWBStatus_t)phOsalUwb_CreateMutex(&(pCtx->mSyncMutex));\n    gMeshTmlMutex = pCtx->mSyncMutex;')
rel='boards/FinderV3_SPI/uwb_bus_interface.c'
replace(rel,'uwb_bus_status_t uwb_bus_init(','''static spi_master_config_t gMeshUwbSpiConfig;
const spi_master_config_t *Mesh_GetUwbSpiConfig(void) { return &gMeshUwbSpiConfig; }

uwb_bus_status_t uwb_bus_init(''')
replace(rel,'    // SPI_MasterGetDefaultConfig(&masterConfig);','    gMeshUwbSpiConfig = masterConfig;\n    // SPI_MasterGetDefaultConfig(&masterConfig);')
# Leave SRAM1 headroom for the larger app task and the SDK UWB tasks/queues.
replace('boards/Host/FinderV3/FreeRTOSConfig.h',
        '#define configTOTAL_HEAP_SIZE            ((size_t)(30 * 1024))',
        '#define configTOTAL_HEAP_SIZE            ((size_t)(48 * 1024))')
print('Mesh SDK integration applied')
