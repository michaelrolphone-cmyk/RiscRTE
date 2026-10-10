#!/usr/bin/env python3
"""Stage a separate owner-maintenance executable; never deploy or flash it."""
import argparse, json, hashlib, re, shutil, subprocess
from pathlib import Path
from release_assets import ROOT, file_bytes, esp_image, require, head
from check_versions import firmware
from maintenance_plan import LAYOUTS, owner_target
from paired_candidate import partitions
from paired_bank_images import BOOTLOADER_SHA256


def stage(source,app_data=False):
    require(re.fullmatch('[0-9a-f]{40}',source) is not None and source==head(),'maintenance source mismatch')
    require(subprocess.run(['git','diff','--quiet','HEAD'],cwd=ROOT).returncode==0,'dirty maintenance sources')
    require(not subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=ROOT).strip(),'untracked maintenance inputs')
    layout='riscrte-paired-appdata-v2' if app_data else 'riscrte-paired-16m-v1'
    expected,_,target,ordinary_target=LAYOUTS[layout]
    build=ROOT/'.pio/build'/target
    image=file_bytes(build/'firmware.bin'); esp_image(image)
    require(image[3]>>4==4,'maintenance flash declaration')
    partitions(file_bytes(build/'partitions.bin'),expected)
    require(hashlib.sha256(file_bytes(build/'bootloader.bin')).hexdigest()==BOOTLOADER_SHA256,'unverified maintenance rollback bootloader')
    version=firmware((ROOT/'platformio.ini').read_text())
    for marker in (b'RISC_OWNER_INSTALLER:1\0',b'RTE_OWNER_MAINTENANCE=1\0',('RTE_SOURCE='+source).encode()+b'\0',('RISC_RUNTIME_VERSION:'+version).encode()+b'\0'):
        require(marker in image,'maintenance identity missing')
    owner_target(image,target)
    require(all(('RISC_PAIRED_STORE_ABI:'+str(abi)).encode()+b'\0' not in image for abi in (1,2)),'maintenance must not be an ordinary paired OTA image')
    require(len(image)<=expected['app0'][3],'maintenance application bound')
    ordinary_build=ROOT/'.pio/build'/ordinary_target
    ordinary=file_bytes(ordinary_build/'firmware.bin'); esp_image(ordinary)
    partitions(file_bytes(ordinary_build/'partitions.bin'),expected)
    for marker in (('RTE_SOURCE='+source).encode()+b'\0',('RISC_RUNTIME_VERSION:'+version).encode()+b'\0',ordinary_target.encode()+b'\0',('RISC_PAIRED_STORE_ABI:'+str(2 if app_data else 1)).encode()+b'\0'):
        require(marker in ordinary,'normal Runtime source/target identity missing')
    require(all(marker not in ordinary for marker in (b'RISC_OWNER_INSTALLER:1\0',b'RISC_OWNER_TARGET:',b'RTE_OWNER_MAINTENANCE=1\0',b'RTE_MAINTENANCE_V1')),'normal Runtime unexpectedly exposes maintenance endpoint')
    out=ROOT/'dist'/('owner-maintenance-appdata' if app_data else 'owner-maintenance')
    out.mkdir(parents=True,exist_ok=False)
    try:
        (out/'firmware.bin').write_bytes(image)
        (out/'candidate.json').write_text(json.dumps({'schema':1,'source_sha':source,'version':version,'target':target,'layout':layout,'store_abi':2 if app_data else 1,'bytes':len(image),'sha256':hashlib.sha256(image).hexdigest(),'ordinary_firmware_sha256':hashlib.sha256(ordinary).hexdigest(),'scope':'Explicit owner maintenance executable only. No ordinary paired OTA ABI marker. No device execution, partition image, erase, flash or automatic reset. Hardware checks UNRUN.'},indent=2)+'\n')
    except Exception:
        shutil.rmtree(out); raise
    print('Verified separate maintenance image, exact layout and normal endpoint exclusion; hardware UNRUN.')
    return out

if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--source-sha',required=True); p.add_argument('--app-data',action='store_true')
    args=p.parse_args(); stage(args.source_sha,args.app_data)
