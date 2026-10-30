#!/usr/bin/env python3
"""Verify actual SDK cache-probe linkage; never infer target execution."""
import argparse,hashlib,json,shlex,shutil,struct,subprocess
from pathlib import Path
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def require_linked_calls(symbols, disassembly, sdkcalls, nativecalls):
 # GCC emits callback lambdas separately and may alias equivalent thunks.
 # Qualify actual call instructions, not symbol-header text or family size.
 import re
 def calls(name,target):
  return bool(re.search(r"\bcall(?:0|4|8|12)\s+[^\n]*<"+re.escape(target)+r">",disassembly[name]))
 assert len(sdkcalls)==1 and calls(sdkcalls[0],'__wrap_esp_cache_msync'), 'No demonstrated SDK wrap call'
 hits={symbols[name][0] for name in nativecalls if calls(name,'__wrap_esp_cache_msync')}
 assert len(hits)==1, 'Expected exactly one native writeback-family containing-function address with a wrap call'
 assert calls('__wrap_esp_cache_msync','esp_cache_msync'), 'Wrapper must forward actual SDK operations'
 return sorted(hits)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=a.runtime.resolve();sdk=a.sdk.resolve();build=a.build.resolve()
 commit=subprocess.check_output(['git','-C',root,'rev-parse','HEAD'],text=True).strip();sdkcommit=subprocess.check_output(['git','-C',sdk,'rev-parse','HEAD'],text=True).strip()
 pin=json.loads((root/'qualification/psram-cache-sdk.json').read_text());assert sdkcommit==pin['commit']=='fff9895c82d744c7237be8847347bdd1b07c6643'
 for f in pin['files']:assert digest(sdk/f['path'])==f['sha256'],f['path']
 config=(build/'config/sdkconfig.h').read_text()
 for flag in ['CONFIG_IDF_TARGET_ESP32S3','CONFIG_SPIRAM_FETCH_INSTRUCTIONS','CONFIG_SPIRAM_RODATA']:assert '#define '+flag+' 1' in config,flag
 wanted={'runtime/test/native_psram_cache_target/main/probe.cpp','sdk/components/esp_driver_dma/src/async_memcpy_gdma.c','sdk/components/esp_mm/esp_cache_msync.c'}
 inputs={};objects={};commands=[];compiler=None
 for entry in json.loads((build/'compile_commands.json').read_text()):
  source=Path(entry['file']);source=(source if source.is_absolute() else Path(entry['directory'])/source).resolve()
  key=next((label+'/'+str(source.relative_to(base)) for label,base in [('runtime',root),('sdk',sdk)] if source.is_relative_to(base)),None)
  if key not in wanted:continue
  args=entry.get('arguments') or shlex.split(entry['command']);cc=Path(shutil.which(args[0]) or args[0]).resolve();compiler=cc;toolroot=cc.parents[1]
  scan=[];obj=None;i=0
  while i<len(args):
   if args[i]=='-o':obj=Path(entry['directory'])/args[i+1];i+=2;continue
   if args[i] in ['-MF','-MT','-MQ']:i+=2;continue
   if args[i] in ['-c','-MD','-MMD','-MP']:i+=1;continue
   scan.append(args[i]);i+=1
  assert obj and obj.is_file();objects[key]={'bytes':obj.stat().st_size,'sha256':digest(obj)};commands.append(scan)
  raw=subprocess.check_output([*scan,'-M','-MT','dependencies'],cwd=entry['directory'],text=True)
  for name in shlex.split(raw.split(':',1)[1].replace('\\\n',' ')):
   path=Path(name);path=(path if path.is_absolute() else Path(entry['directory'])/path).resolve()
   label=next((label+'/'+str(path.relative_to(base)) for label,base in [('runtime',root),('sdk',sdk),('generated',build),('toolchain',toolroot)] if path.is_relative_to(base)),None)
   assert label,'Unqualified external target dependency: '+str(path)
   assert '/psram_cache_shim/' not in label and '/psram_cache_probe_shim/' not in label
   inputs[label]=digest(path)
 assert set(objects)==wanted
 for path in ['runtime/src/ports/esp32s3/NativePsramCache.h','runtime/src/runtime/memory/PsramCache.h','runtime/sdk/driver/RiscPsramCacheV1.h']:assert path in inputs,path
 elf=build/'native_psram_cache_probe.elf';data=elf.read_bytes();assert commit.encode() in data;assert data[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<HH',data,16)==(2,94)
 prefix=compiler.name.removesuffix('g++').removesuffix('gcc');nm=compiler.with_name(prefix+'nm');objdump=compiler.with_name(prefix+'objdump')
 raw=subprocess.check_output([str(nm),'-S',str(elf)],text=True);symbols={}
 for line in raw.splitlines():
  fields=line.split()
  if len(fields)==4:symbols[fields[3]]=(int(fields[0],16),int(fields[1],16),fields[2])
 assert '__wrap_esp_cache_msync' in symbols and 'esp_cache_msync' in symbols
 assert 'Cache_WriteBack_Addr' in symbols and symbols['Cache_WriteBack_Addr'][2] in ['T','t']
 assert 'cache_writeback_items_freeze' in symbols
 sdkcalls=[name for name in symbols if name.startswith('mcp_gdma_memcpy')];nativecalls=[name for name in symbols if 'NativePsramCache' in name and 'writeback' in name]
 assert len(sdkcalls)==1 and '_ZN7RiscCpu16NativePsramCache9writebackEPvjj' in nativecalls,(sdkcalls,nativecalls)
 disassembly={}
 for name in [*sdkcalls,*nativecalls,'__wrap_esp_cache_msync']:
  address,length,_=symbols[name];assert length>0
  text=subprocess.check_output([str(objdump),'-d','--start-address='+str(address),'--stop-address='+str(address+length),str(elf)],text=True);disassembly[name]=text
 a.output.parent.mkdir(parents=True,exist_ok=True)
 a.output.with_suffix('.disassembly.json').write_text(json.dumps({'elf_sha256':digest(elf),'symbols':{name:symbols[name] for name in disassembly},'disassembly':disassembly},indent=2)+'\n')
 native_wrap_function_addresses=require_linked_calls(symbols,disassembly,sdkcalls,nativecalls)
 globals_needed=['completed','suppressSourceFlush']
 for suffix in globals_needed:
  matches=[v for name,v in symbols.items() if 'GLOBAL__N_' in name and name.endswith(str(len(suffix))+suffix+'E')]
  assert len(matches)==1 and 0x3fc00000<=matches[0][0]<0x3fd00000,'Atomic probe state outside internal DRAM'
 receipt=dict(runtime_commit=commit,sdk_commit=sdkcommit,compiler={'sha256':digest(compiler),'version':subprocess.check_output([str(compiler),'--version'],text=True)},objects=objects,inputs=dict(sorted(inputs.items())),commands=commands,sdk_audit_sha256=digest(root/'qualification/psram-cache-sdk.json'),disassembly=disassembly,native_wrap_function_addresses=native_wrap_function_addresses,elf={'bytes':len(data),'sha256':digest(elf)},binary={'bytes':(build/'native_psram_cache_probe.bin').stat().st_size,'sha256':digest(build/'native_psram_cache_probe.bin')},sdkconfig_sha256=digest(build/'config/sdkconfig.h'),hardware_tested=False,target_execution=False,dma_visibility='unobserved',cleanup_qualified=False)
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(receipt,indent=2)+'\n');print('Actual SDK/probe source, XIP config and linked flush interception verified; target execution and DMA visibility unobserved')
if __name__=='__main__':main()
