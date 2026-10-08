#!/usr/bin/env python3
"""PRIVATE offline ABI2 paired-entry planning; never opens or writes a device.

A trusted product wrapper must supply the admission callback. JSON cannot select
code or assert that admission happened. This owner-controlled ROM procedure is
separate from ordinary native OTA; its policy guards remain unchanged.
"""
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import zlib

import maintenance_plan as maintenance
import paired_bank_images as banks
from paired_candidate import APP_DATA_EXPECTED, partitions
from provision_profile import destination, read, safe_path
from release_assets import esp_image, require
from spiffs_image import read_image

FLASH=0x1000000
LAYOUT=banks.APP_DATA_LAYOUT
PHASES=('ready','invalidated','firmware-staged','store-staged','journal-staged','pending','confirmed','rolled-back')
ORDER=('application','store','journal','otadata')


def sha(raw):return hashlib.sha256(raw).hexdigest()
def encoded(value):return (json.dumps(value,sort_keys=True,indent=2,allow_nan=False)+'\n').encode()
def metadata(raw):return {'bytes':len(raw),'sha256':sha(raw)}


def snapshot(path):
    path=safe_path(path)
    maintenance.frozen(path)
    return path


def region(path,offset,size):
    require(type(offset) is int and type(size) is int and 0<=offset<FLASH and 0<size<=FLASH-offset,'snapshot bounds')
    with path.open('rb') as stream:
        stream.seek(offset);raw=stream.read(size)
    require(len(raw)==size,'snapshot changed or truncated')
    return raw


def inspect_original(inventory,path):
    require(set(inventory)=={'schema','schema_version','layout','flash_bytes','active_bank','running_firmware_sha256','quiescent'},'owner inventory fields')
    require(inventory['schema']=='riscrte.offline-paired-inventory' and type(inventory['schema_version']) is int and
            inventory['schema_version']==1 and inventory['layout']==LAYOUT and
            type(inventory['flash_bytes']) is int and inventory['flash_bytes']==FLASH and
            type(inventory['active_bank']) is int and inventory['active_bank'] in (0,1) and
            inventory['quiescent'] is True,'explicit ABI2 confirmed owner inventory required')
    require(isinstance(inventory['running_firmware_sha256'],str) and re.fullmatch('[0-9a-f]{64}',inventory['running_firmware_sha256']),'owner native digest')
    partitions(region(path,0x8000,3072),APP_DATA_EXPECTED)
    require(sha(region(path,0,banks.BOOTLOADER_BYTES))==banks.BOOTLOADER_SHA256,'unverified QIO rollback bootloader')
    ota=region(path,maintenance.OTA,8192)
    active,sequence,page=maintenance.ota_selection(ota)
    require(active==inventory['active_bank'],'owner bank differs from confirmed selector')
    journal=region(path,maintenance.JOURNAL,8192)
    records=maintenance.journal_records(journal,True)
    require(records[active] is not None,'confirmed bank has no journal')
    current=[]
    for bank,record in enumerate(records):
        app=APP_DATA_EXPECTED['app'+str(bank)];store=APP_DATA_EXPECTED['bootfs'+str(bank)]
        if record is None:
            require(all(entry is None or (entry[0]-1)%2!=bank for entry in maintenance.ota_entries(ota)),'selector references uncommitted bank')
            require(region(path,app[2],app[3])==b'\xff'*app[3] and region(path,store[2],store[3])==b'\xff'*store[3],'uncommitted bank is not blank')
            current.append(None);continue
        firmware=region(path,app[2],record[3]);image=region(path,store[2],store[3]);esp_image(firmware)
        require(b'RISC_PAIRED_STORE_ABI:2\0' in firmware and b'RISC_PAIRED_STORE_ABI:1\0' not in firmware and
                b'RISC_OWNER_INSTALLER:1\0' not in firmware,'original native ABI')
        require(sha(firmware)==record[6].hex() and sha(image)==record[7].hex(),'original journal byte identity')
        current.append((firmware,image))
    old_firmware,old_store=current[active]
    require(sha(old_firmware)==inventory['running_firmware_sha256'],'owner native differs')
    return active,sequence,page,ota,journal,records,old_firmware,old_store


def consumed_profile(journal,active,old_store):
    attempt=journal[active*4096+96:active*4096+272]
    if attempt!=b'\xff'*176:
        # journal_records already validated the active destination and CRC.
        return attempt[12:44]
    files=read_image(old_store,banks.APP_DATA_STORE_BYTES)
    digest=files.get('.provision-sha256')
    require(digest is None or len(digest)==32,'legacy consumed-profile digest')
    return digest


def target_journal(bank,firmware,store,source_record,digest):
    record=banks.record(bank,firmware,store,True)
    trailer=b''
    if digest is not None:
        require(len(digest)==32,'consumed-profile length')
        trailer=struct.pack('<3I32s32s32s32s32s',0x31545052,1,bank,digest,
                            hashlib.sha256(firmware).digest(),hashlib.sha256(store).digest(),source_record[6],source_record[7])
        trailer+=struct.pack('<I',zlib.crc32(trailer)&0xffffffff)
    return record+trailer+b'\xff'*(4096-len(record)-len(trailer))


def preserved_regions(writes):
    result=[];start=0
    for offset,size in sorted(writes):
        require(start<=offset and offset+size<=FLASH,'write overlap')
        if start<offset:result.append((start,offset-start))
        start=offset+size
    if start<FLASH:result.append((start,FLASH-start))
    return result


def prepare(inventory_path,original,firmware_path,store_path,admission):
    require(callable(admission),'explicit trusted product admission callback required')
    inventory=maintenance.json_object(read(inventory_path,4096));original=snapshot(original)
    active,sequence,page,ota,journal,records,old_firmware,old_store=inspect_original(inventory,original)
    firmware=read(firmware_path,banks.APP_DATA_FIRMWARE_MAX);store=read(store_path,banks.APP_DATA_STORE_BYTES)
    require(len(store)==banks.APP_DATA_STORE_BYTES,'receiving store geometry');esp_image(firmware)
    require(firmware[3]>>4==4 and b'RISC_PAIRED_STORE_ABI:2\0' in firmware and
            b'RISC_PAIRED_STORE_ABI:1\0' not in firmware and b'RISC_OWNER_INSTALLER:1\0' not in firmware,'receiving ordinary ABI2 native required')
    proof=admission(old_firmware,old_store,firmware,store)
    required={'schema','schema_version','layout','source_firmware_sha256','source_store_sha256','target_firmware_sha256','target_store_sha256','admissions'}
    require(isinstance(proof,dict) and required<=set(proof) and type(proof['schema_version']) is int and proof['schema_version']==1 and
            isinstance(proof['schema'],str) and re.fullmatch('[A-Za-z0-9_.-]{1,96}',proof['schema']) and
            proof['layout']==LAYOUT and isinstance(proof['admissions'],dict) and len(encoded(proof))<=256*1024,'product admission receipt contract')
    for key,data in (('source_firmware',old_firmware),('source_store',old_store),('target_firmware',firmware),('target_store',store)):
        require(proof[key+'_sha256']==sha(data),'product admission byte identity')
    target=1-active;app=APP_DATA_EXPECTED['app'+str(target)];bootfs=APP_DATA_EXPECTED['bootfs'+str(target)]
    digest=consumed_profile(journal,active,old_store)
    next_journal=target_journal(target,firmware,store,records[active],digest)
    next_seq=sequence+1;require((next_seq-1)%2==target,'selector parity')
    sequence_bytes=struct.pack('<I',next_seq)
    next_ota=sequence_bytes+b'\xff'*20+struct.pack('<II',0,zlib.crc32(sequence_bytes,0xffffffff)&0xffffffff)+b'\xff'*(4096-32)
    writes={'application':(app[2],firmware+b'\xff'*(app[3]-len(firmware))),
            'store':(bootfs[2],store),'journal':(maintenance.JOURNAL+target*4096,next_journal),
            'otadata':(maintenance.OTA+(1-page)*4096,next_ota)}
    payloads={};assets={}
    for kind,(offset,data) in writes.items():
        for prefix,raw in (('enter',data),('restore',region(original,offset,len(data)))):
            name=prefix+'-'+kind+'.bin';payloads[name]=raw;assets[name]={'offset':offset,**metadata(raw)}
    payloads['invalidate-journal.bin']=b'\xff'*4096
    assets['invalidate-journal.bin']={'offset':writes['journal'][0],**metadata(payloads['invalidate-journal.bin'])}
    preserve=[{'offset':offset,'bytes':size,'sha256':sha(region(original,offset,size))}
              for offset,size in preserved_regions([(offset,len(raw)) for offset,raw in writes.values()])]
    for offset,raw in writes.values():
        require(offset+len(raw)<=0x9000 or offset>=0xf000,'NVS write forbidden')
        require(offset+len(raw)<=banks.APP_DATA_OFFSET or offset>=banks.APP_DATA_OFFSET+banks.APP_DATA_BYTES,'app-data write forbidden')
    report={'schema':'riscrte.offline-paired-entry','schema_version':1,'layout':LAYOUT,'active_bank':active,'target_bank':target,
            'sequence':next_seq,'assets':assets,'preserved_regions':preserve,'admission':proof,
            'consumed_profile_inherited':digest is not None,'consumed_profile_sha256':None if digest is None else digest.hex(),
            'hardware_status':'UNRUN','device_operations':False,
            'write_order':['invalidate-journal.bin',*['enter-'+kind+'.bin' for kind in ORDER]],
            'limits':['PRIVATE snapshot, backups and receipts; never publish owner data.',
                      'Explicit owner-controlled ROM operation only; this tool never opens or writes a device.',
                      'Each completed write requires a fresh matching snapshot before continuing; torn writes refuse.',
                      'Invalidate destination readiness before overwriting either image; commit new readiness only after both readbacks.',
                      'The emitted OTA page is NEW, never VALID. Only the receiving ordinary firmware may confirm itself.',
                      'Backups are private recovery evidence, not automatic recovery authority. Never restore NVS/app-data or boot between unverified writes.',
                      'Post-boot application writes can change user state; do not erase or restore it to make a comparison pass. A later hop needs a fresh confirmed snapshot.',
                      'Require a fresh confirmed snapshot and independent plan for the next hop.',
                      'This is not ordinary native OTA and does not relax its admission or policy limits.']}
    return report,payloads


def plan(inventory,original,firmware,store,output,admission):
    output=destination(output,private=True);report,payloads=prepare(inventory,original,firmware,store,admission)
    output.mkdir(mode=0o700)
    try:
        files={**payloads,'plan.json':encoded(report)}
        for name,raw in files.items():
            path=output/name
            with path.open('xb') as stream:require(stream.write(raw)==len(raw),'short private output')
            path.chmod(0o600)
        require(all(read(output/name,16*1024*1024)==raw for name,raw in files.items()),'private output readback')
        (output/'COMPLETE').write_bytes(b'riscrte.offline-paired-entry.v1\n');(output/'COMPLETE').chmod(0o600)
    except BaseException:
        shutil.rmtree(output);raise
    return report


def verify_phase(inventory,original,firmware,store,output,current,phase,admission):
    require(phase in PHASES,'unknown paired-entry phase')
    report,payloads=prepare(inventory,original,firmware,store,admission);output=safe_path(output);current=snapshot(current)
    require({path.name for path in output.iterdir()}==set(payloads)|{'plan.json','COMPLETE'} and
            read(output/'COMPLETE',64)==b'riscrte.offline-paired-entry.v1\n' and
            read(output/'plan.json',512*1024)==encoded(report),'private plan custody')
    for name,data in payloads.items():require(read(output/name,8*1024*1024)==data,'private plan payload changed')
    for item in report['preserved_regions']:
        require(sha(region(current,item['offset'],item['bytes']))==item['sha256'],'preserved region changed (including NVS/app-data)')
    step=PHASES.index(phase)
    for number,kind in enumerate(ORDER):
        threshold={'application':2,'store':3,'journal':4,'otadata':5}[kind]
        name=('enter' if step>=threshold else 'restore')+'-'+kind+'.bin'
        if kind=='journal' and 1<=step<4:name='invalidate-journal.bin'
        item=report['assets'][name]
        actual=region(current,item['offset'],item['bytes']);expected=payloads[name]
        if kind=='otadata' and step>=5:
            state=struct.unpack_from('<I',actual,24)[0]
            allowed={'pending':(0,1),'confirmed':(2,),'rolled-back':(4,)}[phase]
            require(state in allowed,'unexpected OTA verification state')
            normalized=bytearray(actual);struct.pack_into('<I',normalized,24,0);actual=bytes(normalized)
        require(actual==expected,'write order or phase bytes differ: '+kind)
    return {'phase':phase,'verified':True,'hardware_status':'UNRUN','source_bank':report['active_bank'],'target_bank':report['target_bank'],
            'native_confirmation_seen':phase=='confirmed','rollback_seen':phase=='rolled-back',
            'scope':'Offline snapshot proof only; live ROM state and native health confirmation are not inferred.'}


if __name__=='__main__':
    raise SystemExit('Use a trusted product wrapper with exact native/store admission; unvalidated CLI input is refused.')
