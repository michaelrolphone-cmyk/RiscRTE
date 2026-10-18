#!/usr/bin/env python3
"""Production Runtime + actual Settings lifecycle launch guard, no serial session/device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--baseline',action='store_true',help='Prove the regression after removing only the new launch guard in an output-local source copy')
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
sanitized=os.environ.get('SANITIZE')=='1'
flags=['-O0','-g','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-fno-omit-frame-pointer']
if sanitized:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
selected=['-DRISC_NATIVE_SERIAL_FILES=1','-DRISC_NATIVE_SETTINGS_PORT_SELECTED=1','-DRISC_NATIVE_SETTINGS_LIFECYCLE=1','-DRISC_NATIVE_SETTINGS_WORKER=1','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1','-DRISC_NATIVE_STORAGE_CUSTODY=1']
include_dirs=('','src','src/bootstrap','sdk/app','sdk/driver','sdk/hardware','lib/hal','lib/ArduinoJson/src','test/drivers/stubs','test/native_nvs_shim','test/native_sleep_diagnostics_idf6_shim','test/native_platform_shim','test/native_settings_worker_shim')
includes=['-I'+str(root/x) for x in include_dirs]
sources=['src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp','src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp','src/runtime/storage/AppDataFiles.cpp','src/runtime/serial/Files.cpp','src/ports/esp32s3/NativeSerialFiles.cpp','src/ports/esp32s3/CpuPort.cpp','test/native_settings_launch_test.cpp']
records={str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest()
    for folder in ('src','sdk','experimental/native_replica','lib/hal','lib/ArduinoJson/src','test/drivers/stubs','test/native_nvs_shim','test/native_sleep_diagnostics_idf6_shim','test/native_platform_shim','test/native_settings_worker_shim')
    for f in (root/folder).rglob('*') if f.is_file() and f.suffix in ('.h','.hpp','.c','.cpp','.inc')}
for name in ('test/native_settings_launch_test.cpp','test/run_native_settings_launch_test.py','test/RuntimeFileFixture.h','test/native_replica_checked_fixture.h','test/fixtures/atomic_key_value_app.c'):
    records[name]=hashlib.sha256((root/name).read_bytes()).hexdigest()
commands=[]
def run(cmd):
    cmd=[str(x) for x in cmd];commands.append(cmd);subprocess.run(cmd,check=True)
cc,cxx=os.environ.get('CC','cc'),os.environ.get('CXX','c++')
for name,extra in (('default.elf',[]),('child.elf',['-DATOMIC_APP_CHILD=1'])):
    run([cc,'-std=c11',*flags,*includes,*extra,'-shared','-fPIC','-fvisibility=hidden',root/'test/fixtures/atomic_key_value_app.c','-o',out/name])
run([cxx,'-std=c++17',*flags,*includes,*selected,'-DESP_PLATFORM','-c',root/'src/ports/esp32s3/NativeSettingsLifecycle.cpp','-o',out/'lifecycle.o'])
compiled_sources=[root/s for s in sources]
if a.baseline:
    original=(root/'src/bootstrap/Runtime.cpp').read_text()
    guard='  if(ok && port_.sleepServicesReady && !port_.sleepServicesReady())return false;'
    assert original.count(guard)==1
    (out/'Runtime.cpp').write_text(original.replace(guard,'  // Regression control: launch readiness guard omitted.'))
    compiled_sources[sources.index('src/bootstrap/Runtime.cpp')]=out/'Runtime.cpp'
run([cxx,'-std=c++17',*flags,*includes,*selected,'-DRISC_NATIVE_ESP_IDF=1','-rdynamic','-no-pie','-pthread',*compiled_sources,out/'lifecycle.o','-ldl','-Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename','-o',out/'test'])
(out/'board.json').write_text(json.dumps(dict(schema='riscrte.board-hardware',schema_version=1,board_id='test',revision='unspecified',buses=[],devices=[])))
(out/'app.json').write_text(json.dumps(dict(type='application',id='settings-launch-test',version='1.0.0',architecture='xtensa-esp32s3',file_name='default.elf',entry='app_main',requires=[dict(capability='storage.key-value.atomic',api=1)])))
(out/'boot.json').write_text(json.dumps(dict(board='board.json',default_app='default.elf',drivers=[],app_capabilities=[dict(manifest='app.json',grants=[dict(capability='storage.key-value.atomic',api=1,instance_id=1)])])))
command=[str(out/'test'),str(out)];commands.append(command)
result=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
(out/'settings-launch.log').write_text(result.stdout);print(result.stdout,end='')
passed=(result.returncode!=0 and 'actual_settings_continuing=1 serial_exit_safe=1 attempt=0 accepted=1' in result.stdout) if a.baseline else result.returncode==0
for name,digest in records.items():assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,'Input changed during test: '+name
receipt=dict(revision=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),baseline=a.baseline,sanitized=sanitized,sanitizer_environment={key:os.environ.get(key) for key in ('ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS')},commands=commands,returncode=result.returncode,passed=passed,runtime_sources=records,immutable_inputs_verified=True,binaries={name:hashlib.sha256((out/name).read_bytes()).hexdigest() for name in ('test','lifecycle.o','default.elf','child.elf')},limits='Host production Runtime, NativeSettingsLifecycle, NativeSerialFiles, CpuPort, Settings coordinators and checked AppData/NVS stores. Scheduler/NVS/internal-memory placement are modeled. The admitted maintenance client is deterministic Pending/Idle, not the concrete SD maintenance backend. Native serial is prepared but has no session or input frames. No target instructions, physical timing, USB, SD, or device qualification. Permanent native clients exit without artificial unregister; leak detection settings are recorded.')
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
assert passed,'Settings lifecycle launch regression failed; see settings-launch.log'
print('Actual Settings lifecycle launch '+('baseline failure reproduced' if a.baseline else 'qualification PASS'))
