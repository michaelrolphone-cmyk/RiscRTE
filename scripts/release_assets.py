#!/usr/bin/env python3
"""Offline, exact-source firmware asset custody, derived from Reader's release preflight.
Never contacts GitHub, publishes, uploads, formats storage or accesses a device.
"""
import argparse, hashlib, io, json, os, re, shutil, struct, subprocess
from pathlib import Path
from check_versions import firmware, version
ROOT=Path(__file__).resolve().parents[1]
OUTPUT=ROOT/'dist/release'
TARGET='esp32s3-baseline'
MAX_BYTES=32*1024*1024
INPUT_FILES={'platformio.ini','partitions.csv','requirements-ci.txt','scripts/build_apps.py','scripts/embed_store.py','scripts/reproducible_build.py'}
def require(ok,why):
    if not ok: raise ValueError(why)
def sha(data):return hashlib.sha256(data).hexdigest()
def file_bytes(path):
    require(path.is_file() and not path.is_symlink() and 0<path.stat().st_size<=MAX_BYTES,f'missing/empty/unsafe asset: {path.name}')
    data=path.read_bytes();require(len(data)<=MAX_BYTES,'asset exceeds bound');return data
def head():return subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
def fingerprint():
    names=subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode().split('\0')
    records={n:sha(file_bytes(ROOT/n)) for n in sorted(names) if n and (n.startswith(('src/','lib/','sdk/','data/','apps/')) or n in INPUT_FILES)}
    return sha(json.dumps(records,sort_keys=True,separators=(',',':')).encode())
def esp_image(data):
    from esptool.bin_image import ESP32S3FirmwareImage
    require(len(data)>=24 and data[0]==0xe9 and struct.unpack_from('<H',data,12)[0]==9,'not an ESP32-S3 image')
    image=ESP32S3FirmwareImage(io.BytesIO(data))
    require(image.checksum==image.calculate_checksum(),'ESP image checksum mismatch')
    require(image.append_digest and image.stored_digest==image.calc_digest,'ESP image SHA-256 mismatch')
    require(len(data)==image.data_length+32,'ESP image trailing/truncated bytes')
def elf(data,dynamic=False):
    from elftools.elf.elffile import ELFFile
    e=ELFFile(io.BytesIO(data))
    require(e.elfclass==32 and e.little_endian and e['e_machine']=='EM_XTENSA','not Xtensa ELF32')
    require(e['e_type']==('ET_DYN' if dynamic else 'ET_EXEC'),'unexpected ELF type')
    if dynamic:
        table=e.get_section_by_name('.dynsym');require(table is not None,'missing dynsym')
        require(any(s.name=='app_main' and s['st_info']['type']=='STT_FUNC' and s['st_shndx']!='SHN_UNDEF' for s in table.iter_symbols()),'missing app_main')
def partition_table(data):
    require(len(data)==3072,'wrong partition-table length')
    entries={}; raw=b''; verified=False
    for pos in range(0,len(data),32):
        row=data[pos:pos+32];magic=struct.unpack_from('<H',row)[0]
        if magic==0x50aa:
            _,kind,subtype,offset,size,label,flags=struct.unpack('<HBBII16sI',row)
            label=label.split(b'\0')[0].decode();require(label not in entries,'duplicate partition label')
            entries[label]=(kind,subtype,offset,size);raw+=row
        elif magic==0xebeb:
            require(row[16:]==hashlib.md5(raw).digest(),'partition MD5 mismatch');verified=True;break
        else:raise ValueError('partition digest missing or invalid entry')
    require(verified and entries=={'nvs':(1,2,0x9000,0x6000),'factory':(0,0,0x10000,0x300000),'bootfs':(1,0x82,0x310000,0x4f0000)},'unexpected deployment layout')
    return entries
def names(v):
    p=f'riscrte_{TARGET}_{v}'
    return {'app':p+'-app.bin','merged':p+'-merged.bin','symbols':p+'.elf','bootloader':p+'-bootloader.bin','partitions':p+'-partitions.bin','bootfs':p+'-bootfs.bin','default':'default.elf','default_manifest':'default.json','boot':'boot.json','board':'board.json'}
def validate_payloads(folder,v):
    n=names(v);data={k:file_bytes(folder/f) for k,f in n.items()}
    esp_image(data['app']);esp_image(data['bootloader']);elf(data['symbols']);elf(data['default'],True)
    require(len(data['app'])<=0x300000 and len(data['bootloader'])<=0x8000,'firmware exceeds its partition')
    partition_table(data['partitions']);require(len(data['bootfs'])==0x4f0000,'bootfs has wrong size')
    require(len(data['merged'])==0x800000,'merged image must cover declared 8 MiB layout')
    segments=[(0,data['bootloader']),(0x8000,data['partitions']),(0x10000,data['app']),(0x310000,data['bootfs'])]
    expected=bytearray(b'\xff'*0x800000)
    for offset,blob in segments:expected[offset:offset+len(blob)]=blob
    require(data['merged']==expected,'merged offsets/bytes do not match separate images')
    require(set(data['bootfs'])!={255},'empty bootfs image')
    # SPIFFS tool extraction below additionally proves the complete actual files.
    boot=json.loads(data['boot']);app=json.loads(data['default_manifest'])
    require(boot==json.loads((ROOT/'data/boot.json').read_text()),'staged boot configuration differs from source')
    require(data['board']==(ROOT/'data/board.json').read_bytes(),'staged board differs from source')
    require(app==json.loads((ROOT/'apps/heartbeat/manifest.json').read_text()),'default manifest differs from source')
    require(boot['default_app']=='default.elf' and boot['board']=='board.json','unexpected baseline boot paths')
    return data

def stage(source):
    require(re.fullmatch('[0-9a-f]{40}',source) and source==head(),'source SHA differs from checked-out candidate')
    require(subprocess.run(['git','diff','--quiet','HEAD'],cwd=ROOT).returncode==0,'working tree differs from source SHA')
    require(not subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=ROOT).strip(),'untracked candidate inputs must be committed')
    v=firmware((ROOT/'platformio.ini').read_text());n=names(v)
    if OUTPUT.exists():shutil.rmtree(OUTPUT)
    OUTPUT.mkdir(parents=True)
    build=ROOT/'.pio/build/esp32s3'
    sources={'app':build/'firmware.bin','symbols':build/'firmware.elf','bootloader':build/'bootloader.bin','partitions':build/'partitions.bin','bootfs':build/'spiffs.bin','default':ROOT/'build/store/default.elf','default_manifest':ROOT/'apps/heartbeat/manifest.json','boot':ROOT/'build/store/boot.json','board':ROOT/'build/store/board.json'}
    for key,path in sources.items():(OUTPUT/n[key]).write_bytes(file_bytes(path))
    merged=bytearray(b'\xff'*0x800000)
    for key,offset in [('bootloader',0),('partitions',0x8000),('app',0x10000),('bootfs',0x310000)]:
        data=file_bytes(OUTPUT/n[key]);require(offset+len(data)<=len(merged),'image exceeds flash');merged[offset:offset+len(data)]=data
    (OUTPUT/n['merged']).write_bytes(merged)
    validate_payloads(OUTPUT,v)
    # Verify the filesystem image by extracting with the same pinned SPIFFS tool.
    core=Path(os.environ.get('PLATFORMIO_CORE_DIR',Path.home()/'.platformio'))
    tool=core/'packages/tool-mkspiffs/mkspiffs';extracted=ROOT/'build/verify-store'
    if extracted.exists():shutil.rmtree(extracted)
    extracted.mkdir()
    subprocess.run([str(tool),'-u',str(extracted),'-b','4096','-p','256',str(OUTPUT/n['bootfs'])],check=True)
    require({p.name for p in extracted.iterdir()}=={'boot.json','board.json','default.elf'},'bootfs missing/unexpected members')
    for key in ['boot','board','default']:require(file_bytes(extracted/n[key])==file_bytes(OUTPUT/n[key]),'bootfs content mismatch')
    assets={f:{'bytes':len(file_bytes(OUTPUT/f)),'sha256':sha(file_bytes(OUTPUT/f))} for f in sorted(n.values())}
    record={'schema':1,'product':'firmware','version':v,'target':TARGET,'source_sha':source,'source_fingerprint':fingerprint(),'tag':f'firmware-v{v}','layout':{'flash_bytes':0x800000,'app_offset':0x10000,'bootfs_offset':0x310000,'bootfs_bytes':0x4f0000,'merged_offset':0,'storage':'spiffs','existing_media':'separate-owner-approved-deployment-required'},'build':{'platformio':'6.1.19','platform':'espressif32@6.13.0','arduino':'3.20017.241212+sha.dcc1105b','gcc':'8.4.0+2021r2-patch5','source_date_epoch':subprocess.check_output(['git','show','-s','--format=%ct',source],cwd=ROOT,text=True).strip()},'assets':assets}
    (OUTPUT/'release.json').write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
    checks={**assets,'release.json':{'sha256':sha(file_bytes(OUTPUT/'release.json'))}}
    (OUTPUT/'SHA256SUMS').write_text(''.join(f'{meta["sha256"]}  {name}\n' for name,meta in sorted(checks.items())))
    print('Staged verified release bundle:',OUTPUT)

def verify(source,folder=OUTPUT):
    record=json.loads(file_bytes(folder/'release.json'))
    require(record['schema']==1 and record['product']=='firmware' and record['target']==TARGET,'wrong release identity')
    v=firmware((ROOT/'platformio.ini').read_text())
    require(record['version']==v and record['tag']==f'firmware-v{v}','release/source version mismatch')
    require(record['source_sha']==source and source==head(),'release source SHA mismatch')
    require(record['source_fingerprint']==fingerprint(),'release source fingerprint mismatch')
    expected=set(names(v).values());require(set(record['assets'])==expected,'missing/unexpected asset record')
    require({p.name for p in folder.iterdir()}==expected|{'release.json','SHA256SUMS'},'missing/unexpected bundle file')
    for name,meta in record['assets'].items():
        data=file_bytes(folder/name);require(meta=={'bytes':len(data),'sha256':sha(data)},f'asset hash/length mismatch: {name}')
    checks={**record['assets'],'release.json':{'sha256':sha(file_bytes(folder/'release.json'))}}
    expected_sums=''.join(f'{meta["sha256"]}  {name}\n' for name,meta in sorted(checks.items()))
    require((folder/'SHA256SUMS').read_text()==expected_sums,'checksum manifest mismatch')
    validate_payloads(folder,v)
    print('Verified exact-source release assets:',source)
    return record
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('action',choices=['stage','verify']);p.add_argument('--source-sha',required=True);a=p.parse_args()
    (stage if a.action=='stage' else verify)(a.source_sha)
