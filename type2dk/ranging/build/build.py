#!/usr/bin/env python3
"""Build the supplied NXP SDK without MCUXpresso IDE; no downloads or flashing."""
import argparse, concurrent.futures, json, os, re, shutil, subprocess, sys
from pathlib import Path
import xml.etree.ElementTree as ET
ap=argparse.ArgumentParser()
ap.add_argument('--sdk',required=True)
ap.add_argument('--gcc-bin',default='')
ap.add_argument('--node',type=int,choices=[19,21,22],default=19)
ap.add_argument('--out',default='build')
ap.add_argument('--stock',action='store_true')
ap.add_argument('--uart-off',action='store_true',help='node19 control build for ranging/UART comparison')
a=ap.parse_args(); sdk=Path(a.sdk).resolve(); out=Path(a.out).resolve(); out.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parent.parent
prefix=str(Path(a.gcc_bin).resolve()/'arm-none-eabi-') if a.gcc_bin else 'arm-none-eabi-'
p=sdk/'project/FinderV3'; cc=ET.parse(p/'.cproject').findall('.//cconfiguration')[0]
tool=next(t for t in cc.findall('.//tool') if t.get('name')=='MCU C Compiler')
opts={o.get('name'):o for o in tool.findall('option')}
defs=[v.get('value') for v in opts['Defined symbols (-D)']]
# CRP header and nohost glue belong to the IDE, not the hardware SDK.
defs=[v for v in defs if v not in ['__MCUXPRESSO','SERIAL_PORT_TYPE_SWO=1']]
incs=[]
for v in opts['Include paths (-I)']:
 s=v.get('value')
 d=p if '${' in s else (p/'Debug'/s).resolve()
 if d.is_dir() and str(d) not in incs:incs.append(str(d))
ex=cc.find('.//sourceEntries/entry').get('excluding').split('|')
sources={}
for l in ET.parse(p/'.project').findall('.//link'):
 uri=l.findtext('locationURI'); name=l.findtext('name')
 if not uri.startswith('PARENT-2-PROJECT_LOC/'):continue
 path=sdk/uri.split('/',1)[1]
 for f in ([path] if path.is_file() else path.rglob('*')):
  if not f.is_file() or f.suffix not in ['.c','.S']:continue
  rel=name if path.is_file() else name+'/'+str(f.relative_to(path))
  if any(rel==x or rel.startswith(x+'/') for x in ex):continue
  sources[str(f)]=rel
if not a.stock:
 sources={f:r for f,r in sources.items() if 'demo_tracker_sr040.c' not in f}
 for f in (root/'source').glob('*.c'):sources[str(f)]=f'mesh/{f.name}'
 incs.insert(0,str(root/'source'))
 incs.insert(0,str(root.parents[1]/'include'))
 defs.append('MESH_UART_ENABLE='+str(int(a.node==19 and not a.uart_off)))
 defs.append('MESH_NODE='+str(a.node))
flags=['-mcpu=cortex-m4','-mthumb','-mfloat-abi=soft','-Os','-g3','-std=gnu99','-ffunction-sections','-fdata-sections','-ffreestanding','-fno-builtin','-fno-common','--specs=nano.specs','-Wall','-Wno-unused-parameter','-Wno-missing-field-initializers','-Werror=implicit-function-declaration','-include',str(sdk/'boards/Host/FinderV3/app_preinclude.h')]+['-D'+s for s in defs]+['-I'+s for s in incs]
records=[]
def compile_one(item):
 f,r=item; obj=out/'obj'/(r+'.o'); obj.parent.mkdir(parents=True,exist_ok=True)
 cmd=[prefix+'gcc']+flags+['-c',f,'-o',str(obj)]
 # Recompile each time to avoid stale config/headers.
 p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 return (p.returncode,p.stdout,str(obj),cmd)
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
 results=list(pool.map(compile_one,sorted(sources.items())))
(out/'compile.log').write_text(''.join(r[1] for r in results))
(out/'compile_commands.json').write_text(json.dumps([{'directory':str(out),'arguments':r[3],'file':r[3][-3]} for r in results],indent=2))
failed=[r for r in results if r[0]]
print(f'{len(results)} translation units; {len(failed)} compile failures')
if failed:
 print('\n'.join(r[1] for r in failed[:8]));sys.exit(1)
ld=(sdk/'boards/FinderV3_SPI/QN9090_UWB_TAG_FW.ld').read_text()
ld=re.sub(r'GROUP\s*\(.*?\)','',ld,flags=re.S)
(out/'firmware.ld').write_text(ld)
suffix=('no_uart' if a.uart_off else 'uart') if a.node==19 else 'peer'
elf=out/f'2dk_range_node{a.node}_{suffix}_v1.elf'
libs=[str(f) for f in (sdk/'ext/boards/qn9090/bluetooth/libs').glob('*.a')]
cmd=[prefix+'gcc','-mcpu=cortex-m4','-mthumb','-mfloat-abi=soft','-nostartfiles','--specs=nano.specs','--specs=nosys.specs','-Wl,--defsym=__heap_size__=4096','-Wl,--gc-sections','-Wl,--print-memory-usage','-Wl,-Map='+str(out/'firmware.map'),'-T',str(out/'firmware.ld')]+[r[2] for r in results]+['-Wl,--start-group']+libs+['-lc_nano','-lm','-lgcc','-lnosys','-Wl,--end-group','-o',str(elf)]
r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(out/'link.log').write_text(r.stdout);print(r.stdout)
if r.returncode:sys.exit(r.returncode)
subprocess.run([prefix+'size',str(elf)],check=True)
env=os.environ.copy();env['PATH']=str(Path(prefix).parent)+os.pathsep+env['PATH']
subprocess.run([sys.executable,str(sdk/'scripts/dk6_image_tool.py'),elf.name],check=True,env=env,cwd=out)
subprocess.run([prefix+'objcopy','-O','binary',str(elf),str(elf.with_suffix('.bin'))],check=True)
print(elf.with_suffix('.bin'))
