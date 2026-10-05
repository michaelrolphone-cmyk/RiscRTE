#!/usr/bin/env python3
"""Stage a separate owner-maintenance executable; never deploy or flash it."""
import argparse,json,hashlib
from pathlib import Path
from release_assets import ROOT,file_bytes,esp_image,require,head
from check_versions import firmware

def stage(source):
    require(source==head(),'maintenance source mismatch')
    image=file_bytes(ROOT/'.pio/build/esp32s3-16mb-maintenance/firmware.bin');esp_image(image)
    require(image[3]>>4==4,'maintenance flash declaration')
    version=firmware((ROOT/'platformio.ini').read_text())
    for marker in (b'RISC_OWNER_INSTALLER:1\0',b'RTE_OWNER_MAINTENANCE=1\0',('RTE_SOURCE='+source).encode()+b'\0',('RISC_RUNTIME_VERSION:'+version).encode()+b'\0'):
        require(marker in image,'maintenance identity missing')
    require(b'RISC_PAIRED_STORE_ABI:1\0' not in image,'maintenance must not be an ordinary paired OTA image')
    require(len(image)<=0x300000,'maintenance application bound')
    ordinary=file_bytes(ROOT/'.pio/build/esp32s3-16mb-paired/firmware.bin')
    require(b'RTE_OWNER_MAINTENANCE=1\0' not in ordinary and b'RTE_MAINTENANCE_V1' not in ordinary,'normal Runtime unexpectedly exposes maintenance endpoint')
    out=ROOT/'dist/owner-maintenance';out.mkdir(exist_ok=False)
    (out/'firmware.bin').write_bytes(image)
    (out/'candidate.json').write_text(json.dumps({'schema':1,'source_sha':source,'version':version,'target':'esp32s3-16mb-maintenance','bytes':len(image),'sha256':hashlib.sha256(image).hexdigest(),'scope':'Explicit owner maintenance executable only. No paired OTA ABI marker. No device execution, partition image, erase, flash or automatic reset. Hardware checks UNRUN.'},indent=2)+'\n')
    print('Verified separate maintenance image and normal endpoint exclusion; hardware UNRUN.')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source-sha',required=True);stage(p.parse_args().source_sha)
