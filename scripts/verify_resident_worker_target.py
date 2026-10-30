#!/usr/bin/env python3
"""Bind actual native worker-bearing Runtime objects, SDK and linked artifact."""
import argparse,hashlib,json,shlex,shutil,struct,subprocess
from pathlib import Path
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=a.runtime.resolve();sdk=a.sdk.resolve();build=a.build.resolve()
 commit=subprocess.check_output(['git','-C',root,'rev-parse','HEAD'],text=True).strip();sdkcommit=subprocess.check_output(['git','-C',sdk,'rev-parse','HEAD'],text=True).strip();assert sdkcommit=='fff9895c82d744c7237be8847347bdd1b07c6643'
 config=(build/'config/sdkconfig.h').read_text();assert '#define CONFIG_FREERTOS_SMP 1' not in config,'Unreviewed Amazon SMP worker-create path';assert '#define CONFIG_FREERTOS_NUMBER_OF_CORES 2' in config
 entries=json.loads((build/'compile_commands.json').read_text());wanted={'src/ports/esp32s3/CpuPort.cpp','src/ports/esp32s3/NativeHardware.cpp'};commands=[];inputs={};objects={};compiler=None
 for entry in entries:
  source=Path(entry['file']);source=(source if source.is_absolute() else Path(entry['directory'])/source).resolve()
  if not source.is_relative_to(root) or str(source.relative_to(root)) not in wanted:continue
  args=entry.get('arguments') or shlex.split(entry['command']);assert '-DRISC_NATIVE_ESP_IDF=1' in args
  cc=Path(shutil.which(args[0]) or args[0]).resolve();compiler=cc;toolroot=cc.parents[1]
  scan=[];obj=None;i=0
  while i<len(args):
   if args[i]=='-o':obj=Path(entry['directory'])/args[i+1];i+=2;continue
   if args[i]=='-c':i+=1;continue
   scan.append(args[i]);i+=1
  assert obj and obj.is_file();objects[str(source.relative_to(root))]={'bytes':obj.stat().st_size,'sha256':sha(obj)}
  raw=subprocess.check_output([*scan,'-M','-MT','dependencies'],cwd=entry['directory'],text=True);commands.append(scan)
  for name in shlex.split(raw.split(':',1)[1].replace('\\\n',' ')):
   path=Path(name)
   if not path.is_absolute():path=Path(entry['directory'])/path
   path=path.resolve();key=None
   for label,base in [('runtime',root),('sdk',sdk),('generated',build),('toolchain',toolroot)]:
    if path.is_relative_to(base):key=label+'/'+str(path.relative_to(base));break
   assert key,'Unpinned external target dependency: '+str(path)
   inputs[key]=sha(path)
 assert set(objects)==wanted
 for required in ['runtime/src/runtime/workers/ResidentWorker.h','runtime/src/ports/esp32s3/NativeResidentWorker.h','runtime/sdk/driver/RiscResidentWorkerV1.h']:
  assert required in inputs,'Target did not compile worker source: '+required
 elf=build/'riscrte_native.elf';data=elf.read_bytes();assert data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(2,94)
 nm=compiler.with_name(compiler.name.replace('g++','nm').replace('gcc','nm'));symbols=subprocess.check_output([str(nm),'-S','-C',str(elf)],text=True)
 authority=[line for line in symbols.splitlines() if line.endswith('RiscCpu::NativeResidentWorker::authority')];assert len(authority)==1
 fields=authority[0].split();address=int(fields[0],16);size=int(fields[1],16);assert 0x3fc00000<=address<0x3fd00000 and 0<size<=4096,'Worker atomics are not linked in internal DRAM'
 assert 'RiscCpu::NativeResidentWorker::residentEntry' in symbols
 receipt=dict(runtime_commit=commit,sdk_commit=sdkcommit,compiler={'path':str(compiler),'sha256':sha(compiler),'version':subprocess.check_output([str(compiler),'--version'],text=True)},objects=objects,inputs=dict(sorted(inputs.items())),commands=commands,authority={'address':address,'bytes':size},elf={'bytes':len(data),'sha256':sha(elf)},binary={'bytes':(build/'riscrte_native.bin').stat().st_size,'sha256':sha(build/'riscrte_native.bin')},sdkconfig_sha256=sha(build/'config/sdkconfig.h'),hardware_tested=False,target_execution=False,private_import_cohort_changed=False)
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(receipt,indent=2)+'\n');print('Actual native Runtime worker objects/link/DRAM/SDK provenance PASS; not device execution')
if __name__=='__main__':main()
