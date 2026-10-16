#!/usr/bin/env python3
"""Compile actual metadata consumers using a configured pinned-IDF command database.

No firmware link, flash or hardware qualification. Existing generated SDK headers
and product profile are reused; source/include paths are redirected to this tree.
"""
import argparse,gzip,json,pathlib,shlex,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('commands',type=pathlib.Path);p.add_argument('output',type=pathlib.Path)
p.add_argument('--storage',choices=['0','1'],default='1')
p.add_argument('--embedded',action='store_true')
p.add_argument('--elf',action='store_true',help='also compile the provider graph and actual loader')
a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1];a.output.mkdir(parents=True,exist_ok=True)
open_db=gzip.open if a.commands.suffix=='.gz' else open
with open_db(a.commands,'rt') as f:rows=json.load(f)
if a.embedded:(a.output/'RiscEmbeddedStore.h').write_text((root/'test/embedded_store_stubs/RiscEmbeddedStore.h').read_text())
units=['src/bootstrap/Json.cpp','src/bootstrap/Runtime.cpp','src/ports/esp32s3/NativeBootstrap.cpp','src/ports/esp32s3/NativeBankStore.cpp','src/main.cpp']
if a.embedded:units+=['src/bootstrap/EmbeddedStore.cpp']
if a.elf:units+=['src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp','lib/elf_loader/src/esp_elf.c','lib/elf_loader/src/dlso/dlfcn.c','lib/elf_loader/src/dlso/dlmod.c']
for relative in units:
 row=next(r for r in rows if r['file'].endswith('/'+relative));original=row['file'][:-len(relative)-1]
 args=shlex.split(row['command']);command=[];skip=False
 for value in args:
  if skip:skip=False;continue
  if value=='-o':skip=True;continue
  # Retain the configured build's generated SDK headers/response files; every
  # Runtime source, SDK and bundled-library header comes from the current tree.
  if original in value and original+'/idf/' not in value:value=value.replace(original,str(root))
  command.append(value)
 command+=['-URISC_RUNTIME_PREFERENCE_STORE','-URISC_RUNTIME_STORAGE_FILES','-DRISC_RUNTIME_PREFERENCE_STORE='+a.storage,'-DRISC_RUNTIME_STORAGE_FILES='+a.storage,'-o',str((a.output/(pathlib.Path(relative).stem+'.o')).resolve())]
 if a.embedded:
  # Embedded storage is a separate assembly: paired-bank/app-data services and
  # their settings/scratch consumers cannot be inherited from a product probe.
  settings={value[2:].split('=')[0] for value in command if value.startswith('-DRISC_NATIVE_SETTINGS_')}
  for setting in sorted(settings):command+=['-U'+setting,'-D'+setting+'=0']
  command+=['-URISC_PAIRED_BANKS','-URISC_PAIRED_APP_DATA','-URISC_PAIRED_APP_DATA_EXPANDED','-URISC_NATIVE_FILE_SOURCE','-DRISC_NATIVE_FILE_SOURCE=0','-DRISC_EMBEDDED_BOOTSTORE','-I'+str(a.output.resolve())]
 print('Compile actual native translation unit:',relative,flush=True)
 subprocess.run(command,cwd=row['directory'],check=True)
 print('PASS:',relative,flush=True)
