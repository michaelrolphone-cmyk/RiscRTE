#!/usr/bin/env python3
"""Source-bound actual worker, native trampoline and Runtime/ELF consumer tests."""
import argparse,hashlib,json,os,shlex,shutil,subprocess,tempfile
from functools import lru_cache
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
COMMON=['src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp','src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp','src/ports/esp32s3/CpuPort.cpp']
def check_compiler_environment():
 for name in ('CPATH','CPLUS_INCLUDE_PATH','C_INCLUDE_PATH','OBJC_INCLUDE_PATH','GCC_EXEC_PREFIX','COMPILER_PATH','DEPENDENCIES_OUTPUT','SUNPRO_DEPENDENCIES'):
  if name in os.environ:raise ValueError('Unqualified compiler environment override: '+name)
def compiler_system_roots(prefix):
 check_compiler_environment()
 return compiler_standard_paths(prefix[0],'c' if '-std=c11' in prefix else 'c++')
@lru_cache(maxsize=None)
def compiler_standard_paths(compiler,language):
 # Resolve symlinked standard headers too; GCC emits their canonical targets.
 query=subprocess.run([compiler,'-E','-x',language,'-v','-'],input='',text=True,capture_output=True,check=True)
 text=query.stderr.split('#include <...> search starts here:',1)[1].split('End of search list.',1)[0]
 roots=[Path(line.strip()).resolve() for line in text.splitlines() if line.strip()]
 if not roots or any(not p.is_dir() for p in roots):raise ValueError('Cannot identify compiler standard include directories')
 aliases={};extra=[]
 for root in roots:
  if any(root!=other and root.is_relative_to(other) for other in roots):continue
  for path in root.rglob('*'):
   if not path.is_symlink():continue
   target=path.resolve()
   if path.is_dir():extra.append(target)
   elif path.is_file():aliases[str(target)]=str(path)
 return roots+extra,aliases
def compiler_dependencies(prefix,source):
 check_compiler_environment()
 raw=subprocess.check_output([*prefix,'-M','-MT','dependencies',str(source)],text=True)
 return [Path(value).resolve() for value in shlex.split(raw.split(':',1)[1].replace('\\\n',' '))]
def dependency_key(path,root,base,owned,system_roots,system_aliases=()):
 if path.is_relative_to(root) and str(path.relative_to(root)) in owned:return 'candidate/'+str(path.relative_to(root))
 if path.is_relative_to(base):return 'runtime/'+str(path.relative_to(base))
 if str(path) in system_aliases or any(path.is_relative_to(p) for p in system_roots):return 'system/'+str(path)
 raise ValueError('Undeclared external dependency: '+str(path))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,default=ROOT);p.add_argument('--output',type=Path,required=True);p.add_argument('--sanitize',action='store_true');p.add_argument('--discover',action='store_true');a=p.parse_args();base=a.base.resolve()
 lockpath=ROOT/'qualification/resident-worker-source.json';lock=json.loads(lockpath.read_text());owned=set(lock['candidate_paths']);inputs={};system_inputs={};system_aliases={};compiler_provenance={};commands=[];results=[]
 def select(path):return ROOT/path if path in owned else base/path
 def scan(prefix,source):
  system_roots,aliases=compiler_system_roots(prefix)
  compiler=Path(shutil.which(prefix[0]) or prefix[0]).resolve()
  language='c' if '-std=c11' in prefix else 'c++'
  compiler_provenance[str(compiler)+':'+language]={'sha256':hashlib.sha256(compiler.read_bytes()).hexdigest(),'standard_roots':[str(p) for p in system_roots]}
  for path in compiler_dependencies(prefix,source):
   key=dependency_key(path,ROOT,base,owned,system_roots,aliases)
   if key.startswith('system/'):
    system_inputs[str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
    if str(path) in aliases:system_aliases[str(path)]=aliases[str(path)]
    continue

   digest=hashlib.sha256(path.read_bytes()).hexdigest()
   if not a.discover and key.startswith('runtime/') and lock['dependency_inputs'].get(key,{}).get('sha256')!=digest:raise ValueError('Unpinned runtime dependency: '+key)
   inputs[key]=digest
 with tempfile.TemporaryDirectory(prefix='resident-worker-') as tmp:
  out=Path(tmp);san=['-fsanitize=undefined','-fno-sanitize-recover=all'] if a.sanitize else []
  includes=['-I'+str(ROOT/'src'),'-I'+str(ROOT/'sdk/driver'),'-I'+str(base/'src'),'-I'+str(base/'src/ports/esp32s3'),'-I'+str(base/'sdk/app'),'-I'+str(base/'sdk/driver'),'-I'+str(base/'sdk/hardware'),'-I'+str(base/'lib/ArduinoJson/src'),'-I'+str(base/'test/drivers/stubs'),'-I'+str(base/'test')]
  cxx=[os.environ.get('CXX','c++'),'-std=c++17','-pthread','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',*san,'-rdynamic']
  cc=[os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fPIC','-fvisibility=hidden',*san,*includes]
  for name in ['resident_worker','resident_worker_app']:
   source=select('test/fixtures/'+name+'.c');scan(cc,source);cmd=[*cc,'-shared',str(source),'-o',str(out/('driver.elf' if name=='resident_worker' else 'default.elf'))];subprocess.run(cmd,check=True);commands.append(cmd)
  for name,sources in [('core',['test/resident_worker_test.cpp']),('native',['test/native_resident_worker_test.cpp']),('binding',COMMON+['test/resident_worker_binding_test.cpp']),('lifecycle',COMMON+['test/resident_worker_lifecycle_test.cpp'])]:
   prefix=[*cxx,*(['-I'+str(ROOT/'test/resident_worker_shim')] if name=='native' else []),*includes]
   paths=[select(s) for s in sources]
   for source in paths:scan(prefix,source)
   exe=out/name;cmd=[*prefix,*map(str,paths),'-ldl','-o',str(exe)];commands.append(cmd);subprocess.run(cmd,check=True)
   run=subprocess.run([str(exe),str(out)],text=True,capture_output=True);results.append(dict(name=name,returncode=run.returncode,stdout=run.stdout,stderr=run.stderr));exe.unlink();assert run.returncode==0,results[-1]
 if not a.discover and set(k for k in inputs if k.startswith('runtime/'))!=set(lock['dependency_inputs']):raise ValueError('Declared dependency set differs from actual compiled closure')
 receipt=dict(recipe={'runner_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'declaration_sha256':hashlib.sha256(lockpath.read_bytes()).hexdigest()},compiler_provenance=compiler_provenance,system_inputs=dict(sorted(system_inputs.items())),system_aliases=system_aliases,hardware_tested=False,sanitizer='undefined' if a.sanitize else 'none',inputs=dict(sorted(inputs.items())),cases=results,commands=commands)
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(receipt,indent=2)+'\n');print('4 actual source suites PASS; '+str(len(inputs))+' exact inputs; physical/target execution not inferred')
if __name__=='__main__':main()
