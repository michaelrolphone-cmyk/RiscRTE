#!/usr/bin/env python3
"""Actual Runtime resident observation without lifecycle/capture revocation."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--sanitize',action='store_true');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
flags=['-O0','-g0','-pipe','-Wall','-Wextra','-Werror'];san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'] if a.sanitize else [];incs=['-I'+str(R/x) for x in ['src','sdk/app','sdk/driver','sdk/hardware','lib/ArduinoJson/src','test/drivers/stubs','test']];commands=[]
def run(c,name):
 commands.append(c);(out/'commands.json').write_text(json.dumps(commands,indent=2));r=subprocess.run(c,text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'});(out/(name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();return r.stdout
files=[p for part in ['src','sdk','test/resident_attention','lib/ArduinoJson/src','test/drivers/stubs'] for p in (R/part).rglob('*') if p.is_file()]+[R/'test/RuntimeFileFixture.h',Path(__file__).resolve()]
initial={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
for role in (0,1):run(['cc','-std=c11',*flags,*san,*incs,'-fPIC','-fvisibility=hidden','-shared','-DATTENTION_ROLE='+str(role),str(R/'test/resident_attention/app.c'),'-o',str(out/('role'+str(role)+'.elf'))],'build-app'+str(role))
units=[R/'src'/f for f in ['bootstrap/Json.cpp','bootstrap/Board.cpp','bootstrap/Runtime.cpp','runtime/streams/AppStreamSessions.cpp','runtime/streams/ProviderQueueHost.cpp','runtime/drivers/ProviderGraphV2.cpp','runtime/drivers/ProviderModuleV2.cpp']]+[R/'test/resident_attention/runtime_test.cpp']
run(['c++','-std=c++17',*flags,*san,'-Wno-missing-field-initializers','-rdynamic','-fno-pie','-no-pie',*incs,*map(str,units),'-ldl','-o',str(out/'test')],'build-runtime')
modes=['no-safety-hook','unsafe-pre','unsafe-post','unsafe-pre-owner','unsafe-post-owner','diagnostic','happy','none','busy','old-host','no-clock','bad-flags','bad-supported','bad-size','bad-time','bad-status','bad-retained','bad-busy','forbidden-yield','forbidden-acquire','forbidden-release','forbidden-launch','forbidden-clock','forbidden-checkpoint','forbidden-register','forbidden-recursive','forbidden-storage','owner-loss','reject-ok','reject-policy','reject-busy','bad-reject-status','forbidden-reject-yield','forbidden-reject-recursive']
results=[]
for mode in modes:
 d=out/mode;d.mkdir(exist_ok=True)
 for role,name in [(0,'host'),(1,'child')]:
  f=d/(name+'.elf')
  if f.exists():f.unlink()
  os.link(out/('role'+str(role)+'.elf'),f)
 results.append(run([str(out/'test'),str(d),mode],mode).strip())
assert initial=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},"source changed during qualification"
(out/'receipt.json').write_text(json.dumps({'results':results,'sanitize':a.sanitize,'files':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},'scope':'Actual Runtime/DSO lifecycle; doubled native capture exit gate, KV backend and observation endpoint. No product/device proof.'},indent=2)+'\n');print(len(results),'resident attention cases PASS')
