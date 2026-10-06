#!/usr/bin/env python3
"""Fail-closed final Xtensa ELF proof for the opt-in fixed IQ SRAM bank."""
import argparse, hashlib, io, json, struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
BANK=(0x3FCB0000,0x3FCC0000)
ALIAS=(0x403A0000,0x403B0000)
def require(ok,message):
 if not ok: raise ValueError(message)
def overlap(start,size,region):
 return bool(size and start<region[1] and start+size>region[0])
def prove(data):
 require(b'esp32s3-16mb-appdata-iq\0' in data,'missing IQ target identity')
 e=ELFFile(io.BytesIO(data))
 require(e.elfclass==32 and e.little_endian and e['e_machine']=='EM_XTENSA','not an ELF32 LE Xtensa image')
 symbols={s.name:s for s in e.get_section_by_name('.symtab').iter_symbols()}
 def value(name):
  require(name in symbols and symbols[name]['st_shndx']!='SHN_UNDEF','missing linked symbol '+name)
  return symbols[name]['st_value']
 checked=[]
 for s in e.iter_sections():
  if s['sh_flags']&2 and s['sh_size']:
   require(not any(overlap(s['sh_addr'],s['sh_size'],r) for r in (BANK,ALIAS)),'allocated section overlaps IQ bank: '+s.name)
   checked.append({'name':s.name,'address':s['sh_addr'],'bytes':s['sh_size']})
 for p in e.iter_segments():
  if p['p_type']=='PT_LOAD':
   for key in ('p_vaddr','p_paddr'):
    require(not any(overlap(p[key],p['p_memsz'],r) for r in (BANK,ALIAS)),'load segment overlaps IQ bank')
 start,end=value('soc_reserved_memory_region_start'),value('soc_reserved_memory_region_end')
 require(end>start and (end-start)%8==0,'invalid reserved memory table')
 def bytes_at(addr,size):
  for s in e.iter_sections():
   if s['sh_type']!='SHT_NOBITS' and s['sh_addr']<=addr and addr+size<=s['sh_addr']+s['sh_size']:
    return s.data()[addr-s['sh_addr']:addr-s['sh_addr']+size]
  raise ValueError('reservation table not backed by ELF bytes')
 reserved=list(struct.iter_unpack('<II',bytes_at(start,end-start)))
 require(reserved.count(BANK)==1,'missing or duplicate pre-heap bank reservation')
 require(value('_heap_start')<=BANK[0] and value('_iram_end')<=ALIAS[0],'static end reaches IQ bank')
 require(value('_rom_chip_id')==0x40000570 and value('_rom_eco_version')==0x40000574,'unreviewed ROM identity map')
 for name,address in {'rom_i2c_readReg':0x40005d48,'rom_i2c_writeReg':0x40005d60,'rom_pbus_rd':0x40005df0,'ets_delay_us':0x40000600}.items():
  require(value(name)==address,'unreviewed ROM entry '+name)
 require(any('reserved_region_risc_radio_iq' in n and s['st_value']>=start and s['st_value']+s['st_size']<=end for n,s in symbols.items()),'named reservation is outside startup table')
 return {'schema':1,'bank_base':BANK[0],'bank_bytes':BANK[1]-BANK[0],'iram_alias':ALIAS[0],
  'heap_start':value('_heap_start'),'iram_end':value('_iram_end'),'reservation_table_address':start,
  'reservation_table':reserved,'allocated_sections':checked,'elf_sha256':hashlib.sha256(data).hexdigest(),
  'scope':'Pre-heap reservation and final linked static-memory proof only; no hardware or RF qualification.'}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('elf',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
 result=json.dumps(prove(a.elf.read_bytes()),indent=2,sort_keys=True)+'\n'
 if a.output:a.output.write_text(result)
 else:print(result,end='')
