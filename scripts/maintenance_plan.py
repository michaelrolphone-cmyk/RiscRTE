#!/usr/bin/env python3
"""Offline ABI1/ABI2 owner-maintenance plans and snapshot phase checks.

Never opens a device. Only the inactive application and alternate OTA page are
payloads. NVS is never read; app-data and every other flash region are preserved.
"""
import argparse, hashlib, json, struct, zlib
from pathlib import Path
from release_assets import file_bytes, require, esp_image
from paired_candidate import partitions, EXPECTED, APP_DATA_EXPECTED
from paired_bank_images import BOOTLOADER_BYTES, BOOTLOADER_SHA256, STORE_BYTES, parse_record

FLASH=0x1000000; APP_BYTES=0x300000; APPS=(0x10000,0x800000); STORES=(0x310000,0xb00000)
OTA=0xff0000; JOURNAL=0xff2000
LAYOUTS={
    'riscrte-paired-16m-v1': (EXPECTED, False, 'esp32s3-16mb-maintenance', 'esp32s3-16mb-paired'),
    'riscrte-paired-appdata-v2': (APP_DATA_EXPECTED, True, 'esp32s3-16mb-appdata-maintenance', 'esp32s3-16mb-appdata'),
}
PHASES=('entry-ready','application-staged','maintenance','application-restored','restored')

def sha(data): return hashlib.sha256(data).hexdigest()
def json_object(data):
    def unique(pairs):
        out={}
        for key,value in pairs:
            require(key not in out,'duplicate JSON field'); out[key]=value
        return out
    value=json.loads(data,object_pairs_hook=unique)
    require(isinstance(value,dict),'JSON object required')
    return value

def read_range(snapshot,offset,size):
    require(type(offset) is int and type(size) is int and offset>=0 and size>0 and offset+size<=FLASH,'snapshot range bounds')
    require(offset+size<=0x9000 or offset>=0xf000,'NVS read forbidden')
    with snapshot.open('rb') as stream:
        stream.seek(offset); data=stream.read(size)
    require(len(data)==size,'snapshot range missing'); return data

def frozen(snapshot):
    require(snapshot.is_file() and not snapshot.is_symlink() and snapshot.stat().st_size==FLASH,'full frozen 16 MiB snapshot required')

def ota_entries(data):
    require(len(data)==8192,'OTA metadata size')
    entries=[]
    for index in range(2):
        page=data[index*4096:(index+1)*4096]; entry=page[:32]
        require(page[32:]==b'\xff'*(4096-32),'unknown OTA page contents')
        if entry==b'\xff'*32:
            entries.append(None); continue
        seq,label,state,crc=struct.unpack('<I20sII',entry)
        require(label==b'\xff'*20,'unknown OTA label')
        require(1<=seq<0xffffffef and crc==(zlib.crc32(entry[:4],0xffffffff)&0xffffffff),'OTA sequence/CRC unsupported')
        require(state in (2,3,4),'unconfirmed OTA transition')
        entries.append((seq,state))
    return entries

def ota_selection(data):
    entries=ota_entries(data)
    valid=[(entry[0],index) for index,entry in enumerate(entries) if entry and entry[1]==2]
    require(valid,'no confirmed OTA selection')
    sequences=[entry[0] for entry in entries if entry]
    require(len(set(sequences))==len(sequences),'ambiguous OTA sequence')
    seq,page=max(valid); return (seq-1)%2,seq,page

def maintenance(folder,source,layout='riscrte-paired-16m-v1'):
    expected,app_data,target,_=LAYOUTS[layout]
    meta=json_object(file_bytes(folder/'candidate.json')); image=file_bytes(folder/'firmware.bin')
    require(type(meta.get('schema')) is int and meta['schema']==1 and meta.get('target')==target and meta.get('source_sha')==source,'maintenance identity')
    if app_data or 'layout' in meta or 'store_abi' in meta:
        require(meta.get('layout')==layout and type(meta.get('store_abi')) is int and meta['store_abi']==(2 if app_data else 1),'maintenance layout identity')
    require(meta.get('bytes')==len(image) and meta.get('sha256')==sha(image),'maintenance digest')
    require(len(image)<=expected['app0'][3],'maintenance slot bound'); esp_image(image)
    require(image[3]>>4==4,'maintenance flash declaration')
    for marker in (b'RISC_OWNER_INSTALLER:1\0', b'RTE_OWNER_MAINTENANCE=1\0', ('RTE_SOURCE='+source).encode()+b'\0', target.encode()+b'\0'):
        require(marker in image,'maintenance markers')
    require(all(('RISC_PAIRED_STORE_ABI:'+str(abi)).encode()+b'\0' not in image for abi in (1,2)),'maintenance must not be ordinary paired firmware')
    return image

def journal_records(journal,app_data):
    records=[]
    for bank in range(2):
        page=journal[bank*4096:(bank+1)*4096]
        if page==b'\xff'*4096:
            records.append(None); continue
        record=parse_record(page[:96],app_data)
        require(record[2]==bank,'journal bank identity')
        trailer=page[96:272]
        if trailer!=b'\xff'*176:
            attempt=struct.unpack('<3I32s32s32s32s32sI',trailer)
            require(attempt[:3]==(0x31545052,1,bank) and attempt[4]==record[6] and attempt[5]==record[7] and attempt[8]==zlib.crc32(trailer[:172])&0xffffffff,'journal provisioning receipt integrity')
        require(page[272:]==b'\xff'*(4096-272),'unknown journal contents')
        records.append(record)
    return records

def preserved_ranges(app_offset,app_bytes,page_offset):
    excluded=sorted(((0x9000,0xf000),(app_offset,app_offset+app_bytes),(page_offset,page_offset+4096)))
    ranges=[]; start=0
    for low,high in excluded:
        if start<low: ranges.append((start,low-start))
        start=high
    if start<FLASH: ranges.append((start,FLASH-start))
    return ranges

def prepare(inventory_path,snapshot,artifact):
    inventory=json_object(file_bytes(inventory_path))
    required={'schema','schema_version','layout','flash_bytes','active_bank','runtime_version','running_firmware_sha256','maintenance_source_sha','quiescent'}
    require(set(inventory)==required,'missing/unknown inventory fields')
    layout=inventory['layout']
    require(inventory['schema']=='riscrte.maintenance-inventory' and type(inventory['schema_version']) is int and inventory['schema_version']==1 and isinstance(layout,str) and layout in LAYOUTS and type(inventory['flash_bytes']) is int and inventory['flash_bytes']==FLASH,'incompatible inventory layout')
    require(type(inventory['active_bank']) is int and inventory['active_bank'] in (0,1) and inventory['quiescent'] is True,'explicit confirmed/quiescent inventory required')
    from check_versions import version
    require(version(inventory['runtime_version'])>=(0,1,31),'ordinary Runtime lacks descriptor-v2 installation support')
    source=inventory['maintenance_source_sha']; require(isinstance(source,str) and len(source)==40 and all(c in '0123456789abcdef' for c in source),'exact maintenance source required')
    expected,app_data,_,_=LAYOUTS[layout]; app_bytes=expected['app0'][3]; abi=2 if app_data else 1
    frozen(snapshot)
    partitions(read_range(snapshot,0x8000,3072),expected)
    require(sha(read_range(snapshot,0,BOOTLOADER_BYTES))==BOOTLOADER_SHA256,'unverified rollback bootloader')
    otadata=read_range(snapshot,OTA,8192); active,seq,page=ota_selection(otadata)
    require(active==inventory['active_bank'],'active-bank inventory mismatch')
    journal=read_range(snapshot,JOURNAL,8192); records=journal_records(journal,app_data)
    require(records[active] is not None,'active journal missing')
    firmware=None
    for bank,record in enumerate(records):
        application=expected['app'+str(bank)]; store=expected['bootfs'+str(bank)]
        if record is None:
            require(all(entry is None or (entry[0]-1)%2!=bank for entry in ota_entries(otadata)),'OTA bank lacks journal')
            require(read_range(snapshot,application[2],application[3])==b'\xff'*application[3] and read_range(snapshot,store[2],store[3])==b'\xff'*store[3],'uncommitted inactive bank contents')
            continue
        image=read_range(snapshot,application[2],record[3]); esp_image(image)
        require(sha(image)==record[6].hex(),'journal firmware identity')
        require(('RISC_PAIRED_STORE_ABI:'+str(abi)).encode()+b'\0' in image and ('RISC_PAIRED_STORE_ABI:'+str(3-abi)).encode()+b'\0' not in image and b'RISC_OWNER_INSTALLER:1\0' not in image,'ordinary runtime ABI markers')
        require(sha(read_range(snapshot,store[2],store[3]))==record[7].hex(),'journal store identity')
        if bank==active: firmware=image
    require(sha(firmware)==inventory['running_firmware_sha256'],'running firmware inventory mismatch')
    require(('RISC_RUNTIME_VERSION:'+inventory['runtime_version']).encode()+b'\0' in firmware,'ordinary runtime version marker')
    target=1-active; backup=read_range(snapshot,APPS[target],app_bytes)
    image=maintenance(artifact,source,layout); entry_image=image+b'\xff'*(app_bytes-len(image))
    target_page=1-page; page_offset=OTA+target_page*4096; original_page=otadata[target_page*4096:(target_page+1)*4096]
    new_seq=seq+1; require((new_seq-1)%2==target,'OTA transition parity')
    sequence=struct.pack('<I',new_seq)
    entry=sequence+b'\xff'*20+struct.pack('<II',0,zlib.crc32(sequence,0xffffffff)&0xffffffff)
    entry_page=entry+b'\xff'*(4096-32)
    payloads={'enter-application.bin':entry_image,'enter-otadata-page.bin':entry_page,'restore-application.bin':backup,'restore-otadata-page.bin':original_page}
    offsets={'enter-application.bin':APPS[target],'restore-application.bin':APPS[target],'enter-otadata-page.bin':page_offset,'restore-otadata-page.bin':page_offset}
    report={'schema':'riscrte.maintenance-plan','schema_version':2,'layout':layout,'active_bank':active,'maintenance_bank':target,'maintenance_source_sha':source,
        'hardware_status':'UNRUN','execution':'offline plan only; no device/flash/reset operation',
        'assets':{name:{'offset':offsets[name],'bytes':len(data),'sha256':sha(data)} for name,data in payloads.items()},
        'preserve':{'nvs':'excluded from reads and every write/restore payload','active_firmware_sha256':sha(firmware),'active_store_sha256':records[active][7].hex(),'bank_journal_sha256':sha(journal),'partition_table':'unchanged',
            'regions':[{'offset':offset,'bytes':size,'sha256':sha(read_range(snapshot,offset,size))} for offset,size in preserved_ranges(APPS[target],app_bytes,page_offset)]},
        'maintenance_ota':{'page_offset':page_offset,'sequence':new_seq,'allowed_restore_states':[0,1,4],'inactive_firmware_sha256':sha(entry_image)},
        'preconditions':['Owner must independently verify the frozen snapshots match the live device in deliberately selected ROM maintenance mode.','Use only this exact layout; this procedure cannot migrate ABI, native firmware, policy rows or product cohort.','Keep the original snapshot, inventory, maintenance artifact and restoration files privately.','Owner-input staging may fail if the existing 24 KiB NVS lacks free space. Two dense profiles are not guaranteed to fit; unrelated keys remain untouched and no erase or resize is permitted.','Verify the named phase before each next write. Torn or unexpected bytes refuse; never infer a safe continuation.'],
        'entry_order':['Verify entry-ready.','Write and read back enter-application.bin; verify application-staged.','Write and read back enter-otadata-page.bin; verify maintenance.','Deliberately boot maintenance, then use the explicit source-matched NVS install command.'],
        'restoration_order':['Deliberately return to ROM maintenance; do not overwrite executing code. Verify maintenance.','Restore and read back restore-application.bin; verify application-restored. Do not boot while the maintenance OTA page still selects this slot.','Restore and read back restore-otadata-page.bin; verify restored.','Deliberately boot ordinary Runtime; hardware outcome remains unqualified. Never restore NVS from the old snapshot.']}
    if app_data:
        _,_,offset,size=expected['appdata']
        report['preserve']['appdata']={'offset':offset,'bytes':size,'sha256':sha(read_range(snapshot,offset,size))}
    return report,payloads

def plan(inventory_path,snapshot,artifact,output):
    require(not output.exists() and not output.is_symlink(),'output already exists')
    report,payloads=prepare(inventory_path,snapshot,artifact)
    output.mkdir(mode=0o700)
    try:
        for name,data in payloads.items(): (output/name).write_bytes(data)
        (output/'plan.json').write_text(json.dumps(report,sort_keys=True,indent=2)+'\n')
        (output/'SHA256SUMS').write_text(''.join(f'{sha(file_bytes(p))}  {p.name}\n' for p in sorted(output.iterdir())))
    except Exception:
        import shutil
        shutil.rmtree(output); raise
    return report

def verify_phase(inventory_path,snapshot,artifact,output,current,phase):
    """Read-only phase admission from the original trusted inputs and a new dump."""
    require(phase in PHASES,'unknown maintenance phase')
    report,payloads=prepare(inventory_path,snapshot,artifact)
    require(json_object(file_bytes(output/'plan.json'))==report,'plan differs from original inputs')
    for name,data in payloads.items():
        require(file_bytes(output/name)==data,'restoration/entry payload differs: '+name)
    frozen(current)
    for region in report['preserve']['regions']:
        require(sha(read_range(current,region['offset'],region['bytes']))==region['sha256'],'preserved flash region changed')
    app_name='enter-application.bin' if phase in ('application-staged','maintenance') else 'restore-application.bin'
    region=report['assets'][app_name]
    require(read_range(current,region['offset'],region['bytes'])==payloads[app_name],'application does not match phase')
    page_offset=report['maintenance_ota']['page_offset']; page=read_range(current,page_offset,4096)
    if phase in ('maintenance','application-restored'):
        state=struct.unpack_from('<I',page,24)[0]
        require(state in (0,1,4),'maintenance OTA state refused')
        normalized=bytearray(page); struct.pack_into('<I',normalized,24,0)
        require(bytes(normalized)==payloads['enter-otadata-page.bin'],'maintenance OTA sequence/page changed')
    else:
        require(page==payloads['restore-otadata-page.bin'],'OTA page does not match phase')
    return {'phase':phase,'verified':True,'hardware_status':'UNRUN','scope':'snapshot comparison only; live state and ROM mode require owner verification'}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('inventory','snapshot','maintenance','output'): p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--verify-snapshot',type=Path); p.add_argument('--phase',choices=PHASES)
    a=p.parse_args()
    if a.verify_snapshot is not None or a.phase is not None:
        if a.verify_snapshot is None or a.phase is None:
            p.error('--verify-snapshot and --phase are required together')
        result=verify_phase(a.inventory,a.snapshot,a.maintenance,a.output,a.verify_snapshot,a.phase)
        print(json.dumps(result,sort_keys=True))
    else:
        plan(a.inventory,a.snapshot,a.maintenance,a.output)
        print('Prepared private offline maintenance plan; NVS excluded and app-data preserved; hardware UNRUN.')
if __name__=='__main__': main()
