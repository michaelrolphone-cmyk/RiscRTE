#!/usr/bin/env python3
"""Build exact test-owned native provider/app and immutable typed fixture store."""
import argparse,hashlib,json,pathlib,shlex,shutil,struct,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
def sha(data):return hashlib.sha256(data).hexdigest()
def validate_image(data,symbols,entry,allowed):
 assert len(data)>=52 and data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(3,94), 'Expected ELF32 little-endian Xtensa shared object'
 rows=[line.split() for line in symbols if len(line.split())>=7 and line.split()[0].endswith(':')]
 assert any(len(row)==8 and row[3]=='FUNC' and row[4]=='GLOBAL' and row[5]=='DEFAULT' and row[6]!='UND' and row[7]==entry for row in rows), 'Missing exported entry'
 imports=sorted({row[7] for row in rows if len(row)==8 and row[6]=='UND'})
 assert set(imports)<=allowed, 'Unexpected imports: '+repr(imports)
 return imports

def main():
 p=argparse.ArgumentParser();p.add_argument('--runtime',type=pathlib.Path,default=ROOT);p.add_argument('--cc',required=True);p.add_argument('--mode',choices=['clean','retained'],required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();root=a.runtime.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 sys.path.insert(0,str(root/'test'));from run_resident_worker import check_compiler_environment
 check_compiler_environment();cc=pathlib.Path(shutil.which(a.cc) or a.cc).resolve();tools=cc.parents[1];readelf=cc.with_name(cc.name.removesuffix('gcc')+'readelf')
 common=[str(cc),'-std=c11','-Os','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-nostdlib','-nostartfiles','-shared','-Wl,--hash-style=sysv',*['-I'+str(root/'sdk'/x) for x in ['app','driver','hardware']]]
 inputs={'runtime/test/run_resident_worker.py':sha((root/'test/run_resident_worker.py').read_bytes())};commands=[];artifacts={}
 for name,source,entry,allowed,extra in [('driver.elf','provider.c','t5_driver_get',{'strcmp','memset'},[]),('default.elf','app.c','app_main',{'risc_runtime_get_api','memset'},['-DWORKER_RETAINED='+str(int(a.mode=='retained'))])]:
  path=root/'test/native_worker_provider'/source;prefix=common+extra
  raw=subprocess.check_output([*prefix,'-M','-MT','dependencies',str(path)],text=True)
  for item in shlex.split(raw.split(':',1)[1].replace('\\\n',' ')):
   f=pathlib.Path(item).resolve();label='runtime/'+str(f.relative_to(root)) if f.is_relative_to(root) else 'toolchain/'+str(f.relative_to(tools)) if f.is_relative_to(tools) else None
   assert label,'Unexpected external compiler input: '+str(f);inputs[label]=sha(f.read_bytes())
  cmd=[*prefix,str(path),'-o',str(out/name)];subprocess.run(cmd,check=True);commands.append(cmd);data=(out/name).read_bytes()
  symbols=subprocess.check_output([str(readelf),'--dyn-syms','--wide',str(out/name)],text=True).splitlines()
  imports=validate_image(data,symbols,entry,allowed)
  artifacts[name]={'bytes':len(data),'sha256':sha(data),'imports':imports,'entry':entry}
 manifest={'type':'driver','id':'native-worker-probe','version':'1.0.0','driver_abi':2,'architecture':'xtensa-esp32s3','file_name':'driver.elf','requires':[{'capability':'hardware.device','api':1},{'capability':'platform.resident-worker','api':1}],'provides':[{'capability':'test.worker-native','api':1}],'hardware_compatibility':[{'compatible':'test,gpio','revisions':['unspecified'],'config_type':'gpio.bank','config_version':1}]}
 app={'type':'application','id':'native-worker-app-'+a.mode,'version':'1.0.0','architecture':'xtensa-esp32s3','file_name':'default.elf','entry':'app_main','requires':[{'capability':'test.worker-native','api':1}]}
 board={'schema':'riscrte.board-hardware','schema_version':1,'board_id':'worker-test','revision':'unspecified','buses':[],'devices':[{'instance_id':1,'chip':{'vendor':'test','model':'gpio','revision':'unspecified'},'compatible':'test,gpio','config_type':'gpio.bank','config_version':1,'config':{'pins':[8],'active_high':True,'pull_up':False,'debounce_us':0,'long_press_us':0,'click_min_us':0}}]}
 boot={'board':'board.json','default_app':'default.elf','drivers':[{'manifest':'driver.json','instance_id':1}],'app_capabilities':[{'manifest':'app.json','grants':[{'capability':'test.worker-native','api':1,'instance_id':1}]}]}
 for name,value in [('driver.json',manifest),('app.json',app),('board.json',board),('boot.json',boot)]:(out/name).write_text(json.dumps(value,separators=(',',':'))+'\n')
 names=['boot.json','board.json','driver.json','app.json','driver.elf','default.elf'];lines=[];records=[]
 for i,name in enumerate(names):
  data=(out/name).read_bytes();lines.append('static const unsigned char worker_store_'+str(i)+'[]={'+','.join(map(str,data))+'};');records.append('{"/'+name+'",worker_store_'+str(i)+',sizeof(worker_store_'+str(i)+')}')
 lines.append('static const StoreFile embeddedFiles[]={'+','.join(records)+'};');(out/'RiscEmbeddedStore.h').write_text('\n'.join(lines)+'\n')
 driver=artifacts['driver.elf'];imports=','.join(json.dumps(x) for x in driver['imports']) or 'nullptr';digest=','.join('0x'+driver['sha256'][i:i+2] for i in range(0,64,2))
 policy='''#pragma once
#include "runtime/drivers/NativeProviderPolicyV1.h"
namespace RuntimeProviders {inline NativeProviderPolicySetV1 selectedNativeProviderPoliciesV1(){
 static const char* const imports[]={IMPORTS};
 static const NativeProviderRequirementV1 requirements[]={{"hardware.device",1},{"platform.resident-worker",1}};
 static const NativeProviderPolicyV1 policy{"driver.elf","native-worker-probe","1.0.0","test.worker-native",1,2,LENGTH,{DIGEST},imports,COUNT,requirements,2,0};
 return {&policy,1};}}
'''.replace('IMPORTS',imports).replace('LENGTH',str(driver['bytes'])).replace('DIGEST',digest).replace('COUNT',str(len(driver['imports'])))
 (out/'WorkerGraphPolicy.h').write_text(policy);(out/'WorkerGraphProfile.h').write_text('#pragma once\n#define WORKER_GRAPH_RETAINED '+str(int(a.mode=='retained'))+'\n#define WORKER_GRAPH_PROFILE '+json.dumps(a.mode)+'\n')
 receipt={'mode':a.mode,'runtime_commit':subprocess.check_output(['git','-C',root,'rev-parse','HEAD'],text=True).strip(),'recipe_sha256':sha(pathlib.Path(__file__).read_bytes()),'compiler_sha256':sha(cc.read_bytes()),'compiler_version':subprocess.check_output([str(cc),'--version'],text=True),'inputs':dict(sorted(inputs.items())),'commands':commands,'artifacts':artifacts,'generated':{f.name:sha(f.read_bytes()) for f in out.iterdir() if f.name in names+['RiscEmbeddedStore.h','WorkerGraphPolicy.h','WorkerGraphProfile.h']},'target_execution':False,'provider_lifecycle_qualified':False}
 (out/'producer.json').write_text(json.dumps(receipt,indent=2)+'\n');print('Compiled exact typed provider/app/store; native execution and lifecycle still unobserved')
if __name__=='__main__':main()
