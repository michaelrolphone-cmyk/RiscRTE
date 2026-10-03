#!/usr/bin/env python3
"""Freeze the application-only embedded-store candidate. No hardware operations."""
import argparse,json,subprocess
from pathlib import Path
from release_assets import ROOT,file_bytes,sha,require,head,esp_image,elf
LIMIT=0x1f0000
PAYLOADS=('default.elf','board.json','boot.json')
def validate(folder,source):
    manifest=json.loads(file_bytes(folder/'manifest.json'))
    require(manifest['schema']==1 and manifest['target']=='cam-nosd' and manifest['source_sha']==source,'wrong candidate identity')
    require(manifest['boot_backend']=='embedded-readonly' and manifest['flash_bytes']==16777216 and manifest['memory_type']=='qio_opi','wrong candidate port')
    require(isinstance(manifest['run_id'],int) and manifest['run_id']>0 and isinstance(manifest['run_attempt'],int) and manifest['run_attempt']>0,'missing Actions custody')
    require({p.name for p in folder.iterdir()}=={'firmware.bin','manifest.json',*PAYLOADS},'unexpected candidate members')
    image=file_bytes(folder/'firmware.bin');esp_image(image)
    require((len(image)+4095)//4096*4096<=LIMIT,'candidate exceeds guarded app region')
    require(('RTE_SOURCE='+source).encode()+b'\0' in image,'compiled source differs from candidate')
    require(b'cam-nosd\0' in image,'missing CAM health identity')
    require(manifest['firmware']=={'file':'firmware.bin','bytes':len(image),'sha256':sha(image),'offset':65536},'firmware custody mismatch')
    require(len(manifest['payloads'])==3 and {p['file'] for p in manifest['payloads']}==set(PAYLOADS),'wrong embedded file inventory')
    for item in manifest['payloads']:
        data=file_bytes(folder/item['file']);offset=item['image_offset']
        require(item['bytes']==len(data) and item['sha256']==sha(data),'payload hash mismatch')
        require(isinstance(offset,int) and offset>=0 and image[offset:offset+len(data)]==data,'payload is not embedded at declared image offset')
    elf(file_bytes(folder/'default.elf'),True)
    require(json.loads(file_bytes(folder/'boot.json'))=={'board':'board.json','default_app':'default.elf','drivers':[]},'unexpected CI startup graph')
    return manifest

def stage(source,run_id,attempt):
    require(source==head(),'candidate differs from checkout')
    require(subprocess.run(['git','diff','--quiet','HEAD'],cwd=ROOT).returncode==0,'candidate source tree is dirty')
    require(not subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=ROOT).strip(),'untracked candidate source')
    output=ROOT/'dist/cam';output.mkdir(parents=True,exist_ok=True)
    for p in output.iterdir():
        require(p.is_file() and not p.is_symlink(),'unsafe prior candidate member');p.unlink()
    image=file_bytes(ROOT/'.pio/build/cam-ci/firmware.bin');(output/'firmware.bin').write_bytes(image)
    payloads=[]
    for name in PAYLOADS:
        data=file_bytes(ROOT/'build/store'/name);(output/name).write_bytes(data)
        pos=image.find(data);require(pos>=0,'payload differs from compiled bytes')
        payloads.append({'file':name,'bytes':len(data),'sha256':sha(data),'image_offset':pos})
    manifest={'schema':1,'target':'cam-nosd','source_sha':source,'run_id':run_id,'run_attempt':attempt,'boot_backend':'embedded-readonly','flash_bytes':16777216,'memory_type':'qio_opi','firmware':{'file':'firmware.bin','bytes':len(image),'sha256':sha(image),'offset':65536},'payloads':payloads}
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
    validate(output,source);print('Verified application-only CAM candidate:',output)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source-sha',required=True);p.add_argument('--run-id',type=int,required=True);p.add_argument('--run-attempt',type=int,required=True);a=p.parse_args()
    stage(a.source_sha,a.run_id,a.run_attempt)
