"""Rebuild with Zig 0.16.0 and the user-provided SR040 SDK (no download needed).
python type2dk/build.py --zig /path/to/zig --sdk-root /path/to/uwbiot-top
"""
import argparse, pathlib, subprocess, struct, binascii, hashlib
p=argparse.ArgumentParser()
p.add_argument('--zig',default='zig'); p.add_argument('--sdk-root',type=pathlib.Path,required=True)
p.add_argument('--uart',action='store_true',help='build standalone PIO13 UART TX test')
a=p.parse_args(); root=pathlib.Path(__file__).resolve().parent
qn=a.sdk_root.resolve()/'ext/boards/qn9090'
name='2dk_uart_tx_v1'
elf=root/(name+'.elf'); raw=root/(name+'.raw')
subprocess.run([a.zig,'cc','-target','thumb-freestanding-eabi','-mcpu=cortex_m4',
 '-Oz','-mfloat-abi=soft','-Wall','-Wextra','-Werror','-ffreestanding','-fno-builtin','-fno-stack-protector','-nostdlib',
 '-DCPU_QN9090HN','-I'+str(qn/'devices/QN9090'),'-I'+str(qn/'CMSIS/Include'),
 '-I'+str(root.parent/'include'),str(root/'src/uart_tx.c'),str(root/'src/startup.c'),'-Wl,-T,'+str(root/'src/link.ld'),
 '-Wl,-e,Reset_Handler','-o',str(elf)],check=True)
subprocess.run([a.zig,'objcopy','-O','binary',str(elf),str(raw)],check=True)
b=bytearray(raw.read_bytes()); b.extend(b'\xff'*((-len(b))%4)); off=len(b)
assert off < 64*1024-32
# QN9090 legacy image format, matching SDK scripts/dk6_image_tool.py.
# Stated application size matches the supplied 2dk_controller/controlee images.
b.extend(struct.pack('<8I',0xBB0110BB,0,0,off+32,0x90000,0,0,0))
struct.pack_into('<I',b,28,(-sum(struct.unpack_from('<7I',b))) & 0xffffffff)
struct.pack_into('<II',b,32,0x98447902,off)
struct.pack_into('<I',b,40,binascii.crc32(b[:40]) & 0xffffffff)
assert sum(struct.unpack_from('<8I',b)) & 0xffffffff == 0
assert struct.unpack_from('<I',b,40)[0] == binascii.crc32(b[:40]) & 0xffffffff
sp,reset=struct.unpack_from('<II',b)
assert 0x04000400 < sp <= 0x04008000
assert reset & 1 and 0x120 <= (reset & ~1) < off
irq=struct.unpack_from('<I',b,30*4)[0]
assert irq & 1 and 0x120 <= (irq & ~1) < off
out=root/(name+'.bin'); out.write_bytes(b); raw.unlink()
(root/'UART_SHA256SUMS.txt').write_text(hashlib.sha256(b).hexdigest()+'  '+out.name+'\n')
print('Build and ROM-header checks passed:',len(b),'bytes')
