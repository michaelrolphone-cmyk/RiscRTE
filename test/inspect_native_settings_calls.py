#!/usr/bin/env python3
"""Resolve target call instructions/literals and ENTRY frames; incomplete call graph."""
import argparse,bisect,json,pathlib,re,subprocess,struct,tempfile
from elftools.elf.elffile import ELFFile
p=argparse.ArgumentParser();p.add_argument('elf',type=pathlib.Path);p.add_argument('--tool-prefix',required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
with a.elf.open('rb') as f:
 e=ELFFile(f);secs=[s for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']!='SHT_NOBITS'];syms=[s for s in e.get_section_by_name('.symtab').iter_symbols() if s['st_size'] and s['st_info']['type']=='STT_FUNC'];nodes={}
 names=subprocess.check_output([a.tool_prefix+'c++filt'],input='\n'.join(s.name for s in syms),text=True).splitlines()
 for s,name in zip(syms,names):
  addr=s['st_value']
  if addr not in nodes or s['st_size']>nodes[addr]['size']:nodes[addr]={'symbol':s.name,'name':name,'size':s['st_size'],'frame':None,'calls':[],'unknown_calls':[]}
 addrs=sorted(nodes)
 def read32(addr):
  for s in secs:
   if s['sh_addr']<=addr and addr+4<=s['sh_addr']+s['sh_size']:return struct.unpack_from('<I',s.data(),addr-s['sh_addr'])[0]
  return None
 # Some merged Xtensa .xt.prop ranges classify surviving code as literal data.
 # Decode a temporary stripped view, after checking executable bytes and VMAs.
 with tempfile.TemporaryDirectory() as td:
  view=pathlib.Path(td)/'decode.elf'
  subprocess.run([a.tool_prefix+'objcopy','--strip-debug','--remove-section=.xt.prop','--remove-section=.xt.lit',str(a.elf),str(view)],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  with view.open('rb') as vf:
   ve=ELFFile(vf)
   for sec in secs:
    if sec['sh_flags']&4:
     other=ve.get_section_by_name(sec.name)
     assert other is not None and other['sh_addr']==sec['sh_addr'] and other.data()==sec.data(),sec.name
  out=subprocess.check_output([a.tool_prefix+'objdump','-d',str(view)],text=True)
 prev=None
 for l in out.splitlines():
  m=re.match(r'^\s*([0-9a-f]+):\s+([0-9a-f]+)\s+(\S+)(?:\s+(.*))?$',l)
  if not m:continue
  pc=int(m[1],16);op=m[3];args=m[4] or '';i=bisect.bisect_right(addrs,pc)-1
  if i<0:continue
  start=addrs[i];n=nodes[start]
  if pc>=start+n['size']:continue
  if op=='entry':n['frame']=int(args.split(',')[1].strip(),0)
  target=None;kind=op
  if op=='call8':target=int(args.split()[0],16)
  elif op=='callx8':
   if prev and prev[0]+len(prev[1])==pc and prev[2]=='l32r' and prev[3].split(',')[0].strip()==args.strip():
    raw=prev[1];literal=((prev[0]+3)&~3)+(int.from_bytes(raw[1:],'little')-65536)*4;target=read32(literal);kind='l32r+callx8'
   if target is None:n['unknown_calls'].append(pc)
  if target is not None:n['calls'].append({'pc':pc,'target':target,'target_name':nodes.get(target,{}).get('name',hex(target)),'kind':kind})
  prev=(pc,bytes.fromhex(m[2])[::-1],op,args)
 targets=['__wrap_esp_vfs_register','__wrap_esp_vfs_unregister','__wrap_esp_partition_read','__wrap_esp_partition_write','__wrap_esp_partition_erase_range','__wrap_esp_littlefs_format','__wrap_esp_littlefs_format_partition','bootloader_random_enable','bootloader_random_disable','esp_fill_random']
 incoming={t:[] for t in targets}
 for start,n in nodes.items():
  for c in n['calls']:
   sym=nodes.get(c['target'],{}).get('symbol')
   if sym in incoming:incoming[sym].append({'caller':n['name'],'caller_address':start,**c})
 selected={hex(k):v for k,v in nodes.items() if any(x in v['name'] for x in ['NativeSettings','NativeReplica','AppData','littlefs','lfs_','bootloader_random','setup()','NativeSd','Checked','Storage::BasicWorker','NativeAtomic','FreshInitArtifact','__ssprint_r','snprintf','_svfprintf_r','esp_partition_read','esp_flash_read','esp_vfs_open','fenced','open','read','write','fsync','rename'])}
 result={'scope':'Executable bytes/VMAs are verified unchanged in a temporary stripped disassembly view without .xt.prop/.xt.lit. Direct CALL8 and adjacent L32R/CALLX8 only; unresolved function pointers and non-adjacent register flow are explicit omissions. ENTRY frame bytes exclude dynamic stack and interrupt/window-spill overhead. This is not a whole-call-chain upper bound or hardware high-water.','incoming_wrapped_and_entropy':incoming,'selected_functions':selected}
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
