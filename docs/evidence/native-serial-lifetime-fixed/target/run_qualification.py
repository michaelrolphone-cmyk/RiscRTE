#!/usr/bin/env python3
import pathlib,json,subprocess,hashlib,re,sys,shlex,datetime
O=pathlib.Path(__file__).parent;R=pathlib.Path('/workspace/shared/serial-lifetime-20261018');B=pathlib.Path('/workspace/shared/serial-lifetime-evidence/baseline-source');T=pathlib.Path('/workspace/shared/wave-stat-successor/toolchain/xtensa-esp-elf');base=json.loads((O/'base-command.json').read_text());P=O/(sys.argv[1] if len(sys.argv)>1 else 'pass-1');P.mkdir()
C=pathlib.Path(base[0]);NM=C.with_name(C.name.replace('g++','nm'));OB=C.with_name(C.name.replace('g++','objdump'))
commands=[];result={'started':datetime.datetime.now(datetime.timezone.utc).isoformat(),'scope':'Xtensa production compilation/layout with explicit host boundary header substitutions, not SDK/link qualification','compiles':[],'types':{}}
def h(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def sources(root):
 return {str(p.relative_to(root)):h(p) for f in ['src','sdk','include','lib','experimental/native_replica'] for p in (root/f).rglob('*') if p.is_file() and p.suffix in ['.h','.hpp','.c','.cpp','.inc']}
result['before']={'candidate':sources(R),'baseline':sources(B)}
def run(cmd,name):
 commands.append({'name':name,'argv':[str(x) for x in cmd]});p=subprocess.run(cmd,text=True,capture_output=True,cwd=R);(P/(name+'.log')).write_text(p.stdout+p.stderr);print(name,p.returncode, (p.stdout+p.stderr)[-500:],flush=True);(P/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');return p
shims=['native_settings_payload_shim','native_settings_atomic_shim','native_sleep_diagnostics_idf6_shim','native_platform_shim','elf_yield_shim','native_bank_stubs']
def flags(root,on,profile='expanded',shim=True):
 args=[a.replace(str(R),str(root)) for a in base]
 args=[a for a in args if not a.startswith(('-DRISC_NATIVE_SERIAL_FILES=','-DRISC_NATIVE_SERIAL_APPDATA_FILES=','-DRISC_NATIVE_SERIAL_SD=','-DRISC_NATIVE_SERIAL_BOOT_FILES='))]
 args+=['-DRISC_NATIVE_SERIAL_FILES='+str(on),'-DRISC_NATIVE_SERIAL_APPDATA_FILES='+str(on),'-DRISC_NATIVE_SERIAL_SD='+str(on),'-DRISC_NATIVE_SERIAL_BOOT_FILES=0','-include',str(O/'prior-evidence/config/sdkconfig.h')]
 if profile!='expanded':
  args=[a for a in args if not a.startswith(('-DRISC_INSTALLED_APP_CAPACITY=','-DRISC_PROVIDER_CAPACITY='))]
  if profile=='legacy':args=[a for a in args if not a.startswith(('-DRISC_PAIRED_BANKS=','-DRISC_RUNTIME_METADATA_PSRAM='))]
  if profile=='installed':args+=['-DRISC_INSTALLED_APP_CAPACITY=64']
 if shim:args+=['-include','/workspace/shared/stat-observation-recovery/idf-headers/components/esp_common/include/esp_err.h','-I'+str(O/'boundary-shims'),*['-I'+str(root/'test'/x) for x in shims],'-I/workspace/shared/stat-observation-recovery/idf-headers/components/esp_common/include']
 return args
for who,root in [('baseline',B),('candidate',R)]:
 for on in (0,1):
  for profile in ['expanded','cohort','installed','legacy']:
   name=f'{who}-types-{profile}-{on}';obj=P/(name+'.o');p=run([*flags(root,on,profile),'-DQUALIFY_CANDIDATE='+str(int(who=='candidate')),'-fno-access-control','-MMD','-MF',str(P/(name+'.d')),'-c',str(O/'types.cpp'),'-o',str(obj)],name)
   if p.returncode==0:
    syms=run([str(NM),'-S','--size-sort',str(obj)],name+'-nm').stdout
    result['types'][name]={m.group(2):int(m.group(1),16) for m in re.finditer(r'^[0-9a-f]+\s+([0-9a-f]+)\s+\w\s+(measure_\w+)$',syms,re.M)}
for who,root in [('baseline',B),('candidate',R)]:
 name=who+'-native-types';obj=P/(name+'.o');p=run([*flags(root,1),'-c',str(O/'native-types.cpp'),'-o',str(obj)],name)
 if not p.returncode:
  syms=run([str(NM),'-S','--size-sort',str(obj)],name+'-nm').stdout
  result['types'][name]={m.group(2):int(m.group(1),16) for m in re.finditer(r'^[0-9a-f]+\s+([0-9a-f]+)\s+\w\s+(measure_\w+)$',syms,re.M)}
for profile in ['cohort','installed','legacy']:
 name='candidate-graph-'+profile;obj=P/(name+'.o');p=run([*flags(R,1,profile),'-fstack-usage','-c',str(R/'src/runtime/drivers/ProviderGraphV2.cpp'),'-o',str(obj)],name)
 if not p.returncode:run([str(OB),'-drC',str(obj)],name+'-disassembly')
for shim in [False,True]:
 for on in (0,1):
  for rel in ['src/bootstrap/Runtime.cpp','src/main.cpp','src/ports/esp32s3/NativeSerialFiles.cpp','src/ports/esp32s3/NativeSerialStorage.cpp','src/runtime/drivers/ProviderGraphV2.cpp']:
   source=R/rel;name=('shim' if shim else 'sdk-only')+'-'+source.stem+'-'+str(on);obj=P/(name+'.o');p=run([*flags(R,on,shim=shim),'-fstack-usage','-MMD','-MF',str(P/(name+'.d')),'-c',str(source),'-o',str(obj)],name)
   record={'name':name,'source':rel,'serial_enabled':bool(on),'boundary_shims':shim,'returncode':p.returncode}
   if not p.returncode:
    record['object_sha256']=h(obj);record['object_bytes']=obj.stat().st_size
    run([str(NM),'-S','-C',str(obj)],name+'-nm');run([str(OB),'-h','-t',str(obj)],name+'-objdump');run([str(OB),'-drC',str(obj)],name+'-disassembly')
   result['compiles'].append(record)
result['after']={'candidate':sources(R),'baseline':sources(B)};result['source_hashes_stable']=result['before']==result['after'];result['completed']=datetime.datetime.now(datetime.timezone.utc).isoformat();(P/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print('DONE',json.dumps({'stable':result['source_hashes_stable'],'types':result['types'],'compiles':result['compiles']},indent=2),flush=True)
