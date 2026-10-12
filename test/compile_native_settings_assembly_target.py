#!/usr/bin/env python3
"""Serial .99 target compile-only assembly/allocator, layout and default-off gates."""
import argparse, hashlib, json, pathlib, shlex, subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline', default='d3d2ab0a633b546ae0c1c5d11a500d8c682049bb');p.add_argument('compile_commands', type=pathlib.Path);p.add_argument('output', type=pathlib.Path)
a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
entry=next(x for x in json.loads(a.compile_commands.read_text()) if x['file'].endswith('/Runtime.cpp'))
command=shlex.split(entry['command']);tool=command[0];flags=[];skip=False
for arg in command[1:]:
    if skip:skip=False;continue
    if arg=='-o':skip=True;continue
    if arg in ('-c','-MMD') or arg.endswith('/Runtime.cpp'):continue
    if arg.startswith('-I') and not pathlib.Path(arg[2:]).is_absolute():arg='-I'+str(root/arg[2:])
    if arg.startswith('-ffile-prefix-map='):arg=f'-ffile-prefix-map={root}=.'
    flags.append(arg)
flags+=['-I'+str(root),'-fstack-usage','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers']
selected=['-DBOARD_XTEINK_X4_PRO','-DRISC_NATIVE_SETTINGS_ASSEMBLY=1','-DRISC_NATIVE_SETTINGS_PSRAM_PAYLOAD=1','-DRISC_NATIVE_SETTINGS_LIFECYCLE=1','-DRISC_NATIVE_SETTINGS_WORKER=1','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1','-DRISC_NATIVE_STORAGE_CUSTODY=1']
probe=out/'assembly-layout.cpp'
probe.write_text('#include "'+str(root/'src/ports/esp32s3/NativeSettingsAssembly.cpp')+'"\nextern "C" { char assembly_controls[sizeof(RiscCpu::NativeSettingsAssembly::state)]; char assembly_namespace_controls[sizeof(RiscCpu::NativeSettingsAssembly::NamespaceControl)]; }\n')
sources=[(root/'src/ports/esp32s3/NativeSettingsAssembly.cpp','assembly',selected),(root/'src/ports/esp32s3/NativeSettingsPayloadAllocator.cpp','payload',selected),(root/'src/runtime/storage/AppDataFiles.cpp','files',selected),(probe,'layout',selected),(root/'src/ports/esp32s3/NativeSettingsAssembly.cpp','assembly-off',[])]
commands=[]
for source,name,extra in sources:
    cmd=[tool,*flags,*extra,'-c',str(source),'-o',str(out/(name+'.o'))];commands.append(cmd);subprocess.run(cmd,cwd=root,check=True)
    nm=subprocess.check_output([tool.removesuffix('g++')+'nm','-S',str(out/(name+'.o'))],text=True);(out/(name+'.nm')).write_text(nm)
    if name=='assembly-off':assert not nm.strip()
    for line in nm.splitlines():
        if ' assembly_' in line:print(line)
# These are explicit selection dependencies, not new selectable target profiles.
for missing in ('RISC_NATIVE_SETTINGS_LIFECYCLE','RISC_NATIVE_SETTINGS_PSRAM_PAYLOAD','BOARD_XTEINK_X4_PRO'):
    denied=[x for x in selected if not x.startswith('-D'+missing)]
    cmd=[tool,*flags,*denied,'-c',str(sources[0][0]),'-o',str(out/'refused.o')];commands.append(cmd)
    result=subprocess.run(cmd,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    assert result.returncode and '#error' in result.stdout, missing
    (out/(missing+'-refused.log')).write_text(result.stdout)
# Use the same source pathname and flags on both sides to exclude filename noise.
parity=out/'files-parity.cpp';parity_header=out/'AppDataFiles.h';hashes=[]
for original in (True,False):
    parity_header.write_bytes(subprocess.check_output(['git','show',a.baseline+':src/runtime/storage/AppDataFiles.h'],cwd=root) if original else (root/'src/runtime/storage/AppDataFiles.h').read_bytes())
    parity.write_bytes(subprocess.check_output(['git','show',a.baseline+':src/runtime/storage/AppDataFiles.cpp'],cwd=root) if original else (root/'src/runtime/storage/AppDataFiles.cpp').read_bytes())
    cmd=[tool,*flags,'-I'+str(root/'src/runtime/storage'),'-c',str(parity),'-o',str(out/'files-default-off.o')];commands.append(cmd);subprocess.run(cmd,cwd=root,check=True)
    hashes.append(hashlib.sha256((out/'files-default-off.o').read_bytes()).hexdigest())
assert hashes[0]==hashes[1],hashes
print('Default-off AppDataFiles object byte-identical to '+a.baseline+': '+hashes[0])
source_hashes={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for folder in ('src','experimental/native_replica','sdk','test') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in ('.h','.cpp','.c','.inc','.py','.sh')}
(out/'manifest.json').write_text(json.dumps(dict(commands=commands,source_sha256=source_hashes,default_off_files=dict(baseline=a.baseline,sha256=hashes[0],byte_identical=True),compile_database_sha256=hashlib.sha256(a.compile_commands.read_bytes()).hexdigest(),artifacts={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file() and p.suffix in ('.o','.su','.nm')}),indent=2)+'\n')
print('Target assembly/PSRAM/files compilation, static sizes, dependency refusal and empty default-off object PASS; no firmware link or hardware qualification')
