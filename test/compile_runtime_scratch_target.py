#!/usr/bin/env python3
"""Compile current scratch source against configured native IDF6.1/GCC15 inputs."""
import argparse,gzip,hashlib,json,pathlib,shlex,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('commands',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
with (gzip.open if a.commands.suffix=='.gz' else open)(a.commands,'rt') as f:rows=json.load(f)
row=next(r for r in rows if r['file'].endswith('/src/ports/esp32s3/NativeFileSource.cpp'))
old=row['file'].removesuffix('/src/ports/esp32s3/NativeFileSource.cpp');args=shlex.split(row['command']);flags=[];skip=False
for value in args[1:]:
 if skip:skip=False;continue
 if value in ('-o','-MF','-MT','-MQ'):skip=True;continue
 if value in ('-c','-MD','-MMD') or value==row['file']:continue
 if old in value and old+'/idf/' not in value:value=value.replace(old,str(root))
 flags.append(value)
compiler=args[0];version=subprocess.check_output([compiler,'--version'],text=True).splitlines()[0];assert '15.2.0' in version
sdk=next(pathlib.Path(f[2:]).parent.parent.parent for f in flags if f.startswith('-I') and f.endswith('/esp_common/include'))
pin=subprocess.check_output(['git','-C',str(sdk),'rev-parse','HEAD'],text=True).strip();assert pin=='fff9895c82d744c7237be8847347bdd1b07c6643'
commands=[]
for label,source,enabled in [('native-on','src/ports/esp32s3/NativeFileSource.cpp',True),('native-off','src/ports/esp32s3/NativeFileSource.cpp',False),('source-layout','test/native_file_source_target_probe.cpp',True)]:
 command=[compiler,*flags,'-fstack-usage','-Werror','-Wno-misleading-indentation','-URISC_NATIVE_FILE_SOURCE','-DRISC_NATIVE_FILE_SOURCE='+str(int(enabled)),'-c',str(root/source),'-o',str(out/(label+'.o'))]
 commands.append(command);subprocess.run(command,cwd=row['directory'],check=True);print('PASS',label,flush=True)
nm=compiler.removesuffix('g++')+'nm';assert not subprocess.check_output([nm,str(out/'native-off.o')],text=True).strip()
(out/'manifest.json').write_text(json.dumps(dict(sdk_commit=pin,compiler=version,commands=commands,sources={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'src/runtime/storage/FileSourceFiles.h',root/'src/ports/esp32s3/NativeFileSource.cpp',root/'src/ports/esp32s3/NativeScratchFiles.h']},objects={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.o')}),indent=2)+'\n')
print('PASS: native-on and symbol-free native-off; source and includes rebased to current checkout')
