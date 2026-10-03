#!/usr/bin/env python3
"""Reader-derived S3 PIC build flags; no hardware operations."""
import os, pathlib, subprocess, argparse
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--cc',default=os.getenv('NATIVE_APP_CC',str(pathlib.Path.home()/'.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc')));a=p.parse_args()
out=root/'build/elf';out.mkdir(parents=True,exist_ok=True)
for source,name in [('apps/heartbeat/main.c','default.elf'),('test/fixtures/default.c','handoff.elf'),('test/fixtures/child.c','child.elf'),('test/fixtures/provider.c','probe.elf')]:
    if not (root/source).exists():continue
    subprocess.run([a.cc,'-std=c11','-Os','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-nostdlib','-nostartfiles','-shared','-Wl,--hash-style=sysv','-I'+str(root/'sdk/app'),'-I'+str(root/'sdk/driver'),'-I'+str(root/'sdk/hardware'),str(root/source),'-o',str(out/name)],check=True)
    readelf=a.cc.replace('gcc','readelf');info=subprocess.check_output([readelf,'-h',str(out/name)],text=True)
    assert 'DYN (Shared object file)' in info and 'Xtensa' in info
    symbols=subprocess.check_output([readelf,'--dyn-syms','--wide',str(out/name)],text=True)
    entry='t5_driver_get' if name=='probe.elf' else 'app_main'
    assert any('FUNC' in row and 'GLOBAL' in row and 'UND' not in row and row.split()[-1]==entry for row in symbols.splitlines())
    allowed={'risc_runtime_get_api','snprintf','memset','strcmp'}
    imports={row.split()[-1] for row in symbols.splitlines() if 'GLOBAL' in row and 'UND' in row}
    assert imports<=allowed,imports
    print(name,'Xtensa ET_DYN entry/import checks passed')

import shutil
store=root/'build/store';store.mkdir(exist_ok=True)
for item in (root/'data').iterdir():
    if item.is_file():shutil.copy2(item,store/item.name)
shutil.copy2(out/'default.elf',store/'default.elf')
print('Staged boot.json, board.json and default.elf in build/store (no device writes)')
