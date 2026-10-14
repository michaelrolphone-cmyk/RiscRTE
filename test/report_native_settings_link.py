#!/usr/bin/env python3
"""Static linked layout and pre-startup heap-region ceilings; never runtime free heap."""
import argparse,hashlib,json,pathlib,struct,subprocess
from elftools.elf.elffile import ELFFile
p=argparse.ArgumentParser();p.add_argument('elf',type=pathlib.Path);p.add_argument('--baseline',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
def inspect(path):
 with path.open('rb') as f:
  e=ELFFile(f);symbols={s.name:s for s in e.get_section_by_name('.symtab').iter_symbols()};sections=[s for s in e.iter_sections() if s['sh_flags']&2]
  def data(addr,n):
   for s in sections:
    if s['sh_type']!='SHT_NOBITS' and s['sh_addr']<=addr and addr+n<=s['sh_addr']+s['sh_size']:return s.data()[addr-s['sh_addr']:addr-s['sh_addr']+n]
   raise ValueError(hex(addr))
  def value(s):return symbols[s]['st_value']
  def symdata(s):return data(value(s),symbols[s]['st_size'])
  def string(addr):
   b=bytearray()
   while True:
    x=data(addr,1)
    if x==b'\0':return b.decode()
    b+=x;addr+=1
  types=[{'name':string(n),'caps':c1|c2|c3,'aliased_iram':bool(ai),'startup_stack':bool(ss)} for n,c1,c2,c3,ai,ss in struct.iter_unpack('<IIIIBB2x',symdata('soc_memory_types'))]
  reserved=list(struct.iter_unpack('<II',data(value('soc_reserved_memory_region_start'),value('soc_reserved_memory_region_end')-value('soc_reserved_memory_region_start'))))
  regions=[]
  for start,size,t,alias in struct.iter_unpack('<IIII',symdata('soc_memory_regions')):
   pieces=[(start,start+size)]
   for lo,hi in reserved:
    next=[]
    for b,z in pieces:
     if b<hi and z>lo:
      if b<lo:next.append((b,lo))
      if z>hi:next.append((hi,z))
     else:next.append((b,z))
    pieces=next
   typ=types[t];regions.append(dict(start=start,size=size,type=typ,alias=alias,unreserved=[dict(start=b,end=z,bytes=z-b) for b,z in pieces]))
  eligible=[r for r in regions if r['type']['caps']&(1<<11) and r['type']['caps']&(1<<2)]
  statics=[{'name':s.name,'address':s['sh_addr'],'bytes':s['sh_size']} for s in sections if s['sh_size']]
  controls=[{'name':s.name,'address':s['st_value'],'bytes':s['st_size']} for s in symbols.values() if s['st_size'] and s['st_info']['type']=='STT_OBJECT' and any(x in s.name for x in ['NativeSettings','RiscAppDataMount'])]
  required=['__wrap_'+n for n in ['esp_vfs_register','esp_vfs_unregister','esp_partition_read','esp_partition_write','esp_partition_erase_range','esp_littlefs_format','esp_littlefs_format_partition']]+['bootloader_random_enable','bootloader_random_disable','esp_fill_random']
  return dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),sections=statics,heap_start=value('_heap_start'),iram_end=value('_iram_end'),static_dram=sum(s['bytes'] for s in statics if s['name'] in ['.dram0.data','.dram0.bss']),static_iram=sum(s['bytes'] for s in statics if s['name'] in ['.iram0.vectors','.iram0.text','.iram0.data']),pre_iq_static_room=0x3fcb0000-value('_heap_start'),reserved=reserved,memory_regions=regions,internal_8bit_unreserved_ceiling=sum(x['bytes'] for r in eligible for x in r['unreserved']),settings_objects=controls,required_symbols={n:dict(address=symbols[n]['st_value'],bytes=symbols[n]['st_size'],binding=symbols[n]['st_info']['bind'],section=symbols[n]['st_shndx']) if n in symbols else None for n in required})
b=inspect(a.baseline);s=inspect(a.elf);r=dict(scope='Static section/linked-symbol and pre-startup region ceilings only. Excludes heap metadata, startup SDK/RTOS allocations, fragmentation and all runtime high-water. Negative pre-IQ room is a layout failure; GNU ld success does not override the fixed IQ proof.',baseline=b,selected=s,delta={k:s[k]-b[k] for k in ['static_dram','static_iram','heap_start','iram_end','internal_8bit_unreserved_ceiling']});a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:r[k] for k in ['scope','delta']},indent=2));print('selected static DRAM',s['static_dram'],'IQ room',s['pre_iq_static_room'],'region ceiling',s['internal_8bit_unreserved_ceiling'])
