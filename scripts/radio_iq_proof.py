#!/usr/bin/env python3
"""Fail-closed final Xtensa ELF proof for the opt-in fixed IQ SRAM bank."""
import argparse, hashlib, io, json, re, struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
BANK=(0x3FCB0000,0x3FCC0000)
ALIAS=(0x403A0000,0x403B0000)
ROM_ELF_SHA256='c0ce0f338d1de1bdc6efbef1591779a2a42c1ab7d759d3c6ae8ae63a7dd34cfd'
ROM_ENTRIES={'rom_i2c_readReg':0x40005d48,'rom_i2c_writeReg':0x40005d60,'rom_pbus_rd':0x40005df0,'ets_delay_us':0x40000600}
def require(ok,message):
 if not ok: raise ValueError(message)
def overlap(start,size,region):
 return bool(size and start<region[1] and start+size>region[0])
def prove_rom(data):
 """Validate SDK-pinned esp-rom-elfs/20241011/esp32s3_rev0_rom.elf.

 Its __call_* symbols identify the historical ABI entry stubs. The unprefixed
 debug symbols point at function bodies and must not replace those entrypoints.
 This is build provenance, not evidence of the ROM running on any device.
 """
 digest=hashlib.sha256(data).hexdigest()
 require(digest==ROM_ELF_SHA256,'unreviewed ESP32-S3 ROM ELF checksum')
 e=ELFFile(io.BytesIO(data))
 require(e.elfclass==32 and e.little_endian and e['e_machine']=='EM_XTENSA','not an ELF32 LE Xtensa ROM image')
 symbols={s.name:s for s in e.get_section_by_name('.symtab').iter_symbols()}
 def value(name):
  require(name in symbols and symbols[name]['st_shndx']!='SHN_UNDEF','missing ROM symbol '+name)
  return symbols[name]['st_value']
 require(value('_rom_chip_id')==0x40000570 and value('_rom_eco_version')==0x40000574,'unreviewed ROM identity map')
 for name,address in ROM_ENTRIES.items():
  require(value('__call_'+name)==address,'unreviewed ROM call stub '+name)
  require(value(name)!=address,'ROM body confused with call stub '+name)
 return {'schema':1,'rom_elf_sha256':digest,'rom_tool':'esp-rom-elfs/20241011',
  'image':'esp32s3_rev0_rom.elf','required_linker_aliases':ROM_ENTRIES.copy(),
  'call_stub_symbols':{'__call_'+name:address for name,address in ROM_ENTRIES.items()},
  'function_body_addresses':{name:value(name) for name in ROM_ENTRIES},
  'scope':'Pinned official ROM ELF entry-stub provenance only; no hardware or RF qualification.'}
def prove(data,target_identity='esp32s3-16mb-appdata-iq'):
 require(isinstance(target_identity,str) and re.fullmatch(r'[A-Za-z0-9_-]{1,63}',target_identity),'invalid IQ target identity')
 require(target_identity.encode()+b'\0' in data,'missing IQ target identity')
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
 for name,address in ROM_ENTRIES.items():
  require(value(name)==address,'unreviewed ROM entry '+name)
 require(any('reserved_region_risc_radio_iq' in n and s['st_value']>=start and s['st_value']+s['st_size']<=end for n,s in symbols.items()),'named reservation is outside startup table')
 return {'schema':1,'target_identity':target_identity,'bank_base':BANK[0],'bank_bytes':BANK[1]-BANK[0],'iram_alias':ALIAS[0],
  'heap_start':value('_heap_start'),'iram_end':value('_iram_end'),'reservation_table_address':start,
  'reservation_table':reserved,'allocated_sections':checked,'elf_sha256':hashlib.sha256(data).hexdigest(),
  'scope':'Pre-heap reservation and final linked static-memory proof only; no hardware or RF qualification.'}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('elf',type=Path);p.add_argument('--output',type=Path);p.add_argument('--target-identity',default='esp32s3-16mb-appdata-iq');a=p.parse_args()
 result=json.dumps(prove(a.elf.read_bytes(),a.target_identity),indent=2,sort_keys=True)+'\n'
 if a.output:a.output.write_text(result)
 else:print(result,end='')
