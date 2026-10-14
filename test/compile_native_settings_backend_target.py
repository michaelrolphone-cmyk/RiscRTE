#!/usr/bin/env python3
"""Compile-only pinned native settings backend, entrypoint and default-off parity."""
import argparse,hashlib,json,pathlib,shlex,subprocess,shutil
p=argparse.ArgumentParser(description=__doc__);p.add_argument('compile_commands',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
entry=next(e for e in json.loads(a.compile_commands.read_text()) if e['file'].endswith('/Runtime.cpp'));command=shlex.split(entry['command']);tool=command[0];flags=[];skip=False
for arg in command[1:]:
 if skip:skip=False;continue
 if arg=='-o':skip=True;continue
 if arg in ('-c','-MMD') or arg.endswith('/Runtime.cpp'):continue
 if arg.startswith('-I') and not pathlib.Path(arg[2:]).is_absolute():arg='-I'+str(root/arg[2:])
 if arg.startswith('-ffile-prefix-map='):arg=f'-ffile-prefix-map={root}=.'
 flags.append(arg)
flags=['-I'+str(root/'src'),'-I'+str(root),'-I'+str(root/'sdk/app'),'-I'+str(root/'sdk/driver'),'-I'+str(root/'sdk/hardware'),*flags,'-fstack-usage','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-Wno-misleading-indentation']
selected=['-DBOARD_XTEINK_X4_PRO','-DRISC_PAIRED_APP_DATA','-DRISC_PAIRED_BANKS','-DRISC_NATIVE_SETTINGS_BACKEND=1','-DRISC_NATIVE_SETTINGS_STARTUP_ENTROPY=1','-DRISC_NATIVE_APP_DATA_MOUNT=1','-DRISC_NATIVE_SETTINGS_ASSEMBLY=1','-DRISC_NATIVE_SETTINGS_PSRAM_PAYLOAD=1','-DRISC_NATIVE_SETTINGS_LIFECYCLE=1','-DRISC_NATIVE_SETTINGS_WORKER=1','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1','-DRISC_NATIVE_STORAGE_CUSTODY=1']
identities=list(pathlib.Path(entry['directory']).glob('.pio/build/*/RiscBuildIdentity.h'));assert len(identities)==1,identities
identity=identities[0];flags+=['-I'+str(identity.parent)]
commands=[]
def run(cmd):commands.append(cmd);subprocess.run(cmd,cwd=root,check=True)
probe=out/'backend-layout.cpp';probe.write_text('#include "'+str(root/'src/ports/esp32s3/NativeSettingsBackend.cpp')+'"\nextern "C" { char settings_backend_controls[sizeof(RiscCpu::NativeSettingsBackend::state)]; char settings_namespace_backend[sizeof(RiscCpu::NativeSettingsBackend::Backend)]; }\n')
for source,name,extra in [(root/'src/ports/esp32s3/NativeSettingsStartupEntropy.cpp','entropy',selected),(root/'src/ports/esp32s3/NativeSettingsStartupEntropy.cpp','entropy-off',[]),(root/'src/ports/esp32s3/NativeSettingsBackend.cpp','backend',selected),(probe,'layout',selected),(root/'src/main.cpp','main',selected),(root/'src/ports/esp32s3/NativeSettingsBackend.cpp','backend-off',[])]:
 run([tool,*flags,*extra,'-c',str(source),'-o',str(out/(name+'.o'))]);nm=subprocess.check_output([tool.removesuffix('g++')+'nm','-S',str(out/(name+'.o'))],text=True);(out/(name+'.nm')).write_text(nm)
 if name in ('backend-off','entropy-off'):assert not nm.strip()
 for line in nm.splitlines():
  if ' settings_' in line:print(line)
# Same path/flags excludes filename noise. Default-off entrypoint remains exact.
parity=out/'main-parity.cpp';hashes=[]
for old in (True,False):
 parity.write_bytes(subprocess.check_output(['git','show','2d362e8958f2938a3b6ad290b2cb2c75744d8f83:src/main.cpp'],cwd=root) if old else (root/'src/main.cpp').read_bytes())
 run([tool,*flags,'-c',str(parity),'-o',str(out/'main-default-off.o')]);name='main-old' if old else 'main-new';shutil.copyfile(out/'main-default-off.o',out/(name+'-with-debug.o'));run([tool.removesuffix('g++')+'objcopy','--strip-debug',str(out/'main-default-off.o'),str(out/(name+'.o'))]);hashes.append(hashlib.sha256((out/(name+'.o')).read_bytes()).hexdigest())
assert hashes[0]==hashes[1],hashes
sources={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for d in ('src','sdk','experimental/native_replica','test') for p in (root/d).rglob('*') if p.is_file() and p.suffix in ('.h','.cpp','.c','.py','.sh')}
(out/'manifest.json').write_text(json.dumps(dict(commands=commands,source_sha256=sources,compile_database_sha256=hashlib.sha256(a.compile_commands.read_bytes()).hexdigest(),build_identity=dict(path=str(identity),sha256=hashlib.sha256(identity.read_bytes()).hexdigest()),default_off_main_without_debug_sha256=hashes[0],artifacts={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file() and p.suffix in ('.o','.su','.nm')}),indent=2)+'\n')
print('PASS: target backend/entrypoint, exact default-off entrypoint after stripping debug line metadata, empty backend-off; no link/hardware qualification')
