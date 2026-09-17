#!/usr/bin/env python3
"""Build the UART installers and verify every distributed Type2DK image."""
import argparse
import base64
import binascii
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def checked_2dk(path, expected):
    image = base64.b64decode(''.join(path.read_text().split()), validate=True)
    assert digest(image) == expected, f'Hash mismatch: {path.name}'
    assert sum(struct.unpack_from('<8I', image)) & 0xffffffff == 0
    assert struct.unpack_from('<I', image, 32)[0] == 0x98447902
    assert struct.unpack_from('<I', image, 40)[0] == binascii.crc32(image[:40]) & 0xffffffff
    info = struct.unpack_from('<I', image, 36)[0]
    assert info + 32 == len(image)
    assert struct.unpack_from('<I', image, info)[0] == 0xBB0110BB
    assert struct.unpack_from('<I', image, info + 12)[0] == len(image)
    return image


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--sha', default='local')
    args = p.parse_args()
    site = ROOT / 'site'
    shutil.rmtree(site, ignore_errors=True)
    shutil.copytree(ROOT / 'web', site)
    output = site / 'firmware'
    output.mkdir()
    checksums = []
    packages = Path.home() / '.platformio/packages'
    for variant, version, title in [
        ('range', '1.3.0-range', 'CoreS3 Type2DK UWB Ranging'),
        ('uart', '1.2.0-uart', 'CoreS3 Type2DK UART RX Test'),
    ]:
        build = ROOT / f'.pio/build/cores3_{variant}'
        merged = output / f'cores3-{variant}-merged.bin'
        subprocess.run([
            sys.executable, str(packages / 'tool-esptoolpy/esptool.py'),
            '--chip', 'esp32s3', 'merge_bin', '-o', str(merged),
            '--flash_mode', 'dio', '--flash_freq', '80m', '--flash_size', '16MB',
            '0x0', str(build / 'bootloader.bin'), '0x8000', str(build / 'partitions.bin'),
            '0xe000', str(packages / 'framework-arduinoespressif32/tools/partitions/boot_app0.bin'),
            '0x10000', str(build / 'firmware.bin')], check=True)
        image = merged.read_bytes()
        assert image[0] == 0xe9 and 65536 < len(image) < 16 * 1024 * 1024
        version += '+' + args.sha[:7]
        manifest = {
            'name': title, 'version': version, 'new_install_prompt_erase': True,
            'new_install_improv_wait_time': 0,
            'builds': [{'chipFamily': 'ESP32-S3',
                        'parts': [{'path': 'firmware/' + merged.name, 'offset': 0}]}],
        }
        (site / f'manifest-{variant}.json').write_text(json.dumps(manifest, indent=2) + '\n')
        info = {'version': version, 'commit': args.sha, 'sha256': digest(image),
                'bytes': len(image), 'built_at': datetime.datetime.now(datetime.timezone.utc).isoformat()}
        (site / f'build-info-{variant}.json').write_text(json.dumps(info, indent=2) + '\n')
        checksums.append(f'{digest(image)}  {merged.name}')
        print('Packaged:', version, len(image), digest(image))

    for folder, sums in [(ROOT / 'type2dk', 'UART_SHA256SUMS.txt'),
                         (ROOT / 'type2dk/ranging/firmware', 'SHA256SUMS.txt')]:
        for line in (folder / sums).read_text().splitlines():
            expected, name = line.split()
            image = checked_2dk(folder / (name + '.b64'), expected)
            (output / name).write_bytes(image)
            checksums.append(line)
            print('Verified Type2DK image:', name, len(image), expected)

    shutil.copyfile(ROOT / 'type2dk/UART_README.md', site / 'UART_README.md')
    shutil.copyfile(ROOT / 'type2dk/ranging/README.md', site / 'RANGE_README.md')
    shutil.copytree(ROOT / 'validation', site / 'validation')
    shutil.copytree(ROOT / 'type2dk/ranging/licenses', site / 'licenses')
    eula = site / 'licenses/EULA.pdf.b64'
    eula.with_suffix('').write_bytes(base64.b64decode(''.join(eula.read_text().split()), validate=True))
    eula.unlink()

    # Keep the exact images from the reported hardware test independently of rebuilds.
    snapshot = json.loads((ROOT / 'firmware/known-good/snapshot.json').read_text())
    tested_zip = output / 'type2dk-uwb-uart-tested-1.3.0.zip'
    with zipfile.ZipFile(tested_zip, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, expected in snapshot['sha256'].items():
            source = ROOT / 'firmware/known-good' / name if name.startswith('cores3-') else output / name
            image = source.read_bytes()
            assert digest(image) == expected, f'Tested snapshot changed: {name}'
            archive.writestr(name, image)
        archive.writestr('snapshot.json', json.dumps(snapshot, indent=2) + '\n')
        archive.writestr('SHA256SUMS.txt', '\n'.join(f'{s}  {n}' for n, s in snapshot['sha256'].items()) + '\n')
        archive.writestr('README.md', (ROOT / 'type2dk/ranging/README.md').read_bytes())
        for license_file in sorted((site / 'licenses').iterdir()):
            if license_file.is_file():
                archive.write(license_file, 'licenses/' + license_file.name)
    checksums.append(f'{digest(tested_zip.read_bytes())}  {tested_zip.name}')
    (output / 'SHA256SUMS').write_text('\n'.join(checksums) + '\n')
    (site / 'tested-snapshot.json').write_text(json.dumps(snapshot, indent=2) + '\n')
    print('Packaged fixed hardware-tested snapshot:', tested_zip.name)


if __name__ == '__main__':
    main()
