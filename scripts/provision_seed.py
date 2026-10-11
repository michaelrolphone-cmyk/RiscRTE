#!/usr/bin/env python3
"""Compose an OFFLINE generic paired seed. Never handles NVS or a device.

This is a first-install artifact for the explicit ABI1 or ABI2 paired layout,
not an OTA update, merged flash image, migration, format or flash command.
"""
import argparse, copy, hashlib, json, re, shutil, subprocess, tempfile, sys
from pathlib import Path
from release_assets import ROOT, file_bytes, require, esp_image, elf, head
from paired_candidate import partitions, native_proof, policy_rows_proof, TARGET, EXPECTED, APP_DATA_EXPECTED
from paired_bank_images import (BOOTLOADER_BYTES, BOOTLOADER_SHA256, STORE_BYTES,
                               initial_bank_state, initial_otadata, parse_record, APP_DATA_STORE_BYTES)

PAYLOADS=('bootloader.bin','partitions.bin','firmware.bin','firmware.elf')
SEED_FILES=('board.json','boot.json','default.elf')
EXTENSION_FILE_BYTES=1024*1024
EXTENSION_TOTAL_BYTES=2*1024*1024
EXTENSION_MAX_FILES=8
SEED_MANIFEST_BYTES=128*1024
SEED_RESERVED=set(PAYLOADS+SEED_FILES+('candidate.json','bootfs0.bin','seed.json','SHA256SUMS','otadata.bin','bank_state.bin','appdata.bin','appdata-image.json','radio-iq-proof.json','profile.json','first-install.json'))
AUXILIARY_ASSETS={'platformio.ini','partitions-paired.csv','partitions-paired-appdata.csv','requirements-ci.txt'}
def sha(data): return hashlib.sha256(data).hexdigest()
def exact_json(left,right):return json.dumps(left,sort_keys=True,separators=(',',':'))==json.dumps(right,sort_keys=True,separators=(',',':'))

def extension_assets(folder, record, core):
    """Freeze only bounded declared JSON sidecars for an explicit validator."""
    names=set(record['assets'])-set(core)-AUXILIARY_ASSETS
    require(len(names)<=EXTENSION_MAX_FILES,'too many candidate extension assets')
    result={};total=0
    for name in sorted(names):
        require(isinstance(name,str) and re.fullmatch(r'[A-Za-z0-9_.-]+\.json',name) and
                name not in SEED_RESERVED,'unsafe candidate extension asset')
        path=folder/name
        require(not path.is_symlink() and path.is_file() and 0<path.stat().st_size<=EXTENSION_FILE_BYTES,'candidate extension asset bounds')
        data=file_bytes(path);total+=len(data)
        require(len(data)<=EXTENSION_FILE_BYTES and total<=EXTENSION_TOTAL_BYTES,'candidate extension total bounds')
        require(exact_json(record['assets'][name],{'bytes':len(data),'sha256':sha(data)}),'candidate extension asset digest')
        # A validator cannot turn an opaque binary into a JSON proof sidecar.
        json.loads(data.decode('utf-8'))
        result[name]=data
    return result

def verified_extension(record, blobs, proof, extras, validator):
    require(callable(validator),'explicit candidate extension validator required')
    # The callback is trusted source code supplied by the product wrapper,
    # never a path/import/name selected by candidate or provisioning JSON.
    value=validator(copy.deepcopy(record),dict(blobs,**extras))
    require(isinstance(value,dict) and set(value)=={'id','native_proof','assets','metadata'},'candidate extension fields')
    require(isinstance(value['id'],str) and re.fullmatch(r'[A-Za-z0-9_.-]{1,96}',value['id']),'candidate extension identity')
    extra_proof=value['native_proof'];assets=value['assets'];metadata=value['metadata']
    require(isinstance(extra_proof,dict) and not set(extra_proof)&set(proof),'candidate extension proof overlap')
    require(exact_json({**proof,**extra_proof},record['native_proof']),'candidate extension native proof mismatch')
    require(isinstance(assets,dict) and set(assets)==set(extras) and
            all(type(assets[name]) is bytes and assets[name]==extras[name] for name in extras),'candidate extension assets mismatch')
    protected={'schema','source_sha','target','layout','store_abi','firmware_version','flash_bytes','assets','native_proof','initial_appdata','_seed_extension'}
    require(isinstance(metadata,dict) and len(metadata)<=16 and not set(metadata)&protected and
            all(isinstance(name,str) and name in record and exact_json(value,record[name]) for name,value in metadata.items()),'candidate extension metadata mismatch')
    receipt={'id':value['id'],'native_proof':extra_proof,'metadata':metadata,
             'assets':{name:record['assets'][name] for name in sorted(extras)}}
    require(len(json.dumps(receipt,sort_keys=True,separators=(',',':')).encode())<=128*1024,'candidate extension receipt bounds')
    return copy.deepcopy(receipt)

def layout(record):
    identity=(record.get('layout'),record.get('target'),record.get('store_abi'))
    if identity==('riscrte-paired-16m-v1',TARGET,1) and type(record.get('store_abi')) is int:
        return EXPECTED,False
    if identity in (('riscrte-paired-appdata-v2','esp32s3-16mb-appdata',2),
                    ('riscrte-paired-appdata-v2','esp32s3-16mb-appdata-iq',2)) and type(record.get('store_abi')) is int:
        return APP_DATA_EXPECTED,True
    raise ValueError('candidate identity')

def segments(app_data=False):
    values={'bootloader.bin':0,'partitions.bin':0x8000,'firmware.bin':0x10000,
            'bootfs0.bin':0x2f0000 if app_data else 0x310000,'otadata.bin':0xff0000,'bank_state.bin':0xff2000}
    if app_data:values['appdata.bin']=0x270000
    return values

def candidate(folder, source, extension_validator=None):
    record=json.loads(file_bytes(folder/'candidate.json'))
    require('_seed_extension' not in record,'reserved candidate extension field')
    expected,app_data=layout(record)
    require(record['source_sha']==source,'candidate identity')
    radio_iq=record['target']=='esp32s3-16mb-appdata-iq'
    names=PAYLOADS+(('appdata.bin','appdata-image.json') if app_data else ())+(('radio-iq-proof.json',) if radio_iq else ())
    blobs={name:file_bytes(folder/name) for name in names}
    for name,data in blobs.items():
        require(record['assets'][name]=={'bytes':len(data),'sha256':sha(data)},'candidate asset digest')
    partitions(blobs['partitions.bin'],expected)
    if app_data:
        from app_data_image import verify_initial
        require(verify_initial(folder)==record['initial_appdata'],'candidate initial app-data identity')
    for name in ('bootloader.bin','firmware.bin'):
        esp_image(blobs[name]);require(blobs[name][3]>>4==4,'candidate flash declaration')
    require(len(blobs['bootloader.bin'])==BOOTLOADER_BYTES and sha(blobs['bootloader.bin'])==BOOTLOADER_SHA256,'rollback bootloader')
    require(len(blobs['firmware.bin'])<=expected['app0'][3],'firmware slot bound')
    elf(blobs['firmware.elf'])
    proof=native_proof(blobs['firmware.elf'])
    # Current candidates carry an explicit linked policy-row proof. Preserve
    # older frozen seeds without that feature, while never inferring an absent
    # declaration for a native that advertises it.
    policy=record['native_proof'].get('app_policy')
    if policy is not None:
        require(isinstance(policy,dict) and type(policy.get('rows')) is int,'candidate app policy proof')
        proof['app_policy']=policy_rows_proof(blobs,policy['rows'])
    else:
        require(all(b'RISC_APP_POLICY_ROWS:' not in blobs[name] for name in ('firmware.bin','firmware.elf')),
                'candidate app policy proof missing')
    if radio_iq:
        from radio_iq_proof import prove
        proof['radio_iq']=json.loads(json.dumps(prove(blobs['firmware.elf'])))
        require(json.loads(blobs['radio-iq-proof.json'])==proof['radio_iq'],'radio IQ linked reservation proof')
    extras=extension_assets(folder,record,blobs) if extension_validator is not None else {}
    extension=None
    if extension_validator is not None:
        extension=verified_extension(record,blobs,proof,extras,extension_validator)
    else:
        require(not (set(record['assets'])-set(blobs)-AUXILIARY_ASSETS),'explicit candidate extension validator required')
        require(proof==record['native_proof'],'native linked proof')
    require(record['target'].encode()+b'\0' in blobs['firmware.bin'],'candidate compiled target')
    if app_data:require(b'RISC_PAIRED_STORE_ABI:1\0' not in blobs['firmware.bin'],'candidate mixed ABI markers')
    for marker in (('RTE_SOURCE='+source).encode()+b'\0',('RISC_RUNTIME_VERSION:'+record['firmware_version']).encode()+b'\0',('RISC_PAIRED_STORE_ABI:'+str(record['store_abi'])).encode()+b'\0'):
        require(marker in blobs['firmware.bin'] and marker in blobs['firmware.elf'],'candidate compiled identity')
    if extension is not None:
        record['_seed_extension']=extension;blobs.update(extras)
    return record,blobs

def build_heartbeat(compiler):
    # Reuse the existing source build; never infer provenance from an ELF marker.
    subprocess.run([sys.executable,str(ROOT/'scripts/build_apps.py'),'--cc',str(compiler)],cwd=ROOT,check=True,capture_output=True)
    return file_bytes(ROOT/'build/elf/default.elf')

def verify_store(tool, image, members, work, app_data=False):
    require(len(image)==(APP_DATA_STORE_BYTES if app_data else STORE_BYTES),'store image size')
    frozen=work/'frozen.bin';frozen.write_bytes(image)
    extracted=work/'readback';extracted.mkdir()
    subprocess.run([str(tool),'-u',str(extracted),'-b','4096','-p','256',str(frozen)],check=True,capture_output=True)
    require({p.name for p in extracted.iterdir()}==set(SEED_FILES),'seed inventory')
    for name in SEED_FILES:require(file_bytes(extracted/name)==members[name],'seed readback mismatch')
    return image

def compose(folder, store_input, tool, output, source, compiler, extension_validator=None):
    require(source==head(),'seed requires exact current source')
    require(not subprocess.check_output(['git','status','--porcelain','--untracked-files=normal'],cwd=ROOT).strip(),'seed requires clean source')
    require(not output.exists() and not output.is_symlink(),'output already exists')
    record,blobs=candidate(folder,source,extension_validator)
    _,app_data=layout(record)
    # Always take the generic no-bus/no-device boot graph from this checkout.
    members={name:file_bytes(ROOT/'data'/name) for name in ('board.json','boot.json')}
    board=json.loads(members['board.json']);boot=json.loads(members['boot.json'])
    require(board=={'schema':'riscrte.board-hardware','schema_version':1,'board_id':'esp32s3-baseline','revision':'unspecified','buses':[],'devices':[]},'non-generic seed board')
    require(boot=={'board':'board.json','default_app':'default.elf','drivers':[]},'non-generic seed graph')
    members['default.elf']=build_heartbeat(compiler);require(len(members['default.elf'])<=2*1024*1024,'native ELF size bound');elf(members['default.elf'],True)
    app=json.loads(file_bytes(ROOT/'apps/heartbeat/manifest.json'))
    require(('RTE_HEARTBEAT version='+app['version']+' ').encode() in members['default.elf'],'seed heartbeat marker')
    with tempfile.TemporaryDirectory(prefix='rte-seed-') as temporary:
        work=Path(temporary);store=verify_store(tool,file_bytes(store_input),members,work,app_data)
        journal=initial_bank_state(blobs['firmware.bin'],store,app_data);parse_record(journal[:96],app_data)
        payloads={**blobs,**members,'candidate.json':file_bytes(folder/'candidate.json'),'bootfs0.bin':store,'otadata.bin':initial_otadata(),'bank_state.bin':journal}
        manifest={'schema':'riscrte.provisioning-seed','schema_version':1,'source_sha':source,'firmware_version':record['firmware_version'],
                  'layout':record['layout'],'target':record['target'],'store_abi':record['store_abi'],'seed_app_version':app['version'],
                  'segments':segments(app_data),
                  'assets':{name:{'bytes':len(data),'sha256':sha(data)} for name,data in sorted(payloads.items())},
                  'prerequisites':['New owner-controlled paired deployment only; not an existing-media update.','Inactive bank1 must be blank.','NVS deliberately omitted: supply private owner input separately.','ABI2 empty app-data is for a new installation only; never use it to update an existing device.'],
                  'scope':'Offline generic seed with verified bank0 identity. No device, network, merge, flash, erase, resize or format operation.'}
        if '_seed_extension' in record:manifest['extension']=record['_seed_extension']
        manifest_bytes=(json.dumps(manifest,sort_keys=True,indent=2)+'\n').encode()
        require(len(manifest_bytes)<=SEED_MANIFEST_BYTES,'seed manifest size bound')
        # All validation precedes output creation; output is never overwritten.
        output.mkdir()
        try:
            for name,data in payloads.items():(output/name).write_bytes(data)
            for name,data in payloads.items():require(file_bytes(output/name)==data,'seed output readback mismatch')
            (output/'seed.json').write_bytes(manifest_bytes)
            (output/'SHA256SUMS').write_text(''.join(f'{sha(file_bytes(p))}  {p.name}\n' for p in sorted(output.iterdir())))
        except BaseException:
            shutil.rmtree(output);raise
    return manifest

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('candidate','store','cc','mkspiffs','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--source-sha',required=True);a=p.parse_args()
    compose(a.candidate,a.store,a.mkspiffs,a.output,a.source_sha,a.cc)
    print('Verified generic paired seed; NVS omitted; no device operation.')
if __name__=='__main__':main()
