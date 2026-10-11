#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
store="${RESIDENT_STORE:?Set RESIDENT_STORE to the frozen packaged store}"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
python="${ELF_PYTHON:-python3}"
"$python" - "$store" "$build" <<'PY'
from pathlib import Path
from elftools.elf.elffile import ELFFile
import sys,struct
store=Path(sys.argv[1]);out=Path(sys.argv[2]);files=[]
for p in sorted(store.glob('*.elf')):
 with p.open('rb') as f:
  e=ELFFile(f);d=e.get_section_by_name('.dynsym')
  if d and d.get_symbol_by_name('risc_resident_app_descriptor_v1'):files.append(str(p))
assert files
(out/'files.txt').write_text('\n'.join(files)+'\n')
p=store/'default.elf';raw=p.read_bytes()
with p.open('rb') as f:
 e=ELFFile(f);d=e.get_section_by_name('.dynsym');symbols=list(d.iter_symbols());index=next(i for i,s in enumerate(symbols) if s.name=='risc_resident_app_descriptor_v1');sym=symbols[index];section=e.get_section(sym['st_shndx']);at=d['sh_offset']+index*d['sh_entsize'];shoff=e['e_shoff'];shsize=e['e_shentsize'];count=e['e_shnum']
 for mode in ['zero-size','oversize','cross-end','below-start','outside-index','wrong-section','undefined','absolute','nonalloc','unloaded','executable']:
  b=bytearray(raw)
  if mode=='zero-size':struct.pack_into('<I',b,at+8,0)
  elif mode=='oversize':struct.pack_into('<I',b,at+8,0xffffffff)
  elif mode=='cross-end':struct.pack_into('<I',b,at+4,section['sh_addr']+section['sh_size']-8)
  elif mode=='below-start':struct.pack_into('<I',b,at+4,section['sh_addr']-1)
  elif mode=='outside-index':struct.pack_into('<H',b,at+14,count)
  elif mode=='wrong-section':struct.pack_into('<H',b,at+14,e.get_section_index('.text'))
  elif mode=='undefined':struct.pack_into('<H',b,at+14,0)
  elif mode=='absolute':struct.pack_into('<H',b,at+14,0xfff1)
  elif mode=='nonalloc':struct.pack_into('<I',b,shoff+sym['st_shndx']*shsize+8,0)
  elif mode=='unloaded':
   names=e.get_section(e['e_shstrndx']);start=names['sh_offset']+section['sh_name'];b[start+1]=ord('x')
  elif mode=='executable':struct.pack_into('<I',b,shoff+sym['st_shndx']*shsize+8,section['sh_flags']|4)
  (out/(mode+'.elf')).write_bytes(b)
 for section_name in ['.data','.data.rel.ro','.bss']:
  target=e.get_section_by_name(section_name);assert target and target['sh_size']>=32
  slot=e.get_section_index(section_name)
  b=bytearray(raw);struct.pack_into('<H',b,at+14,slot);struct.pack_into('<I',b,at+4,target['sh_addr']+16)
  for aligned in ['.data','.rodata','.data.rel.ro','.bss']:
   address=e.get_section_by_name(aligned)['sh_addr'];alignment=min(128,address & -address)
   struct.pack_into('<I',b,shoff+e.get_section_index(aligned)*shsize+32,alignment)
  (out/('object-'+section_name[1:]+'.valid')).write_bytes(b)
  struct.pack_into('<I',b,at+4,target['sh_addr']+target['sh_size']-8)
  (out/('cross-'+section_name[1:]+'.elf')).write_bytes(b)
PY
flags=(-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-sign-compare -Wno-unused-parameter -pthread
 -DELF_LOADER_VER_MAJOR=1 -DELF_LOADER_VER_MINOR=0 -DELF_LOADER_VER_PATCH=0 -DCONFIG_ELF_LOADER_LOAD_PSRAM=1 -DCONFIG_ELF_LOADER_CACHE_OFFSET=1
 '-DpdMS_TO_TICKS(ms)=((TickType_t)(ms))' -I"$repo/test/support/native_registry/stubs" -I"$repo/test/performance_loader_stubs" -I"$repo/lib/elf_loader/include")
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
cc "${flags[@]}" -c "${ELF_READER_SOURCE:-$repo/lib/elf_loader/src/esp_elf.c}" -o "$build/loader.o"
for source in esp_elf_validate dlso/dlfcn dlso/dlmod;do
 cc "${flags[@]}" -include "$repo/test/support/native_registry/redirect.h" -c "$repo/lib/elf_loader/src/$source.c" -o "$build/$(basename "$source").o"
done
cc "${flags[@]}" -include "$repo/test/support/native_registry/redirect.h" "$repo/test/resident_object_export_test.c" "$build/loader.o" "$build/esp_elf_validate.o" "$build/dlfcn.o" "$build/dlmod.o" -o "$build/test"
mapfile -t files < "$build/files.txt"
"$build/test" "${EXPECT_OBJECT_EXPORT:-1}" packaged "${files[@]}"
if [[ "${EXPECT_OBJECT_EXPORT:-1}" == 1 ]];then
 "$build/test" 0 malformed "$build"/*.elf
 "$build/test" 2 data-objects "$build"/*.valid
fi
