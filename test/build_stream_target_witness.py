#!/usr/bin/env python3
"""Build and structurally validate real Xtensa software stream witnesses only."""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--system',type=Path,required=True);p.add_argument('--cc',required=True);p.add_argument('--output',type=Path,default=root/'build/stream-target-witness');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);include=out/'sdk'
shutil.copytree(a.system/'lib/PortableApps/include',include,dirs_exist_ok=True)
for group,names in [('app',['RiscRuntimeV1.h','RiscStreamClientV1.h','RiscSerialStreamSessionV1.h']),('driver',['RiscStreamResultV1.h','RiscStreamSessionProviderV1.h','RiscProviderV2.h','RiscStreamProviderV1.h'])]:
 for name in names:shutil.copy2(root/'sdk'/group/name,include/name)
flags=['-std=c11','-Os','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-nostdlib','-nostartfiles','-shared','-Wl,--hash-style=sysv','-I'+str(include),'-I'+str(a.system/'lib/NativeApps/include')]
validator=out/'validate'
subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-I'+str(root/'test/native_apps/stubs'),'-I'+str(root/'lib/elf_loader/include'),str(root/'lib/elf_loader/src/esp_elf_validate.c'),str(root/'test/native_apps/validate_test.c'),'-o',validator],check=True)
records=[]
for name,sources,defs,entry,allowed in [
 ('provider',[root/'test/fixtures/serial_stream_witness.c'],['-DRISC_STREAM_TARGET_WITNESS'],'t5_driver_get',{'memcpy','memset','strcmp'}),
 ('client',[root/'test/fixtures/serial_stream_target_app.c',a.system/'lib/PortableApps/src/PortableSerialClient.c'],['-DPORTABLE_SERIAL_STREAMS'],'app_main',{'risc_runtime_get_api','memcpy','memset','strcmp'})]:
 elf=out/(name+'.elf');subprocess.run([a.cc,*flags,*defs,*map(str,sources),'-o',str(elf)],check=True)
 readelf=a.cc.replace('gcc','readelf');header=subprocess.check_output([readelf,'-h',elf],text=True)
 assert 'DYN (Shared object file)' in header and 'Xtensa' in header
 symbols=subprocess.check_output([readelf,'--dyn-syms','--wide',elf],text=True)
 imports={row.split()[-1] for row in symbols.splitlines() if 'GLOBAL' in row and 'UND' in row}
 assert imports<=allowed,imports
 assert any('FUNC' in row and 'GLOBAL' in row and 'UND' not in row and row.split()[-1]==entry for row in symbols.splitlines())
 subprocess.run([validator,elf],check=True)
 records.append({'file':elf.name,'size':elf.stat().st_size,'sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'imports':sorted(imports)})
 print(name+': Xtensa ET_DYN/imports/structural rejection checks PASS')
(out/'evidence.json').write_text(json.dumps({'runtime_source':subprocess.check_output(['git','-C',root,'rev-parse','HEAD'],text=True).strip(),'system_source':subprocess.check_output(['git','-C',a.system,'rev-parse','HEAD'],text=True).strip(),'artifacts':records,'hardware':'not run; software witnesses only'},indent=2)+'\n')
