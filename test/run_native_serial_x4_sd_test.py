#!/usr/bin/env python3
"""Pinned real X4 source/FatFs behavior. This does not reproduce or execute the retained .122 ELF."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
X4_PIN='a9009a5d774ae7060b9f298fdcbc81be6c22cc17'
STORAGE_PIN='c66da311c1348b81666d9abeb28e6b151267d4ce'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--x4',type=Path,required=True)
p.add_argument('--storage',type=Path,required=True,help='Checkout containing exact pinned lib/StorageFatFs')
p.add_argument('--receipt',type=Path)
a=p.parse_args();a.x4=a.x4.resolve();a.storage=a.storage.resolve()
records=[]
def verify(repository,pin,paths):
    names=subprocess.check_output(['git','-C',str(repository),'ls-tree','-r','--name-only',pin,'--',*paths],text=True).splitlines()
    if not names:raise SystemExit('Missing pinned source paths: '+str(paths))
    for name in names:
        raw=(repository/name).read_bytes()
        original=subprocess.check_output(['git','-C',str(repository),'show',pin+':'+name])
        if raw!=original:raise SystemExit('Pinned source differs: '+str(repository/name))
        records.append(dict(repository=str(repository),revision=pin,path=name,sha256=hashlib.sha256(raw).hexdigest()))
verify(a.x4,X4_PIN,['minimal/drivers/x4pro_sd','minimal/drivers/x4pro_board_power/PowerReadyV1.h',
                   'Drivers/x4pro_board/x4pro_pins.h','minimal/recovery-057/board.json','minimal/scripts/build_storage_fs.py'])
verify(a.storage,STORAGE_PIN,['lib/StorageFatFs'])
# This source reference predates the .123 pinned Drivers checkout, which does
# not contain lib/StorageFatFs. Neither is a retained-binary build receipt.
limits=('Actual pinned X4 driver source and shared FatFs through current Runtime native binding and wire protocol. '
        'RAM media and typed GPIO/sync/power/diagnostic/storage-worker boundaries are modeled; no legacy GPIO transport. '
        'This source composition is not proven to reproduce retained x4pro-sd 0.2.23 ELF '
        '10880847a604dc7d6b1ea5555fe7e60a47b6859d26d6ed9299c78b293e9bfd46, and target instructions are not executed. '
        'Exact retained-artifact admission, product final linking and hardware are separate evidence.')
flags=['-O0','-g','-Wall','-Wextra','-Werror','-Wno-overflow','-Wno-missing-field-initializers','-Wno-misleading-indentation','-fno-omit-frame-pointer']
if os.environ.get('SANITIZE')=='1':flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
local_records={str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest()
    for folder in ('src','sdk','lib/ArduinoJson/src','test/drivers/stubs',
                   'test/native_sleep_diagnostics_idf6_shim','test/native_platform_shim')
    for path in (ROOT/folder).rglob('*') if path.is_file() and path.suffix in ('.h','.hpp','.cpp','.c','.inc')}
for name in ('test/native_serial_x4_sd_fixture.c','test/native_serial_x4_power_fixture.c',
             'test/native_serial_x4_sd_test.cpp','test/run_native_serial_x4_sd_test.py',
             'test/RuntimeFileFixture.h','test/fixtures/atomic_key_value_app.c'):
    local_records[name]=hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
commands=[]
def run(cmd):
    cmd=[str(x) for x in cmd];commands.append(cmd);subprocess.run(cmd,check=True,timeout=240)
with tempfile.TemporaryDirectory(prefix='native-serial-x4-source-') as temp:
    build=Path(temp);sdk=build/'sdk';sdk.mkdir()
    # Current Runtime is the canonical storage ABI. Product source-relative
    # headers are used in place; no duplicate physical SDK header identities.
    for folder in ('sdk/driver','sdk/app','sdk/hardware'):
        for header in (ROOT/folder).glob('*.h'):(sdk/header.name).write_bytes(header.read_bytes())
    storage=a.storage/'lib/StorageFatFs'
    includes=['-I'+str(x) for x in (sdk,storage,ROOT,ROOT/'src',ROOT/'test',ROOT/'test/drivers/stubs',
        ROOT/'test/native_sleep_diagnostics_idf6_shim',ROOT/'test/native_platform_shim',ROOT/'lib/ArduinoJson/src')]
    manifest=json.loads((a.x4/'minimal/drivers/x4pro_sd/manifest.json').read_text())
    assert manifest['id']=='x4pro-sd'
    manifest['version']='0.2.24';manifest['requires'].append(dict(capability='platform.storage-worker',api=1))
    (build/'sd.json').write_text(json.dumps(manifest)+'\n')
    board=json.loads((a.x4/'minimal/recovery-057/board.json').read_text())
    board['buses']=[];board['devices']=[d for d in board['devices'] if d['instance_id'] in (1,9)]
    assert len(board['devices'])==2
    sd=next(d for d in board['devices'] if d['instance_id']==9)
    assert sd['config']['pins']==[5,41,42,40] and sd['bindings']['board.power.ready']==1
    (build/'board.json').write_text(json.dumps(board)+'\n')
    power=next(d for d in board['devices'] if d['instance_id']==1)
    (build/'power.json').write_text(json.dumps(dict(type='driver',id='serial-test-power',version='1.0.0',driver_abi=2,
        architecture='xtensa-esp32s3',file_name='power.elf',requires=[dict(capability='hardware.device',api=1)],
        provides=[dict(capability='board.power.ready',api=1)],hardware_compatibility=[dict(compatible=power['compatible'],
        revisions=['unspecified'],config_type=power['config_type'],config_version=1)]))+'\n')
    (build/'app.json').write_text(json.dumps(dict(type='application',id='serial-x4-test',version='1.0.0',architecture='xtensa-esp32s3',file_name='default.elf',entry='app_main',requires=[]))+'\n')
    (build/'boot.json').write_text(json.dumps(dict(board='board.json',default_app='default.elf',provider_activation='demand',
        drivers=[dict(manifest='power.json',instance_id=1),dict(manifest='sd.json',instance_id=9)],app_capabilities=[dict(manifest='app.json',grants=[])]))+'\n')
    cc,cxx=os.environ.get('CC','cc'),os.environ.get('CXX','c++')
    for source,name in ((ROOT/'test/fixtures/atomic_key_value_app.c','default.elf'),(ROOT/'test/native_serial_x4_power_fixture.c','power.elf')):
        run([cc,'-std=c11',*flags,*includes,'-shared','-fPIC','-fvisibility=hidden',source,'-o',build/name])
    run([cc,'-std=c11',*flags,*includes,'-shared','-fPIC','-fvisibility=hidden',
        '-DX4_SERIAL_DRIVER_SOURCE="'+str(a.x4/'minimal/drivers/x4pro_sd/driver.c')+'"',
        ROOT/'test/native_serial_x4_sd_fixture.c',storage/'fatfs/ff.c',storage/'fatfs/ffunicode.c','-o',build/'driver.elf'])
    sources=['src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp',
        'src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp',
        'src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp','src/runtime/serial/Files.cpp',
        'src/ports/esp32s3/NativeSerialFiles.cpp','src/ports/esp32s3/NativeSerialStorage.cpp','test/native_serial_x4_sd_test.cpp']
    run([cxx,'-std=c++17',*flags,*includes,'-DRISC_NATIVE_SERIAL_FILES=1','-DRISC_NATIVE_SERIAL_SD=1',
        '-DRISC_NATIVE_SERIAL_SD_PROVIDER="x4pro-sd"','-DRISC_NATIVE_SERIAL_SD_INSTANCE=9','-DRISC_NATIVE_ESP_IDF=1',
        '-rdynamic','-no-pie','-pthread',*[ROOT/name for name in sources],'-ldl','-o',build/'test'])
    cases=('cold','absent-card','healthy','disconnect','failed-close','failed-write','failed-release','failed-prepare')
    for case in cases:run([build/'test',build,case])
    for item in records:assert hashlib.sha256((Path(item['repository'])/item['path']).read_bytes()).hexdigest()==item['sha256']
    for name,expected in local_records.items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=expected:
            raise SystemExit('Runtime input changed during test: '+name)
    receipt=dict(x4_revision=X4_PIN,storage_revision=STORAGE_PIN,external_sources=records,commands=commands,cases=cases,
        sanitized=os.environ.get('SANITIZE')=='1',limits=limits,
        runtime_sources=local_records,
        staged_inputs={name:hashlib.sha256((build/name).read_bytes()).hexdigest() for name in ('sd.json','power.json','board.json','app.json','boot.json')})
    if a.receipt:a.receipt.write_text(json.dumps(receipt,indent=2)+'\n')
print('Pinned actual X4 source native-serial behavior PASS')
print(limits)
