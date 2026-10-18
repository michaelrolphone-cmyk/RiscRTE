#!/usr/bin/env python3
"""New candidate qualification; no historical receipts are inputs."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--provider',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[1];provider=a.provider.resolve();base=a.baseline.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
pin='e1b2fc8b801b33e3bd6ea30626a8dda9bf2237a6';assert subprocess.check_output(['git','-C',str(provider),'rev-parse','HEAD'],text=True).strip()==pin
assert not subprocess.check_output(['git','-C',str(provider),'status','--porcelain'],text=True)
assert (root/'sdk/app/RiscStorageVolumeV1.h').read_bytes()==(provider/'include/RiscStorageVolumeV1.h').read_bytes()
sdk=out/'sdk';sdk.mkdir();headers={}
# Same flattened-header source order as the published Runtime's provider fixture.
for folder in (root/'test/fixtures/watch/sdk/driver',provider/'include',root/'sdk/driver',root/'sdk/app',root/'sdk/hardware'):
 for source in sorted(folder.glob('*.h')):
  (sdk/source.name).write_bytes(source.read_bytes());headers[source.name]={'source':str(source),'sha256':hashlib.sha256(source.read_bytes()).hexdigest()}
flags=['-O0','-g','-Wall','-Wextra','-Werror','-Wno-overflow','-Wno-missing-field-initializers','-Wno-misleading-indentation','-fno-omit-frame-pointer']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie']
commands=[];env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0:detect_stack_use_after_return=1'
def run(args):
 args=list(map(str,args));commands.append(args);r=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env);print(r.stdout,end='');(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');
 if r.returncode:raise RuntimeError((args,r.returncode,r.stdout))
 return r.stdout
cinc=['-I'+str(sdk),'-I'+str(provider/'tests'),'-I'+str(provider/'include'),'-I'+str(provider/'lib/storage_fatfs/fatfs')]
objects=[]
for name,src in [('provider',root/'test/stat_observation_provider.c'),('ff',provider/'lib/storage_fatfs/fatfs/ff.c'),('ffunicode',provider/'lib/storage_fatfs/fatfs/ffunicode.c')]:
 obj=out/(name+'.o');run(['cc','-std=c11',*flags,*cinc,'-DWAVE_STAT_TEST_SOURCE="'+str(provider/'tests/test_storage_async.c')+'"','-c',src,'-o',obj]);objects.append(obj)
for name,tree in [('baseline',base),('candidate',root)]:
 run(['c++','-std=c++17',*flags,'-no-pie',*(['-DBASELINE'] if name=='baseline' else []),'-I'+str(tree/'src'),'-I'+str(sdk),tree/'src/runtime/serial/Files.cpp',root/'test/stat_observation_provider_test.cpp',*objects,'-o',out/name])
cases=['unbound','active-session','sleep-prepared','io-unavailable','owner-refused','provider-retained','healthy','healthy-missing','post-unavailable','post-failed-unavailable']
# client-retained cannot reach an accepted STAT through the endpoint; it is
# covered at RuntimeFiles entry in the focused unit test instead.
for case in cases:
 results=[run([out/name,case,mode]) for name,mode in [('baseline','off'),('candidate','off'),('candidate','on')]]
 assert results[0]==results[1]==results[2],case
 (out/(case+'.log')).write_text(results[0])
manifest={'base_revision':subprocess.check_output(['git','-C',str(base),'rev-parse','HEAD'],text=True).strip(),'provider_revision':pin,'sanitized':a.sanitize,'cases':cases,'headers':headers,'commands':commands,'files':{}}
for tree,folders in [(root,['src','sdk','test/stat_observation_provider.c','test/stat_observation_provider_test.cpp','test/run_stat_observation_provider_test.py']),(provider,['drivers/storage_sdmmc','lib/storage_fatfs','tests/test_storage_async.c','include'])]:
 for folder in folders:
  path=tree/folder
  for f in ([path] if path.is_file() else path.rglob('*')):
   if f.is_file():manifest['files'][str(f)]=hashlib.sha256(f.read_bytes()).hexdigest()
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('PASS: 10 real-provider cases, baseline/capture-off/capture-on wire and call-count parity; copied replay after disconnect; cached duplicate no observation')
