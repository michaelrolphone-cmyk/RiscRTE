#!/usr/bin/env python3
"""Offline ABI1 maintenance entry/restoration plan. Never opens a device.

Inputs are explicit private inventory, frozen flash snapshot and maintenance
artifact. Outputs contain only inactive application and one OTA metadata page;
NVS, active firmware/store, bank journal and partition table are never restored.
"""
import argparse,hashlib,json,struct,zlib
from pathlib import Path
from release_assets import file_bytes,require,esp_image
from paired_candidate import partitions
from paired_bank_images import BOOTLOADER_BYTES,BOOTLOADER_SHA256,STORE_BYTES,parse_record

FLASH=0x1000000;APP_BYTES=0x300000;APPS=(0x10000,0x800000);STORES=(0x310000,0xb00000)
OTA=0xff0000;JOURNAL=0xff2000

def sha(data):return hashlib.sha256(data).hexdigest()
def json_object(data):
    def unique(pairs):
        out={}
        for key,value in pairs:
            require(key not in out,'duplicate JSON field');out[key]=value
        return out
    return json.loads(data,object_pairs_hook=unique)
def read_range(snapshot,offset,size):
    # This tool has no reason to inspect private NVS [0x9000,0xf000).
    require(offset+size<=0x9000 or offset>=0xf000,'NVS read forbidden')
    with snapshot.open('rb') as stream:stream.seek(offset);data=stream.read(size)
    require(len(data)==size,'snapshot range missing');return data

def ota_selection(data):
    require(len(data)==8192,'OTA metadata size')
    valid=[]
    for index in range(2):
        page=data[index*4096:(index+1)*4096];entry=page[:32]
        require(page[32:]==b'\xff'*(4096-32),'unknown OTA page contents')
        if entry==b'\xff'*32:continue
        seq,label,state,crc=struct.unpack('<I20sII',entry)
        require(1<=seq<0xfffffff0 and crc==(zlib.crc32(entry[:4],0xffffffff)&0xffffffff),'OTA sequence/CRC unsupported')
        require(state in (2,3,4),'unconfirmed OTA transition')
        if state==2:valid.append((seq,index))
    require(valid,'no confirmed OTA selection')
    require(len({seq for seq,_ in valid})==len(valid),'ambiguous OTA sequence')
    seq,page=max(valid);return (seq-1)%2,seq,page

def maintenance(folder,source):
    meta=json_object(file_bytes(folder/'candidate.json'));image=file_bytes(folder/'firmware.bin')
    require(meta.get('schema')==1 and meta.get('target')=='esp32s3-16mb-maintenance' and meta.get('source_sha')==source,'maintenance identity')
    require(meta.get('bytes')==len(image) and meta.get('sha256')==sha(image),'maintenance digest')
    require(len(image)<=APP_BYTES,'maintenance slot bound');esp_image(image)
    require(image[3]>>4==4,'maintenance flash declaration')
    require(b'RISC_OWNER_INSTALLER:1\0' in image and b'RISC_PAIRED_STORE_ABI:1\0' not in image and ('RTE_SOURCE='+source).encode()+b'\0' in image,'maintenance markers')
    return image

def plan(inventory_path,snapshot,artifact,output):
    inventory=json_object(file_bytes(inventory_path))
    required={'schema','schema_version','layout','flash_bytes','active_bank','runtime_version','running_firmware_sha256','maintenance_source_sha','quiescent'}
    require(set(inventory)==required,'missing/unknown inventory fields')
    require(inventory['schema']=='riscrte.maintenance-inventory' and type(inventory['schema_version']) is int and inventory['schema_version']==1 and inventory['layout']=='riscrte-paired-16m-v1' and inventory['flash_bytes']==FLASH,'incompatible inventory layout')
    require(type(inventory['active_bank']) is int and inventory['active_bank'] in (0,1) and inventory['quiescent'] is True,'explicit confirmed/quiescent inventory required')
    from check_versions import version
    require(version(inventory['runtime_version'])>=(0,1,31),'ordinary Runtime lacks descriptor-v2 installation support')
    source=inventory['maintenance_source_sha'];require(isinstance(source,str) and len(source)==40 and all(c in '0123456789abcdef' for c in source),'exact maintenance source required')
    require(snapshot.is_file() and not snapshot.is_symlink() and snapshot.stat().st_size==FLASH,'full frozen ABI1 snapshot required')
    require(not output.exists() and not output.is_symlink(),'output already exists')
    partitions(read_range(snapshot,0x8000,3072))
    require(sha(read_range(snapshot,0,BOOTLOADER_BYTES))==BOOTLOADER_SHA256,'unverified rollback bootloader')
    otadata=read_range(snapshot,OTA,8192);active,seq,page=ota_selection(otadata)
    require(active==inventory['active_bank'],'active-bank inventory mismatch')
    journal=read_range(snapshot,JOURNAL,8192);record=parse_record(journal[active*4096:active*4096+96])
    require(record[2]==active,'active journal identity')
    firmware=read_range(snapshot,APPS[active],record[3]);esp_image(firmware)
    require(sha(firmware)==record[6].hex()==inventory['running_firmware_sha256'],'running firmware inventory mismatch')
    require(b'RISC_PAIRED_STORE_ABI:1\0' in firmware and ('RISC_RUNTIME_VERSION:'+inventory['runtime_version']).encode()+b'\0' in firmware,'ordinary runtime markers')
    require(sha(read_range(snapshot,STORES[active],STORE_BYTES))==record[7].hex(),'active store identity')
    target=1-active;backup=read_range(snapshot,APPS[target],APP_BYTES)
    image=maintenance(artifact,source);entry_image=image+b'\xff'*(APP_BYTES-len(image))
    target_page=1-page;original_page=otadata[target_page*4096:(target_page+1)*4096]
    # Next sequence selects the inactive bank. NEW requires normal bootloader
    # pending verification; no maintenance health confirmation is fabricated.
    new_seq=seq+1;require((new_seq-1)%2==target,'OTA transition parity')
    sequence=struct.pack('<I',new_seq)
    entry=sequence+b'\xff'*20+struct.pack('<II',0,zlib.crc32(sequence,0xffffffff)&0xffffffff)
    entry_page=entry+b'\xff'*(4096-32)
    payloads={'enter-application.bin':entry_image,'enter-otadata-page.bin':entry_page,'restore-application.bin':backup,'restore-otadata-page.bin':original_page}
    offsets={'enter-application.bin':APPS[target],'restore-application.bin':APPS[target],'enter-otadata-page.bin':OTA+target_page*4096,'restore-otadata-page.bin':OTA+target_page*4096}
    report={'schema':'riscrte.maintenance-plan','schema_version':1,'layout':inventory['layout'],'active_bank':active,'maintenance_bank':target,'maintenance_source_sha':source,
      'hardware_status':'UNRUN','execution':'offline plan only; no device/flash/reset operation',
      'assets':{name:{'offset':offsets[name],'bytes':len(data),'sha256':sha(data)} for name,data in payloads.items()},
      'preserve':{'nvs':'excluded from reads and every write/restore payload','active_firmware_sha256':sha(firmware),'active_store_sha256':record[7].hex(),'bank_journal_sha256':sha(journal),'partition_table':'unchanged'},
      'maintenance_ota':{'page_offset':OTA+target_page*4096,'sequence':new_seq,'allowed_restore_states':[0,1,4],'inactive_firmware_sha256':sha(entry_image)},
      'preconditions':['Owner must independently verify untouched live regions still match the inventory, and changed regions match the step-specific entry/restore preconditions.','Do not use on ABI2/app-data or another flash layout.','Keep restoration files privately before entering maintenance.'],
      'entry_order':['Deliberately enter ROM maintenance; this tool does not control reset/boot mode.','Verify inactive application and alternate OTA page match restoration hashes.','Write and read back enter-application.bin at its recorded offset.','Write and read back enter-otadata-page.bin at its recorded offset.','Deliberately boot maintenance, then use the explicit source-matched NVS install command.'],
      'restoration_order':['Deliberately return to ROM maintenance; do not overwrite the executing maintenance image from itself.','Verify inactive firmware still matches the entry image and the alternate OTA page has the planned sequence and NEW/PENDING_VERIFY/ABORTED state. Unexpected ordinary provisioning invalidates this restoration plan.','Restore and read back restore-application.bin.','Restore and read back restore-otadata-page.bin.','Verify preserved active firmware/store/journal. Never restore NVS from the old snapshot.','Deliberately boot ordinary Runtime; hardware outcome remains unqualified.']}
    output.mkdir(mode=0o700)
    try:
        for name,data in payloads.items():(output/name).write_bytes(data)
        (output/'plan.json').write_text(json.dumps(report,sort_keys=True,indent=2)+'\n')
        (output/'SHA256SUMS').write_text(''.join(f'{sha(file_bytes(p))}  {p.name}\n' for p in sorted(output.iterdir())))
    except Exception:
        import shutil
        shutil.rmtree(output);raise
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('inventory','snapshot','maintenance','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();plan(a.inventory,a.snapshot,a.maintenance,a.output)
    print('Prepared private offline ABI1 maintenance plan; NVS excluded; hardware UNRUN.')
if __name__=='__main__':main()
