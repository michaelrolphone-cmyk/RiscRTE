#!/usr/bin/env python3
"""Actual merged Runtime observation with simultaneous serial-transfer intent."""
import argparse,hashlib,json,os,subprocess,shlex
from pathlib import Path
R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--sanitize',action='store_true');p.add_argument('--cxx',default='c++');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
flags=['-O0','-g0','-pipe','-Wall','-Wextra','-Werror'];san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'] if a.sanitize else [];incs=['-I'+str(R/x) for x in ['src','sdk/app','sdk/driver','sdk/hardware','lib/ArduinoJson/src','test/drivers/stubs','test']];commands=[]
def run(c,name):
 commands.append(c);(out/'commands.json').write_text(json.dumps(commands,indent=2));r=subprocess.run(c,text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'});(out/(name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();return r.stdout
files=[p for part in ['src','sdk','test/resident_attention','test/resident_attention_transfer','lib','test/drivers/stubs'] for p in (R/part).rglob('*') if p.is_file()]+[R/'test/RuntimeFileFixture.h',Path(__file__).resolve()]
initial={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
for role in (0,1):run(['cc','-std=c11',*flags,*san,*incs,'-fPIC','-fvisibility=hidden','-shared','-DATTENTION_ROLE='+str(role),str(R/'test/resident_attention/app.c'),'-o',str(out/('role'+str(role)+'.elf'))],'build-app'+str(role))
units=[R/'src'/f for f in ['bootstrap/Json.cpp','bootstrap/Board.cpp','bootstrap/Runtime.cpp','runtime/streams/AppStreamSessions.cpp','runtime/streams/ProviderQueueHost.cpp','runtime/drivers/ProviderGraphV2.cpp','runtime/drivers/ProviderModuleV2.cpp']]+[R/'test/resident_attention_transfer/runtime_test.cpp']
dependencies={}
for index,unit in enumerate([R/'test/resident_attention/app.c',*units]):
 compiler='cc' if unit.suffix=='.c' else a.cxx
 standard='-std=c11' if unit.suffix=='.c' else '-std=c++17'
 raw=run([compiler,standard,'-DRISC_NATIVE_SERIAL_FILES=1','-DRISC_NATIVE_APP_MEMORY_TEST',*incs,'-MM',str(unit)],'dependencies-'+str(index))
 for item in shlex.split(raw.replace('\\\n',' ').split(':',1)[1]):
  dep=Path(item).resolve();digest=hashlib.sha256(dep.read_bytes()).hexdigest()
  assert initial.get(str(dep))==digest, 'compiler input outside frozen source snapshot: '+str(dep)
  dependencies[str(dep)]=digest
run([a.cxx,'-DRISC_NATIVE_SERIAL_FILES=1','-DRISC_NATIVE_APP_MEMORY_TEST','-std=c++17',*flags,*san,'-Wno-missing-field-initializers','-rdynamic','-fno-pie','-no-pie',*incs,*map(str,units),'-ldl','-o',str(out/'test')],'build-runtime')
modes=['quiet','pending','reject-ok','reject-busy','forbidden-yield','forbidden-service','forbidden-exit','forbidden-launch','forbidden-reject-yield','forbidden-reject-service','forbidden-reject-exit','forbidden-reject-launch']
results=[]
for mode in modes:
 d=out/mode;d.mkdir(exist_ok=True)
 for role,name in [(0,'host'),(1,'child')]:
  f=d/(name+'.elf')
  if f.exists():f.unlink()
  os.link(out/('role'+str(role)+'.elf'),f)
 results.append(run([str(out/'test'),str(d),mode],mode).strip())
assert initial=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},"source changed during qualification"
(out/'receipt.json').write_text(json.dumps({'results':results,'sanitize':a.sanitize,'compiler_inputs':dependencies,'binaries':{str(out/n):hashlib.sha256((out/n).read_bytes()).hexdigest() for n in ['test','role0.elf','role1.elf']},'files':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},'scope':'Actual merged Runtime with RISC_NATIVE_SERIAL_FILES=1; doubled Port serial/capture/KV endpoints. Native memory selection hooks are also doubles. No physical USB/native protocol/input-focus proof.'},indent=2)+'\n');print(len(results),'resident attention cases PASS')
