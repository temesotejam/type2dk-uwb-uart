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
# Leave SRAM1 headroom for the larger app task and the SDK UWB tasks/queues.
replace('boards/Host/FinderV3/FreeRTOSConfig.h',
        '#define configTOTAL_HEAP_SIZE            ((size_t)(30 * 1024))',
        '#define configTOTAL_HEAP_SIZE            ((size_t)(48 * 1024))')
# The SDK configures the accelerometer before starting the application as well.
# Skip that call for our ranging build; --stock retains the vendor behavior.
replace('demos/common/Standalone_Main_qn9090.c',
        '    ACCEL_Configure();',
        '#if !defined(MESH_NODE)\n    ACCEL_Configure();\n#endif')
print('Ranging v2 SDK integration applied (no accelerometer bus switching)')
