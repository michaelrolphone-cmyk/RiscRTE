#!/usr/bin/env python3
"""Experimental single-job native link using a pinned baseline's immutable archives.
Never creates a flashable package, changes boot packing, or selects a product.
"""
import argparse,ast,hashlib,json,pathlib,shlex,subprocess,sys
P=pathlib.Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline',type=P,required=True)
p.add_argument('--output',type=P,required=True)
p.add_argument('--mode',choices=['baseline','selected'],required=True)
p.add_argument('--no-compile',action='store_true')
a=p.parse_args();root=P(__file__).resolve().parents[1];base=a.baseline.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
db=base/'compile_commands.json';entries=json.loads(db.read_text());entry=next(e for e in entries if e['file'].endswith('/Runtime.cpp'));cmd=shlex.split(entry['command']);tool=cmd[0];sdk=P(next(x[2:] for x in cmd if x.startswith('-I') and x.endswith('/tools/sdk/esp32s3/qio_opi/include'))).parents[1]
build=base/'.pio/build/esp32s3-16mb-appdata-iq-stage';mapfile=build/'firmware.map';lines=mapfile.read_text().splitlines();first=lines.index('START GROUP');end=lines.index('END GROUP',first)
resolve=lambda s:str(base/s) if not P(s).is_absolute() else s
objects=[resolve(l[5:]) for l in lines[:first] if l.startswith('LOAD .pio/') and l.endswith('.o')]
archives=[resolve(l[5:]) for l in lines[first:end] if l.startswith('LOAD ') and l.endswith('.a')]
script=sdk.parents[2]/'tools/platformio-build-esp32s3.py';tree=ast.parse(script.read_text());linknode=next(kw.value for n in ast.walk(tree) if isinstance(n,ast.Call) for kw in n.keywords if kw.arg=='LINKFLAGS');ldflags=[ast.literal_eval(n) for n in linknode.elts if isinstance(n,ast.Constant)]
ldflags+=['-L'+str(sdk/x) for x in ['lib','ld','qio_opi']]
ldflags+=['-Wl,--wrap=nvs_flash_init','-Wl,--wrap=app_main']
ldflags+=['-Wl,-u,'+s for s in ['risc_paired_store_abi','risc_runtime_update_version','risc_sdmmc_host_abi','risc_x4_native_composition_identity','risc_installed_app_capacity']]
ldflags+=['-Wl,-Map='+str(out/'firmware.map')]
selected=['BOARD_XTEINK_X4_PRO','RISC_NATIVE_SETTINGS_BACKEND=1','RISC_NATIVE_SETTINGS_STARTUP_ENTROPY=1','RISC_NATIVE_APP_DATA_MOUNT=1','RISC_NATIVE_SETTINGS_ASSEMBLY=1','RISC_NATIVE_SETTINGS_PSRAM_PAYLOAD=1','RISC_NATIVE_SETTINGS_LIFECYCLE=1','RISC_NATIVE_SETTINGS_WORKER=1','RISC_RUNTIME_ATOMIC_KEY_VALUE=1','RISC_NATIVE_STORAGE_CUSTODY=1']
wraps=['esp_vfs_register','esp_vfs_unregister','esp_partition_read','esp_partition_write','esp_partition_erase_range','esp_littlefs_format','esp_littlefs_format_partition']
calls=[];sha=lambda q:hashlib.sha256(P(q).read_bytes()).hexdigest()
def sources():return {str(q.relative_to(root)):sha(q) for folder in ['src','sdk','include','experimental/native_replica','lib'] for q in (root/folder).rglob('*') if q.is_file() and q.suffix in ['.cpp','.c','.h','.hpp','.ipp','.tpp','.inc']}
source_input=sources()
manifest={'scope':'Experimental native link only; no hardware, product selection, release or flash qualification. Archives and early wrapper reused read-only from exact pinned baseline.','mode':a.mode,'link_only':a.no_compile,'source_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'compile_database_sha256':sha(db),'baseline_map_sha256':sha(mapfile),'inputs':{},'commands':calls}
def run(c):
 calls.append(c);print('RUN',c[0],c[-1],flush=True);r=subprocess.run(c,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);print(r.stdout,end='',flush=True)
 with (out/'build.log').open('a') as f:f.write(shlex.join(c)+'\n'+r.stdout)
 if r.returncode:raise subprocess.CalledProcessError(r.returncode,c)
try:
 if a.mode=='selected':
  flags=[];skip=False
  for arg in cmd[1:]:
   if skip:skip=False;continue
   if arg=='-o':skip=True;continue
   if arg in ('-c','-MMD') or arg.endswith('/Runtime.cpp'):continue
   if arg.startswith('-I') and not P(arg[2:]).is_absolute():arg='-I'+str((base if arg.startswith('-I.pio/') else root)/arg[2:])
   if arg.startswith('-ffile-prefix-map='):arg='-ffile-prefix-map='+str(root)+'=.'
   flags.append(arg)
  flags=['-I'+str(root),*flags,*['-D'+s for s in selected],'-fstack-usage']
  objects=[o for o in objects if '/x4-native/' in o]
  for source in sorted((root/'src').rglob('*.cpp')):
   rel=str(source.relative_to(root));obj=out/(rel+'.o');obj.parent.mkdir(parents=True,exist_ok=True)
   manifest['inputs'][rel]=sha(source)
   if not a.no_compile:run([tool,*flags,'-c',str(source),'-o',str(obj)])
   objects.append(str(obj))
  ldflags+=['-Wl,--wrap='+s for s in wraps]
  # Both format wrappers must remain in the final map even if the pinned archive
  # has no external call to one format entrypoint under this exact gc graph.
  ldflags+=['-Wl,-u,__wrap_'+s for s in wraps]
 manifest['reused_inputs']={x:sha(x) for x in dict.fromkeys(objects+archives) if not str(x).startswith(str(out))}
 run([tool,'-o',str(out/'firmware.elf'),*ldflags,*objects,'-Wl,--start-group',*archives,'-Wl,--end-group'])
 run([tool.removesuffix('g++')+'size','-A',str(out/'firmware.elf')])
 run([sys.executable,str(root/'scripts/radio_iq_proof.py'),str(out/'firmware.elf'),'--output',str(out/'radio-iq-proof.json')])
 assert source_input==sources(),'Source changed during selected build'
 manifest['result']='linked-and-IQ-proof-passed'
except subprocess.CalledProcessError as e:
 manifest['result']='failed';manifest['failed_command']=e.cmd;manifest['returncode']=e.returncode
 raise
finally:
 manifest['source_sha256']=source_input
 manifest['source_unchanged']=source_input==sources()
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
