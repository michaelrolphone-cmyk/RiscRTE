#!/usr/bin/env python3
"""Production Runtime + native serial launch regression; synthetic provider, no device I/O."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--baseline',action='store_true')
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
flags=['-g','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-fno-omit-frame-pointer']
sanitized=os.environ.get('SANITIZE')=='1'
if sanitized:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
includes=['-I'+str(root/x) for x in ('','src','sdk/app','sdk/driver','sdk/hardware','lib/ArduinoJson/src','test/drivers/stubs','test/native_sleep_diagnostics_idf6_shim','test/native_platform_shim')]
sources=('src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp','src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp','src/runtime/serial/Files.cpp','src/ports/esp32s3/NativeSerialFiles.cpp','test/native_serial_launch_test.cpp')
records={str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for folder in ('src','sdk','lib/ArduinoJson/src','test/drivers/stubs','test/native_sleep_diagnostics_idf6_shim','test/native_platform_shim') for f in (root/folder).rglob('*') if f.is_file() and f.suffix in ('.h','.hpp','.c','.cpp','.inc')}
for name in ('test/native_serial_launch_test.cpp','test/run_native_serial_launch_test.py','test/RuntimeFileFixture.h','test/storage_files_adapter_test.cpp','test/fixtures/atomic_key_value_app.c'):records[name]=hashlib.sha256((root/name).read_bytes()).hexdigest()
commands=[]
def run(cmd):
    cmd=[str(x) for x in cmd];commands.append(cmd);subprocess.run(cmd,check=True)
cc,cxx=os.environ.get('CC','cc'),os.environ.get('CXX','c++')
for name,extra in (('default.elf',[]),('child.elf',['-DATOMIC_APP_CHILD=1'])):
    run([cc,'-std=c11',*flags,*includes,*extra,'-shared','-fPIC','-fvisibility=hidden',root/'test/fixtures/atomic_key_value_app.c','-o',out/name])
run([cxx,'-std=c++17',*flags,*includes,'-DRISC_NATIVE_SERIAL_FILES=1','-DRISC_NATIVE_ESP_IDF=1','-rdynamic','-no-pie','-pthread',*[root/s for s in sources],'-ldl','-o',out/'test'])
cases=('baseline-reset','path') if a.baseline else ('path','stale-ready','partial-frame','pending-request','pending-reply','partial-reply','read','write','disconnect-open','expiry','unavailable-media','settings-busy','null-hook','no-session','postcommit-arrival','return-shutdown','failed-close')
results=[]
for case in cases:
    cmd=[str(out/'test'),str(out),case];commands.append(cmd)
    result=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (out/(case+'.log')).write_text(result.stdout);print(result.stdout,end='')
    expected_failure=a.baseline and case=='path'
    if expected_failure:assert 'accepted=1 session_exit_safe=0' in result.stdout and "Assertion `!accepted' failed" in result.stdout, result.stdout
    results.append(dict(case=case,returncode=result.returncode,expected_regression_failure=expected_failure,passed=(result.returncode!=0 if expected_failure else result.returncode==0)))
for name,sha in records.items():assert hashlib.sha256((root/name).read_bytes()).hexdigest()==sha,'Input changed during test: '+name
receipt=dict(revision=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),baseline=a.baseline,toolchains={cc:subprocess.check_output([cc,'--version'],text=True).splitlines()[0] for cc in (cc,cxx)},sanitized=sanitized,sanitizer_environment={key:os.environ.get(key) for key in ('ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS')},commands=commands,results=results,runtime_sources=records,immutable_inputs_verified=True,limits='Host production Runtime/runOne and NativeSerialFiles/Files with synthetic generic storage. Settings-busy models the documented native readiness hook. No target instruction, physical timing, USB or SD qualification; leak detection may be disabled as recorded.')
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
assert all(r['passed'] for r in results),results
print('Native serial launch '+('baseline failure reproduced' if a.baseline else 'qualification PASS'))
