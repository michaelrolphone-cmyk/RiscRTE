#!/usr/bin/env python3
"""Bind actual native worker-bearing Runtime objects, SDK and linked artifact."""
import argparse,hashlib,json,shlex,shutil,struct,subprocess
from pathlib import Path
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def required_dram_symbols(symbols,names):
 additional_dram={}
 for name in names:
  assert name and name not in additional_dram,'Empty/duplicate required symbol'
  rows=[line.split() for line in symbols.splitlines() if len(line.split(maxsplit=3))==4 and line.split(maxsplit=3)[3]==name]
  assert len(rows)==1,'Required resident symbol missing/ambiguous: '+name
  assert rows[0][2] in ('b','B','d','D'),'Required symbol is not allocated writable data: '+name
  addr=int(rows[0][0],16);length=int(rows[0][1],16)
  assert 0x3fc00000<=addr<0x3fd00000 and 0<length<=4096 and addr+length<=0x3fd00000,'Required state is not internal DRAM: '+name
  additional_dram[name]={'address':addr,'bytes':length}
 return additional_dram
def fixture_sources(extra,marker,legacy=False):
 assert bool(extra)==bool(marker),'Additional fixture TUs require an explicit startup marker'
 assert not(marker and legacy),'Ambiguous fixture selection'
 assert len(set(extra))==len(extra),'Duplicate fixture translation unit'
 for name in extra:assert not Path(name).is_absolute() and '..' not in Path(name).parts and name.endswith(('.c','.cpp')),'Invalid fixture translation unit'
 return set(extra)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--execution-fixture',action='store_true');p.add_argument('--provider-fixture',choices=['clean','retained']);p.add_argument('--required-input',action='append',default=[]);p.add_argument('--dram-symbol',action='append',default=[]);p.add_argument('--extra-translation-unit',action='append',default=[]);p.add_argument('--app-main-fixture-marker');a=p.parse_args();assert not(a.execution_fixture and a.provider_fixture);root=a.runtime.resolve();sdk=a.sdk.resolve();build=a.build.resolve()
 fixture_sources(a.extra_translation_unit,a.app_main_fixture_marker,a.execution_fixture or a.provider_fixture)
 commit=subprocess.check_output(['git','-C',root,'rev-parse','HEAD'],text=True).strip();sdkcommit=subprocess.check_output(['git','-C',sdk,'rev-parse','HEAD'],text=True).strip();assert sdkcommit=='fff9895c82d744c7237be8847347bdd1b07c6643'
 config=(build/'config/sdkconfig.h').read_text();assert '#define CONFIG_FREERTOS_SMP 1' not in config,'Unreviewed Amazon SMP worker-create path';assert '#define CONFIG_FREERTOS_NUMBER_OF_CORES 2' in config
 entries=json.loads((build/'compile_commands.json').read_text());wanted={'src/ports/esp32s3/CpuPort.cpp','src/ports/esp32s3/NativeHardware.cpp'};commands=[];inputs={};objects={};compiler=None
 wanted.update(a.extra_translation_unit)
 if a.execution_fixture:wanted.add('test/native_worker_target/probe.cpp')
 if a.provider_fixture:wanted.update({'test/native_worker_provider/native.cpp','src/bootstrap/EmbeddedStore.cpp'})
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
 for required in ['runtime/src/runtime/workers/ResidentWorker.h','runtime/src/ports/esp32s3/NativeResidentWorker.h','runtime/sdk/driver/RiscResidentWorkerV1.h']+a.required_input:
  assert required in inputs,'Target did not compile worker source: '+required
 elf=build/'riscrte_native.elf';data=elf.read_bytes();assert data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(2,94)
 nm=compiler.with_name(compiler.name.replace('g++','nm').replace('gcc','nm'));symbols=subprocess.check_output([str(nm),'-S','-C',str(elf)],text=True)
 authority=[line for line in symbols.splitlines() if line.endswith('RiscCpu::NativeResidentWorker::authority')];assert len(authority)==1
 fields=authority[0].split();address=int(fields[0],16);size=int(fields[1],16);assert 0x3fc00000<=address<0x3fd00000 and 0<size<=4096,'Worker atomics are not linked in internal DRAM'
 additional_dram=required_dram_symbols(symbols,a.dram_symbol)
 assert 'RiscCpu::NativeResidentWorker::residentEntry' in symbols
 if a.execution_fixture or a.provider_fixture or a.app_main_fixture_marker:
  assert '__wrap_app_main' in symbols and commit.encode() in data
  if a.execution_fixture:assert b'WORKER_PROBE result=scheduler_cases_passed' in data
  elif a.provider_fixture:assert b'WORKER_GRAPH result=provider_case_passed' in data and '__wrap_esp_elf_deinit' in symbols
  else:assert a.app_main_fixture_marker.encode() in data,'Selected native fixture marker is absent'
  links=subprocess.check_output(['ninja','-C',str(build),'-t','commands','riscrte_native.elf'],text=True).splitlines()
  selected=[]
  for line in links:
   args=shlex.split(line)
   if any(arg=='-o' and i+1<len(args) and Path(args[i+1]).name=='riscrte_native.elf' for i,arg in enumerate(args)):selected.append(args)
  assert len(selected)==1 and '-Wl,--wrap=app_main' in selected[0], 'Native test startup wrapper not selected'
 provider=None
 if a.provider_fixture:
  assert '-Wl,--wrap=esp_elf_deinit' in selected[0]
  assert '#define CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT 1' in config
  generated=build/'fixture';producer=json.loads((generated/'producer.json').read_text())
  assert producer['runtime_commit']==commit and producer['mode']==a.provider_fixture
  assert producer['recipe_sha256']==sha(root/'test/native_worker_provider/build_fixture.py')
  assert len(producer['commands'])==2
  producer_compilers={Path(command[0]).resolve() for command in producer['commands']}
  assert len(producer_compilers)==1
  producer_cc=producer_compilers.pop()
  assert producer_cc.parent==compiler.parents[1]/'bin' and producer_cc.name.endswith('gcc'), 'Producer compiler is outside the pinned native toolchain'
  assert sha(producer_cc)==producer['compiler_sha256'], 'Producer gcc hash mismatch'
  assert subprocess.check_output([str(producer_cc),'--version'],text=True)==producer['compiler_version']
  assert producer['target_execution'] is False and producer['provider_lifecycle_qualified'] is False
  for name,digest in producer['generated'].items():assert sha(generated/name)==digest
  for name,item in producer['artifacts'].items():
   payload=(generated/name).read_bytes();assert len(payload)==item['bytes'] and sha(generated/name)==item['sha256']
   assert payload in data, 'Selected payload is absent from linked immutable store: '+name
  for key,digest in producer['inputs'].items():
   label,path=key.split('/',1);base={'runtime':root,'toolchain':compiler.parents[1]}[label]
   assert sha(base/path)==digest,'Producer input drift: '+key
  for name in ['WorkerGraphPolicy.h','WorkerGraphProfile.h','RiscEmbeddedStore.h']:
   assert inputs['generated/fixture/'+name]==sha(generated/name)
  provider={'compiler':{'path':str(producer_cc),'sha256':sha(producer_cc),'version':producer['compiler_version']},'profile':a.provider_fixture,'producer_sha256':sha(generated/'producer.json'),'payloads':producer['artifacts'],'compiled_cases':1,'executed_cases':0}
 receipt=dict(additional_translation_units=a.extra_translation_unit,app_main_fixture_marker=a.app_main_fixture_marker,additional_dram=additional_dram,required_inputs=a.required_input,provider_fixture=provider,execution_fixture=a.execution_fixture,provider_graph_qualified=False,runtime_commit=commit,sdk_commit=sdkcommit,compiler={'path':str(compiler),'sha256':sha(compiler),'version':subprocess.check_output([str(compiler),'--version'],text=True)},objects=objects,inputs=dict(sorted(inputs.items())),commands=commands,authority={'address':address,'bytes':size},elf={'bytes':len(data),'sha256':sha(elf)},binary={'bytes':(build/'riscrte_native.bin').stat().st_size,'sha256':sha(build/'riscrte_native.bin')},sdkconfig_sha256=sha(build/'config/sdkconfig.h'),hardware_tested=False,target_execution=False,private_import_cohort_changed=False)
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(receipt,indent=2)+'\n');print('Actual native Runtime worker objects/link/DRAM/SDK provenance PASS; not device execution')
if __name__=='__main__':main()
