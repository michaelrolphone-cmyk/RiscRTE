#!/usr/bin/env python3
"""Real pinned VFS/mount geometry matrix; host images only, never device access.

The published AppData image may be supplied for the 1 MiB product. Default and
Badge layouts run the complete owning-mount suite, including custody, retained
faults, backend IO, recovery and package cuts. Additional sizes prove that the
fix is product-derived, rather than a special case for 128 or 256 blocks.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path)
parser.add_argument('--sanitize',action='store_true')
parser.add_argument('--appdata-image',type=Path)
args=parser.parse_args()
out=args.output or Path(tempfile.mkdtemp(prefix='appdata-geometry-'))
out=out.resolve();out.mkdir(parents=True,exist_ok=True)
image=args.appdata_image.resolve() if args.appdata_image else None
if image and image.stat().st_size!=0x100000:parser.error('AppData image must be exactly 1 MiB')
base=os.environ.copy()
for key in ('APPDATA_IMAGE','APPDATA_CASES','APPDATA_OFFSET','APPDATA_BYTES','FLASH_BYTES','SANITIZE','BUILD_DIR'):
 base.pop(key,None)
base.update(ESP_LITTLEFS_SOURCE=str(root/'experimental/native_replica/pinned_littlefs/vendor'),
 LITTLEFS_SOURCE=str(root/'experimental/native_replica/pinned_littlefs/vendor/littlefs'),
 SDK6_VFS='1',RISC_LITTLEFS_NATIVE_IDF6='1',SANITIZE='1' if args.sanitize else '0',MOUNT_STAGE_LOGS='1')
# Retention intentionally keeps native objects alive through host process exit.
# Leak checking is not a valid teardown test; ASan/UBSan still check all accesses.
if args.sanitize:base['ASAN_OPTIONS']='detect_leaks=0:abort_on_error=1'
results=[]
for name,offset,size,flash,full in [('default',0x270000,0x80000,0x1000000,True),('badge',0x1f00000,0x100000,0x2000000,True),('small',0x10000,0x10000,0x400000,False),('large',0x400000,0x200000,0x800000,False)]:
 env=base.copy();env.update(APPDATA_OFFSET=str(offset),APPDATA_BYTES=str(size),FLASH_BYTES=str(flash),BUILD_DIR=str(out/name))
 if not full:env['APPDATA_CASES']='geometry,wrong-geometry'
 if name=='badge' and image:env['APPDATA_IMAGE']=str(image)
 log=out/(name+'.log')
 with log.open('w') as stream:
  result=subprocess.run([sys.executable,str(root/'test/run_native_app_data_mount_test.py')],env=env,stdout=stream,stderr=subprocess.STDOUT)
 text=log.read_text()
 if result.returncode:
  print(text[-12000:],file=sys.stderr);raise SystemExit(f'FAIL {name}: {log}')
 assert 'result=accepted' in text and 'result=rejected writes=0 erases=0' in text
 assert 'appdata failed reason=filesystem-geometry' in text
 assert 'format=disabled grow=disabled' in text
 results.append({'name':name,'offset':offset,'bytes':size,'full_owning_suite':full,'passed':True,'log':str(log),'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
 print(f'PASS {name}: {size//4096} blocks, exact mount accepted, undersized filesystem rejected without writes',flush=True)
source_files=['src/ports/esp32s3/NativeAppData.cpp','src/ports/esp32s3/NativeAppDataMount.cpp','src/ports/esp32s3/NativeAppDataLayout.h','test/native_littlefs_profile_fixture.c','test/native_app_data_mount_test.cpp','test/run_native_app_data_mount_test.py','test/run_native_app_data_geometry_test.py']
receipt={'hardware_tested':False,'sanitize':args.sanitize,'leak_detection':False,'results':results,'source_sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in source_files}}
if image:receipt['appdata_image']={'path':str(image),'sha256':hashlib.sha256(image.read_bytes()).hexdigest()}
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('PASS product geometry across actual pinned LittleFS VFS and native owning fence; no hardware claim')
