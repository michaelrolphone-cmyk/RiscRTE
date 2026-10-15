#!/usr/bin/env python3
"""Compile real feature-enabled storage/settings sources against SDK6.1/GCC15.

Consumes the configured native compile database. No SDK/RTOS host substitutes,
firmware linking, flashing, device I/O or product activation.
"""
import argparse, hashlib, importlib.util, json, pathlib, shlex, subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('compile_commands',type=pathlib.Path)
p.add_argument('output',type=pathlib.Path)
p.add_argument('--profile',type=pathlib.Path,default=pathlib.Path('waveshare-settings-profile.h'))
a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1]
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
database=a.compile_commands.read_bytes()
entry=next(e for e in json.loads(database) if e['file'].endswith('/Runtime.cpp'))
cmd=shlex.split(entry['command']);cxx=cmd[0];flags=[];skip=False
for arg in cmd[1:]:
 if skip:skip=False;continue
 if arg in ('-o','-MF','-MT','-MQ'):skip=True;continue
 if arg in ('-c','-MD','-MMD') or arg==entry['file']:continue
 flags.append(arg)
version=subprocess.check_output([cxx,'--version'],text=True).splitlines()[0]
assert '15.2.0' in version,version
pin=root/'experimental/native_replica/pinned_littlefs'
spec=importlib.util.spec_from_file_location('pin',pin/'prepare_idf6.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
vendor=out/'vendor';mod.prepare(vendor)
config=out/'storage-sdkconfig.h'
config.write_text('''#include <sdkconfig.h>
#define CONFIG_LITTLEFS_MAX_PARTITIONS 3
#define CONFIG_LITTLEFS_PAGE_SIZE 256
#define CONFIG_LITTLEFS_OBJ_NAME_LEN 64
#define CONFIG_LITTLEFS_READ_SIZE 128
#define CONFIG_LITTLEFS_WRITE_SIZE 128
#define CONFIG_LITTLEFS_CACHE_SIZE 512
#define CONFIG_LITTLEFS_LOOKAHEAD_SIZE 128
#define CONFIG_LITTLEFS_BLOCK_CYCLES 512
#define CONFIG_LITTLEFS_USE_MTIME 1
#define CONFIG_LITTLEFS_MTIME_USE_SECONDS 1
#define CONFIG_LITTLEFS_MALLOC_STRATEGY_DEFAULT 1
#define CONFIG_LITTLEFS_ASSERTS 1
''')
sdk=next(pathlib.Path(f[2:]).parent.parent for f in flags if f.startswith('-I') and f.endswith('/esp_common/include'))
sdk_commit=subprocess.check_output(['git','-C',str(sdk.parent),'rev-parse','HEAD'],text=True).strip()
assert sdk_commit=='fff9895c82d744c7237be8847347bdd1b07c6643',sdk_commit
flags+=['-I'+str(sdk/'spi_flash/include'),'-I'+str(root),'-I'+str(vendor),'-include',str(config),'-fstack-usage','-Werror','-Wno-misleading-indentation']
profile=a.profile.resolve();selected=['-include',str(profile),'-DRISC_PAIRED_APP_DATA','-DRISC_PAIRED_BANKS','-DRISC_NATIVE_APP_DATA_MOUNT=1','-DRISC_NATIVE_SETTINGS_BACKEND=1','-DRISC_NATIVE_SETTINGS_STARTUP_ENTROPY=1','-DRISC_NATIVE_SETTINGS_ASSEMBLY=1','-DRISC_NATIVE_SETTINGS_PSRAM_PAYLOAD=1','-DRISC_NATIVE_SETTINGS_LIFECYCLE=1','-DRISC_NATIVE_SETTINGS_WORKER=1','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1','-DRISC_NATIVE_STORAGE_CUSTODY=1','-DRISC_NATIVE_PACKAGE_VOLUME=1','-DRISC_LITTLEFS_IDF6_MTIME32=1']
commands=[];artifacts=[];failures=[]
def run(args):
 commands.append(args)
 name=pathlib.Path(args[args.index('-o')+1]).name
 r=subprocess.run(args,cwd=entry['directory'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 (out/(name+'.log')).write_text(r.stdout)
 if r.returncode:
  print('FAIL',name,flush=True);print(r.stdout);failures.append(name);return
 print('PASS',name,flush=True);artifacts.append(name)
cflags=[f.replace('/cxxflags', '/cflags') for f in flags if not f.startswith('-std=') and f not in ('-fno-rtti','-fno-exceptions','-fuse-cxa-atexit','-Wno-invalid-offset','-Wno-invalid-offset-of')]
for name in ('littlefs/lfs.c','littlefs/lfs_util.c','littlefs_esp_part.c','esp_littlefs.c','lfs_config.c'):
 run([cxx.removesuffix('g++')+'gcc',*cflags,'-std=gnu17','-DLFS_CONFIG=lfs_config.h','-include',str(root/'idf/components/risc_littlefs/profile_config.h'),'-c',str(vendor/name),'-o',str(out/(pathlib.Path(name).name+'.o'))])
vendor_objects=[out/n for n in ('lfs.c.o','lfs_util.c.o','littlefs_esp_part.c.o','esp_littlefs.c.o','lfs_config.c.o')]
if not failures:
 run([cxx.removesuffix('g++')+'gcc','-nostdlib','-r',*map(str,vendor_objects),'-o',str(out/'littlefs-native-relocatable.o')])
for source in sorted((root/'src/ports/esp32s3').glob('NativeSettings*.cpp'))+sorted((root/'src/ports/esp32s3').glob('NativeAppData*.cpp')):
 run([cxx,*flags,*selected,'-c',str(source),'-o',str(out/(source.stem+'.o'))])
if 'NativeAppDataMount.o' in artifacts:
 symbols=subprocess.check_output([cxx.removesuffix('g++')+'objdump','-t','-C',str(out/'NativeAppDataMount.o')],text=True)
 (out/'mount-atomic-sections.log').write_text(symbols)
 for name in ('failed','owner'):
  matches=[line for line in symbols.splitlines() if line.endswith('RiscAppDataMount::(anonymous namespace)::'+name)]
  assert len(matches)==1 and '.dram' in matches[0],(name,matches)
 print('PASS mount control atomics in DRAM',flush=True)
probe=out/'target-abi.cpp';probe.write_text('''#include <time.h>
#include <sys/stat.h>
#include "experimental/native_replica/pinned_littlefs/littlefs_api.h"
static_assert(sizeof(time_t)==8,"native libc time_t must remain64");
static_assert(LFS_VERSION==0x00020009 && LFS_DISK_VERSION==0x00020001,"core/disk pin");
extern "C" { char storage_time_bytes[sizeof(time_t)]; char storage_stat_bytes[sizeof(struct stat)]; char storage_private_bytes[sizeof(esp_littlefs_t)]; }
''')
run([cxx,*flags,*selected,'-c',str(probe),'-o',str(out/'target-abi.o')])
# Negative controls prove source/config mismatches cannot silently select a
# different disk/private-layout contract.
def refuse(name,args,reason):
 r=subprocess.run(args,cwd=entry['directory'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 (out/(name+'.log')).write_text(r.stdout);commands.append(args)
 if not r.returncode or reason not in r.stdout:raise SystemExit('Refusal gate failed: '+name)
 print('PASS refusal',name,flush=True)
bad=out/'wrong-geometry.h';bad.write_text('#undef CONFIG_LITTLEFS_READ_SIZE\n#define CONFIG_LITTLEFS_READ_SIZE 64\n')
refuse('wrong-geometry',[cxx.removesuffix('g++')+'gcc',*cflags,'-std=gnu17','-DLFS_CONFIG=lfs_config.h','-include',str(bad),'-include',str(root/'idf/components/risc_littlefs/profile_config.h'),'-c',str(vendor/'esp_littlefs.c'),'-o',str(out/'refused-geometry.o')],'requires the shipped LittleFS geometry')
refuse('unqualified-mtime',[cxx,*flags,*selected,'-URISC_LITTLEFS_IDF6_MTIME32','-c',str(root/'src/ports/esp32s3/NativeAppDataMount.cpp'),'-o',str(out/'refused-mtime.o')],'requires the qualified LittleFS mtime32 component')
manifest=dict(sdk_commit=sdk_commit,compiler=version,compile_database_sha256=hashlib.sha256(database).hexdigest(),profile=str(profile),profile_sha256=hashlib.sha256(profile.read_bytes()).hexdigest(),commands=commands,artifacts={n:hashlib.sha256((out/n).read_bytes()).hexdigest() for n in artifacts})
manifest['source_sha256']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for folder in ('src/ports/esp32s3','experimental/native_replica','idf/components/risc_littlefs') for p in (root/folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
if failures:raise SystemExit('Failed objects: '+', '.join(failures))
print('PASS: actual SDK6.1 feature-enabled storage objects; no firmware/hardware claim')
