#!/usr/bin/env python3
"""Actual Runtime provider graph and exact Utilities alarm facade coexistence."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PIN='474ab9d9cae1bea25d315f76fbe8eec61f9dba7b'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--utilities',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--sanitized',action='store_true');a=p.parse_args()
a.utilities=a.utilities.resolve();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
assert subprocess.check_output(['git','-C',str(a.utilities),'rev-parse','HEAD'],text=True).strip()==PIN
commands=[];runs=[]
inc=['-I'+str(path) for path in (ROOT/'src',ROOT/'sdk/driver',ROOT/'sdk/hardware',ROOT/'test/drivers/stubs',a.utilities/'lib/Alarm/include')]
san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'] if a.sanitized else []
flags=['-Wall','-Wextra','-Werror','-g1',*san,*inc]
def run(cmd):commands.append(list(map(str,cmd)));subprocess.run(cmd,check=True)
def shared(path,source,defines):
 run(['cc','-std=c11',*flags,'-fPIC','-fvisibility=hidden','-shared',*defines,str(source),'-o',str(path)])
owner=ROOT/'test/drivers/home_alarm_owner_fixture.c'
facade=a.utilities/'Services/alarm_facade/facade.c'
for version in (1,2):
 for suffix,defs in (('',[]),('-alternate',['-DOWNER_ID="alarm-alternate"']),('-malformed',['-DOWNER_MALFORMED'])):
  shared(a.output/f'owner-{version}{suffix}.so',owner,[f'-DOWNER_API={version}',*defs])
 shared(a.output/f'facade-{version}.so',facade,['-DALARM_FACADE_LEGACY_CLOCK_CHANGE' if version==1 else '-DALARM_FACADE_TAGGED_OWNER'])
binary=a.output/'test'
run(['c++','-std=c++17',*flags,'-rdynamic','-no-pie',
 str(ROOT/'src/runtime/drivers/ProviderGraphV2.cpp'),str(ROOT/'src/runtime/drivers/ProviderModuleV2.cpp'),
 str(ROOT/'test/drivers/home_alarm_facade_graph_test.cpp'),'-ldl','-o',str(binary)])
for version in (1,2):
 for mode in ('normal','missing','wrong-api','duplicate','malformed','retained'):
  source=a.output/f'owner-{version}{"-malformed" if mode=="malformed" else ""}.so'
  cmd=[str(binary),mode,str(version),str(source),str(a.output/f'facade-{version}.so'),str(a.output/f'owner-{version}-alternate.so')]
  result=subprocess.run(cmd,capture_output=True,text=True,timeout=60,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
  log=f'{version}-{mode}.log';(a.output/log).write_text(result.stdout+result.stderr);print(result.stdout+result.stderr,end='',flush=True)
  runs.append(dict(owner_api=version,mode=mode,exit_code=result.returncode,log=log))
sources=[owner,ROOT/'test/drivers/home_alarm_facade_graph_test.cpp',Path(__file__).resolve(),facade,
 *[path for path in (a.utilities/'lib/Alarm/include').glob('*.h')],
 *[path for path in (ROOT/'src/runtime/drivers').glob('*') if path.is_file()],
 *[path for path in (ROOT/'sdk/driver').glob('*.h')]]
(a.output/'receipt.json').write_text(json.dumps(dict(utilities_commit=PIN,sanitized=a.sanitized,hardware_tested=False,
 commands=commands,runs=runs,source_sha256={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}),indent=2)+'\n')
if any(x['exit_code'] for x in runs):raise SystemExit(1)
