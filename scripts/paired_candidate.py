#!/usr/bin/env python3
"""Freeze exact-source paired Runtime input, never publish, partition or flash."""
import argparse, hashlib, io, json, re, shutil, struct, subprocess
from pathlib import Path
from release_assets import ROOT, file_bytes, esp_image, elf, require, head
from check_versions import firmware
from paired_bank_images import BOOTLOADER_SHA256
TARGET='esp32s3-16mb-paired'
EXPECTED={
 'nvs':(1,2,0x9000,0x6000),
 'app0':(0,0x10,0x10000,0x300000),'bootfs0':(1,0x82,0x310000,0x4f0000),
 'app1':(0,0x11,0x800000,0x300000),'bootfs1':(1,0x82,0xb00000,0x4f0000),
 'otadata':(1,0,0xff0000,0x2000),'bank_state':(1,0x40,0xff2000,0x2000)}
APP_DATA_EXPECTED={**EXPECTED, 'app0':(0,0x10,0x10000,0x260000), 'app1':(0,0x11,0x800000,0x260000), 'appdata':(1,0x41,0x270000,0x80000), 'bootfs0':(1,0x82,0x2f0000,0x510000), 'bootfs1':(1,0x82,0xae0000,0x510000)}
def partitions(data, expected=EXPECTED):
 require(len(data)==3072,'partition-table length')
 found={};raw=b'';verified=False
 for at in range(0,len(data),32):
  row=data[at:at+32];magic=struct.unpack_from('<H',row)[0]
  if magic==0x50aa:
   _,kind,subtype,offset,size,label,flags=struct.unpack('<HBBII16sI',row)
   label=label.split(b'\0')[0].decode();require(label not in found and flags==0,'partition alias/flags')
   found[label]=(kind,subtype,offset,size);raw+=row
  elif magic==0xebeb:
   require(row[16:]==hashlib.md5(raw).digest(),'partition MD5');verified=True;break
  else:raise ValueError('missing partition digest')
 require(verified and found==expected,'paired partition layout mismatch')
 return found

def native_proof(data):
 from elftools.elf.elffile import ELFFile
 e=ELFFile(io.BytesIO(data));symbols={s.name:s for s in e.get_section_by_name('.symtab').iter_symbols()}
 dram={name:e.get_section_by_name(name)['sh_size'] for name in ('.dram0.data','.dram0.bss')}
 require(sum(dram.values())<=128*1024,'paired target leaves insufficient static DRAM headroom')
 required=('verifyRollbackLater','esp_ota_mark_app_valid_cancel_rollback','esp_ota_mark_app_invalid_rollback_and_reboot','esp_tls_conn_new_async','esp_crt_bundle_attach','http_parser_execute','_binary_x509_crt_bundle_start','_binary_x509_crt_bundle_end')
 for name in required:require(name in symbols and symbols[name]['st_shndx']!='SHN_UNDEF','missing native feature: '+name)
 hook=symbols['verifyRollbackLater'];require(hook['st_info']['bind']=='STB_GLOBAL','rollback hook must override Arduino weak default')
 section=e.get_section(hook['st_shndx']);offset=hook['st_value']-section['sh_addr'];code=section.data()[offset:offset+hook['st_size']]
 # Pinned GCC8.4 windowed ABI: entry a1,32; movi.n a2,1; retw.n.
 # A different compiler output requires explicit review, not a silent bypass.
 require(code==bytes.fromhex('3641000c121df0'),'rollback hook does not return true in pinned target code')
 start=symbols['_binary_x509_crt_bundle_start'];end=symbols['_binary_x509_crt_bundle_end']
 size=end['st_value']-start['st_value'];require(1024<size<256*1024,'missing/unbounded certificate bundle bytes')
 section=e.get_section(start['st_shndx']);at=start['st_value']-section['sh_addr'];bundle=section.data()[at:at+size]
 require(len(bundle)==size and 1<=int.from_bytes(bundle[:2],'big')<=200,'invalid linked certificate bundle')
 return {'static_dram_sections':dram,'static_dram_bytes':sum(dram.values()),'rollback_hook_hex':code.hex(),'bundle_bytes':size,'bundle_certificates':int.from_bytes(bundle[:2],'big'),'bundle_sha256':hashlib.sha256(bundle).hexdigest(),'required_symbols':list(required)}

def stage(source, app_data=False, app_data_image=None, radio_iq=False, performance_trace=False):
 require(not radio_iq or app_data,'IQ requires the explicit app-data cohort')
 target='esp32s3-16mb-appdata-iq' if radio_iq else ('esp32s3-16mb-appdata' if app_data else TARGET)
 require(not performance_trace or app_data,'Performance candidate requires app-data layout')
 environment=target+'-perf' if performance_trace else target
 expected=APP_DATA_EXPECTED if app_data else EXPECTED
 abi=2 if app_data else 1
 table='partitions-paired-appdata.csv' if app_data else 'partitions-paired.csv'
 initial=None
 if app_data:
  require(app_data_image is not None,'app-data candidate requires a verified initial disk2.1 image')
  from app_data_image import verify_initial
  initial=verify_initial(app_data_image)
 elif app_data_image is not None:raise ValueError('legacy layout must not package app-data')
 require(re.fullmatch('[0-9a-f]{40}',source) and source==head(),'source SHA differs from checkout')
 require(subprocess.run(['git','diff','--quiet','HEAD'],cwd=ROOT).returncode==0,'dirty candidate sources')
 require(not subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=ROOT).strip(),'untracked candidate inputs')
 build=ROOT/'.pio/build'/environment;output=ROOT/'dist'/environment
 if output.exists():shutil.rmtree(output)
 output.mkdir(parents=True)
 blobs={name:file_bytes(build/name) for name in ('firmware.bin','firmware.elf','bootloader.bin','partitions.bin')}
 for name in ('firmware.bin','bootloader.bin'):
  esp_image(blobs[name]);require(blobs[name][3]>>4==4,'16 MiB image flag missing')
 elf(blobs['firmware.elf']);partitions(blobs['partitions.bin'],expected)
 require(hashlib.sha256(blobs['bootloader.bin']).hexdigest()==BOOTLOADER_SHA256,'unreviewed bootloader binary')
 require(target.encode()+b'\0' in blobs['firmware.bin'],'wrong compiled target')
 version=firmware((ROOT/'platformio.ini').read_text())
 for marker in [('RTE_SOURCE='+source).encode()+b'\0',('RISC_RUNTIME_VERSION:'+version).encode()+b'\0',('RISC_PAIRED_STORE_ABI:'+str(abi)).encode()+b'\0']:
  require(all(marker in blobs[n] for n in ('firmware.bin','firmware.elf')),'compiled source/version/ABI mismatch: '+repr(marker))
 require(len(blobs['firmware.bin'])<=expected['app0'][3],'firmware exceeds paired slot')
 if app_data:require(b'RISC_PAIRED_STORE_ABI:1\0' not in blobs['firmware.bin'],'app-data target must reject legacy OTA acceptance')
 proof=native_proof(blobs['firmware.elf'])
 if performance_trace:
  from elftools.elf.elffile import ELFFile
  symtab=ELFFile(io.BytesIO(blobs['firmware.elf'])).get_section_by_name('.symtab')
  symbols={s.name:s for s in symtab.iter_symbols()}
  recorder=symbols.get('_ZN8RiscPerf4dataE')
  require(recorder is not None and recorder['st_shndx']!='SHN_UNDEF' and recorder['st_size']>=4096,'performance recorder absent from diagnostic target')
  proof['performance_trace']={'enabled':True,'recorder_bytes':recorder['st_size'],'symbol':'_ZN8RiscPerf4dataE'}
 if radio_iq:
  from radio_iq_proof import prove
  proof['radio_iq']=prove(blobs['firmware.elf'])
  (output/'radio-iq-proof.json').write_text(json.dumps(proof['radio_iq'],indent=2,sort_keys=True)+'\n')
 for name,data in blobs.items():(output/name).write_bytes(data)
 for name in ('platformio.ini',table,'requirements-ci.txt'):(output/name).write_bytes(file_bytes(ROOT/name))
 if initial:
  for name in ('appdata.bin','appdata-image.json'):(output/name).write_bytes(file_bytes(Path(app_data_image)/name))
 files={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(output.iterdir())}
 record={'schema':1,'target':target,'build_environment':environment,'performance_trace':performance_trace,'source_sha':source,'firmware_version':version,'layout':'riscrte-paired-appdata-v2' if app_data else 'riscrte-paired-16m-v1','store_abi':abi,'flash_bytes':0x1000000,'partitions':expected,'native_proof':proof,'assets':files,'scope':'Development Runtime input only. Requires separately verified product store, initial bank journal and explicit user-controlled full16MiB migration. Existing factory image is not OTA compatible. No release or device operation.'}
 if initial:record['initial_appdata']=initial;record['scope']='Explicit NEW app-data layout input only. Requires matching product store/journal and owner-controlled installation; not a migration or data-preserving reflash. The initial app-data image is EMPTY and must never be installed by routine OTA. No device action or hardware qualification.'
 (output/'candidate.json').write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
 (output/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in sorted(output.iterdir()) if p.name!='SHA256SUMS'))
 print('Verified paired Runtime, linked TLS roots and explicit rollback hook:',source,output)
if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('--source-sha',required=True);parser.add_argument('--app-data',action='store_true');parser.add_argument('--radio-iq',action='store_true');parser.add_argument('--performance-trace',action='store_true');parser.add_argument('--app-data-image',type=Path);args=parser.parse_args();stage(args.source_sha,args.app_data,args.app_data_image,args.radio_iq,args.performance_trace)
