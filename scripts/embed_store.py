"""Generate a read-only VFS store from separately linked real ELF bytes, for CI."""
from pathlib import Path
import subprocess
Import('env')
root=Path(env.subst('$PROJECT_DIR'))
cc=env.PioPlatform().get_package_dir('toolchain-xtensa-esp32s3')+'/bin/xtensa-esp32s3-elf-gcc'
subprocess.run([env.subst('$PYTHONEXE'),str(root/'scripts/build_apps.py'),'--cc',cc],check=True)
build=Path(env.subst('$BUILD_DIR'));build.mkdir(parents=True,exist_ok=True)
lines=[];records=[]
for i,name in enumerate(('boot.json','board.json','default.elf')):
    source=root/env.GetProjectOption('custom_boot_board','data/board.json') if name=='board.json' else root/'build/store'/name
    data=source.read_bytes()
    assert data and len(data)<1024*1024
    lines.append('static const unsigned char store_'+str(i)+'[]={'+','.join(map(str,data))+'};')
    records.append('{"/'+name+'",store_'+str(i)+',sizeof(store_'+str(i)+')}')
lines.append('static const StoreFile embeddedFiles[]={'+','.join(records)+'};')
content='\n'.join(lines)+'\n';header=build/'RiscEmbeddedStore.h'
if not header.exists() or header.read_text()!=content:header.write_text(content)
