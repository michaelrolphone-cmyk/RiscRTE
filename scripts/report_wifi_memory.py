#!/usr/bin/env python3
"""Measure the exact selected ELF mapping demand; never estimate hardware free RAM."""
import argparse,hashlib,json,struct
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--store',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--arduino',type=Path,required=True)
a=p.parse_args();root=a.store.resolve();boot=json.loads((root/'boot.json').read_text())
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def mapped(path):
 b=path.read_bytes();assert b[:6]==b'\x7fELF\x01\x01'
 h=struct.unpack_from('<16sHHIIIIIHHHHHH',b);off,size,count,names=h[6],h[11],h[12],h[13]
 sections=[struct.unpack_from('<IIIIIIIIII',b,off+i*size) for i in range(count)]
 strings=sections[names];names=b[strings[4]:strings[4]+strings[5]]
 selected={}
 for s in sections:
  name=names[s[0]:].split(b'\0',1)[0].decode()
  if name in ('.text','.data','.rodata','.data.rel.ro','.bss') and s[5]:
   assert s[2]&2 and s[8]&max(0,s[8]-1)==0
   selected[name]={'bytes':s[5],'alignment':max(4,s[8])}
 text=selected['.text']['bytes']
 data=sum(s['bytes']+s['alignment']-1 for name,s in selected.items() if name!='.text')
 return {'path':str(path.relative_to(root)),'sha256':digest(path),'file_bytes':len(b),'text_request_bytes':text,'data_request_bytes':data,'mapping_request_bytes':text+data,'sections':selected}
providers=[]
for d in boot['drivers']:
 m=json.loads((root/d['manifest']).read_text());v=mapped(root/Path(d['manifest']).parent/m['file_name'])
 v.update(id=m['id'],version=m['version'],manifest=d['manifest'],requires=m['requires'],provides=m['provides']);providers.append(v)
by_cap={}
for i,pr in enumerate(providers):
 for cap in pr['provides']:by_cap.setdefault((cap['capability'],cap['api']),[]).append(i)
def closure(caps):
 selected=set();todo=list(caps)
 while todo:
  c=todo.pop();matches=by_cap.get((c['capability'],c['api']),[])
  if not matches:continue
  assert len(matches)==1
  i=matches[0]
  if i in selected:continue
  selected.add(i);todo+=providers[i]['requires']
 return sorted(selected)
apps=[]
for policy in boot['app_capabilities']:
 m=json.loads((root/policy['manifest']).read_text());v=mapped(root/m['file_name']);cl=closure(m['requires'])
 v.update(id=m['id'],version=m['version'],authorized_provider_closure=[providers[i]['id'] for i in cl],authorized_provider_mapping_bytes=sum(providers[i]['mapping_request_bytes'] for i in cl));apps.append(v)
sdk=a.arduino/'tools/sdk/esp32s3/qio_opi/include/sdkconfig.h'
wifi=a.arduino/'libraries/WiFi/src/WiFiGeneric.cpp'
configs={}
for line in sdk.read_text().splitlines():
 parts=line.split()
 if len(parts)==3 and parts[0]=='#define' and ('WIFI_' in parts[1] or 'SPIRAM_MALLOC_' in parts[1] or parts[1]=='CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP'):configs[parts[1]]=parts[2]
result={'schema':1,'store':str(root),'boot_sha256':digest(root/'boot.json'),'activation':boot['provider_activation'],
 'provider_count':len(providers),'app_count':len(apps),'all_provider_mapping_bytes':sum(v['mapping_request_bytes'] for v in providers),'providers':providers,'apps':apps,
 'allocator':'CONFIG_ELF_LOADER_LOAD_PSRAM=1: text/data are MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT. Data reserves section length + max(4,sh_addralign)-1 for each nonempty data/rodata/data.rel.ro/bss section, matching esp_elf_data_reserve.',
 'limits':['Mapping bytes are exact loader allocation requests, excluding allocator metadata, loader symbol names/tables, temporary ELF file buffers, ordinary provider allocations and SDK resources.','Authorized closures are potential demand, not a claim that every capability was acquired. demand-retained keeps previously acquired providers; unvisited providers remain unloaded.','No host RSS or PSRAM sum is interpreted as internal or DMA free memory. Actual native heap pools require the setup-boundary snapshots.','No hardware, RF, DHCP or allocator-fragmentation result.'],
 'pinned_sdk':{'sdkconfig_sha256':digest(sdk),'arduino_wifi_sha256':digest(wifi),'values':configs,'runtime_init':'WIFI_INIT_CONFIG_DEFAULT with nvs_enable=false','arduino_default_override':{'static_rx_buf_num':4,'dynamic_rx_buf_num':32,'tx_buf_type':1,'static_tx_buf_num':0,'dynamic_tx_buf_num':32,'cache_tx_buf_num':4}}}
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'providers':len(providers),'apps':len(apps),'all_provider_mapping_bytes':result['all_provider_mapping_bytes'],'wifi_app':next(x for x in apps if x['id']=='wifi_settings')},indent=2))
