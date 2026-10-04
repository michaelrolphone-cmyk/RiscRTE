#!/usr/bin/env python3
"""Stage a checked generic native-USB runtime candidate; never flash or publish."""
import argparse, hashlib, json, re, shutil, subprocess
from pathlib import Path
from release_assets import ROOT, file_bytes, esp_image, elf, partition_table, require, head
from check_versions import firmware
TARGET='esp32s3-16mb-usb'
def stage(source):
    require(re.fullmatch('[0-9a-f]{40}',source) and source==head(),'source SHA differs from checkout')
    require(subprocess.run(['git','diff','--quiet','HEAD'],cwd=ROOT).returncode==0,'dirty candidate sources')
    require(not subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=ROOT).strip(),'untracked candidate inputs')
    build=ROOT/'.pio/build'/TARGET
    output=ROOT/'dist'/TARGET
    if output.exists():shutil.rmtree(output)
    output.mkdir(parents=True)
    blobs={name:file_bytes(build/name) for name in ('firmware.bin','firmware.elf','bootloader.bin','partitions.bin')}
    for name in ('firmware.bin','bootloader.bin'):
        esp_image(blobs[name])
        require(blobs[name][3]>>4==4,'candidate image must declare 16 MiB flash')
    elf(blobs['firmware.elf']);partition_table(blobs['partitions.bin'])
    require(TARGET.encode()+b'\0' in blobs['firmware.bin'],'wrong compiled target profile')
    marker=('RTE_SOURCE='+source).encode()+b'\0'
    require(all(marker in blobs[name] for name in ('firmware.bin','firmware.elf')),'compiled source identity mismatch')
    require(len(blobs['firmware.bin'])<=0x300000 and len(blobs['bootloader.bin'])<=0x8000,'candidate exceeds partition')
    for name,data in blobs.items():(output/name).write_bytes(data)
    for name in ('platformio.ini','partitions.csv','requirements-ci.txt'):
        (output/name).write_bytes(file_bytes(ROOT/name))
    files={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(output.iterdir())}
    record={'schema':1,'target':TARGET,'source_sha':source,'firmware_version':firmware((ROOT/'platformio.ini').read_text()),
      'flash_bytes':0x1000000,'layout_used_bytes':0x800000,'app_offset':0x10000,'bootfs_offset':0x310000,'bootfs_bytes':0x4f0000,
      'usb_cdc_on_boot':True,'flash_mode':'qio','flash_frequency_hz':80000000,'psram':'opi','assets':files,
      'scope':'Generic runtime only. Product bootfs and physical qualification are separate. No device operations performed.'}
    (output/'candidate.json').write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
    (output/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in sorted(output.iterdir()) if p.name!='SHA256SUMS'))
    print('Verified generic native-USB candidate:',source,output)
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--source-sha',required=True)
    stage(parser.parse_args().source_sha)
