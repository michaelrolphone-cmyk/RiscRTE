#!/usr/bin/env python3
"""Non-runnable relocation probe: bind seven external API refs to final ELF wraps."""
import argparse,hashlib,json,pathlib,subprocess
from elftools.elf.elffile import ELFFile
p=argparse.ArgumentParser();p.add_argument('manifest',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
m=json.loads(a.manifest.read_text());elf=a.manifest.parent/'firmware.elf';out=a.output.parent;out.mkdir(parents=True,exist_ok=True)
wraps=['esp_vfs_register','esp_vfs_unregister','esp_partition_read','esp_partition_write','esp_partition_erase_range','esp_littlefs_format','esp_littlefs_format_partition'];link=next(c for c in m['commands'] if '-c' not in c and c[0].endswith('g++'))
for name in wraps:assert '-Wl,--wrap='+name in link
prefix=link[0].removesuffix('g++');src=out/'wrap-probe.cpp';obj=out/'wrap-probe.o';bound=out/'wrap-probe-bound.o'
# Prototypes are intentionally opaque: this object only creates address relocations,
# is never executed, and is never linked into the measured firmware.
src.write_text('extern "C" {\n'+''.join('void '+n+'();\n' for n in wraps)+'extern void (*const native_settings_wrap_probe[])();\nvoid (*const native_settings_wrap_probe[])()={'+','.join(wraps)+'};\n}\n')
commands=[[prefix+'g++','-fdata-sections','-c',str(src),'-o',str(obj)],[prefix+'ld','-r','-R',str(elf),*['--wrap='+n for n in wraps],str(obj),'-o',str(bound)]]
for c in commands:subprocess.run(c,check=True)
with elf.open('rb') as f:
 original=ELFFile(f);symbols={s.name:s for s in original.get_section_by_name('.symtab').iter_symbols()};expected={n:symbols['__wrap_'+n]['st_value'] for n in wraps}
with bound.open('rb') as f:
 e=ELFFile(f);rows=[]
 for sec in e.iter_sections():
  if sec['sh_type']!='SHT_RELA':continue
  table=e.get_section(sec['sh_link'])
  for r in sec.iter_relocations():
   symbol=table.get_symbol(r['r_info_sym']);name=symbol.name
   if name.startswith('__wrap_'):
    api=name[7:];assert api in wraps and symbol['st_value']==expected[api]
    rows.append(dict(api=api,resolved_symbol=name,address=symbol['st_value'],offset=r['r_offset'],relocation_type=r['r_info_type']))
 assert len(rows)==7 and {r['api'] for r in rows}==set(wraps)
a.output.write_text(json.dumps(dict(scope='Non-runnable address-relocation probe only; final ELF is read via --just-symbols, never modified. Proves all seven identical --wrap options map external API references to the exact strong wrapper addresses in the selected firmware. Does not claim a format call occurs at runtime.',elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),commands=commands,bindings=rows),indent=2)+'\n');print('Seven exact final-ELF wrap address bindings PASS')
