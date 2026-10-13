#!/usr/bin/env python3
"""Offline full-image composition preserving all baseline app/provider bytes.

Requires the two exact delivered baselines and compiled recovery artifacts.
Never connects to a device. Full output images overwrite internal user data.
"""
from __future__ import annotations
import argparse, hashlib, json, re, struct, sys
from pathlib import Path

BASELINES = {
    'x4': ('41b3447bbac9d67c82101e6a04730435f753ea5f9404fc4b1c85bae09ed7efa8', '0.1.103', '0.1.104', 'a91edf3fada6a559ec107186dd163d0c8da10d29', 'xteink-x4-pro'),
    'watch': ('f8195d2af2dfdcc9f2c0413883efd97f6efe4d62eb90cfd4e5cab5aec5332cb6', '1.1.20', '1.1.21', '06da76aa7f074220b74a282badca0ed0061f1643', 'twatch-s3'),
}
def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()
def require(ok: bool, why: str) -> None:
    if not ok: raise ValueError(why)
def image_info(b: bytes) -> dict:
    require(len(b)>=24 and b[0]==0xe9 and b[1] in range(1,17), 'Invalid ESP image')
    pos=24;checksum=0xef;segments=[]
    for _ in range(b[1]):
        require(pos+8<=len(b), 'Truncated segment header')
        address,n=struct.unpack_from('<II',b,pos);pos+=8
        require(n%4==0 and pos+n<=len(b), 'Truncated or unaligned segment')
        payload=b[pos:pos+n]
        for byte in payload: checksum^=byte
        segments.append({'address':address,'bytes':n,'sha256':sha(payload)})
        pos+=n
    checksum_end=(pos+16)&~15
    require(checksum_end<=len(b) and b[checksum_end-1]==checksum,'ESP checksum mismatch')
    require(b[23]==1, 'Expected appended ESP image digest')
    require(b[checksum_end:checksum_end+32]==hashlib.sha256(b[:checksum_end]).digest(),'ESP image digest mismatch')
    require(checksum_end+32==len(b), 'Unexpected native trailing bytes')
    return {'bytes':len(b),'sha256':sha(b),'segments':segments,'checksum_and_digest_verified':True}
def patch_existing_file(image: bytes, name: str, old: bytes, new: bytes) -> bytes:
    """Replace only equal-length object data; preserve page/index allocation."""
    require(len(old)==len(new), 'In-place metadata must preserve file length')
    headers=[]
    for block in range(len(image)//4096):
        for entry in range(15):
            obj=struct.unpack_from('<H',image,block*4096+entry*2)[0]
            if obj in (0,65535):continue
            page=block*16+1+entry;off=page*256
            ident,span,flags=struct.unpack_from('<HHB',image,off)
            if obj&0x8000 and span==0 and image[off+13:off+45].split(b'\0')[0]==('/'+name).encode():
                headers.append((off,obj))
    require(len(headers)==1,'Missing or ambiguous metadata object')
    header,obj=headers[0]
    require(struct.unpack_from('<I',image,header+8)[0]==len(old),'Metadata size mismatch')
    require(len(new)<=103*251,'Metadata needs unsupported continuation index')
    out=bytearray(image)
    for span in range((len(new)+250)//251):
        page=struct.unpack_from('<H',image,header+49+2*span)[0];off=page*256
        ident,actual_span,flags=struct.unpack_from('<HHB',image,off)
        require(ident==(obj&0x7fff) and actual_span==span and flags&4,'Bad metadata page relation')
        chunk=new[span*251:(span+1)*251]
        require(image[off+5:off+5+len(chunk)]==old[span*251:span*251+len(chunk)],'Metadata page content mismatch')
        out[off+5:off+5+len(chunk)]=chunk
    return bytes(out)
def main() -> None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--device',choices=BASELINES,required=True)
    for name in ('baseline','native','runtime','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--recipe-commit',required=True)
    a=p.parse_args(); require(re.fullmatch('[0-9a-f]{40}',a.recipe_commit) is not None,'Exact recipe commit required')
    sys.path.insert(0,str(a.runtime/'scripts'))
    from spiffs_image import read_image
    from paired_bank_images import initial_bank_state, initial_otadata, parse_record
    expected,oldversion,version,native_source,product=BASELINES[a.device]
    raw=a.baseline.read_bytes();require(len(raw)==0x1000000 and sha(raw)==expected,'Wrong delivered baseline')
    store=read_image(raw[0x2f0000:0x800000],0x510000)
    oldcohort=json.loads(store['cohort.json']); require(oldcohort['version']==oldversion and oldcohort['product']==product,'Wrong cohort')
    oldfw=raw[0x10000:0x10000+oldcohort['firmware_size']]
    require(sha(oldfw)==oldcohort['firmware_sha256'],'Original cohort/native mismatch')
    original_native=image_info(oldfw)
    pair=parse_record(raw[0xff2000:0xff2000+96],True)
    require(pair[3]==len(oldfw) and pair[6]==hashlib.sha256(oldfw).digest() and pair[7]==hashlib.sha256(raw[0x2f0000:0x800000]).digest(),'Original bank mismatch')
    require(raw[0xff0000:0xff2000]==initial_otadata(),'Baseline is not an initial bank0 image')
    native=(a.native/'firmware.bin').read_bytes();native_info=image_info(native)
    require(len(native)<=0x260000,'Native exceeds app0 partition')
    require(native[2:4]==oldfw[2:4], 'Native flash mode/speed/size changed')
    require(set(re.findall(rb'RTE_SOURCE=[0-9a-f]{40}',native))=={('RTE_SOURCE='+native_source).encode()},'Wrong compiled source')
    for marker in (b'RISC_PAIRED_STORE_ABI:2\0', b'RISC_APP_POLICY_ROWS:24\0', b'RISC_APP_REQUIREMENT_ROWS:24\0', ('RISC_RUNTIME_VERSION:'+oldcohort['runtime_version']).encode()+b'\0'):
        require(marker in native,'Compiled feature marker missing: '+repr(marker))
    for name,offset in (('bootloader.bin',0),('partitions.bin',0x8000)):
        part=(a.native/name).read_bytes();require(raw[offset:offset+len(part)]==part, 'Compiled boot chain mismatch: '+name)
    cohort={**oldcohort,'firmware_sha256':sha(native),'firmware_size':len(native),'version':version,'source_repo':'michaelrolphone-cmyk/RiscRTE','source_revision':a.recipe_commit}
    data=(json.dumps(cohort,indent=2,sort_keys=True)+'\n').encode()
    require(len(data)<=len(store['cohort.json']),'Cohort exceeds existing allocation')
    data+=b' '*(len(store['cohort.json'])-len(data))
    fs=patch_existing_file(raw[0x2f0000:0x800000],'cohort.json',store['cohort.json'],data)
    after=read_image(fs,0x510000)
    require(set(after)==set(store) and [n for n in after if after[n]!=store[n]]==['cohort.json'],'Unexpected logical store changes')
    bank=initial_bank_state(native,fs,True);newpair=parse_record(bank[:96],True)
    require(newpair[3]==len(native) and newpair[6]==hashlib.sha256(native).digest() and newpair[7]==hashlib.sha256(fs).digest(),'New paired identity mismatch')
    result=bytearray(raw);result[0x10000:0x270000]=native+b'\xff'*(0x260000-len(native));result[0x2f0000:0x800000]=fs;result[0xff2000:0xff4000]=bank
    untouched=[(0,0x10000),(0x270000,0x2f0000),(0x800000,0xff2000),(0xff4000,0x1000000)]
    require(all(result[lo:hi]==raw[lo:hi] for lo,hi in untouched),'Unrelated flash region changed')
    a.output.mkdir(parents=True,exist_ok=True)
    name=('X4' if a.device=='x4' else 'T-Watch-S3')+'-'+version+'-Display-Fix-full-0x0.bin'
    target=a.output/name;target.write_bytes(result)
    disk=target.read_bytes();require(disk==result and read_image(disk[0x2f0000:0x800000],0x510000)==after,'Final image readback failed')
    root=a.output/(a.device+'-store');root.mkdir(exist_ok=True)
    for name,data in after.items():
        q=root/name;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(data)
    boot=json.loads(after['boot.json'])
    report={'schema':'riscrte.display-recovery-build','schema_version':1,'device':a.device,'version':version,'recipe_commit':a.recipe_commit,'compiled_runtime_source':native_source,'native':native_info,'baseline':{'name':a.baseline.name,'sha256':expected,'cohort':oldcohort,'native':original_native},'image':{'name':target.name,'bytes':len(result),'sha256':sha(result),'flash_offset':0},'cohort':cohort,'store':{'files':len(after),'elfs':sum(n.endswith('.elf') for n in after),'app_policy_entries':len(boot['app_capabilities']),'providers':len(boot['drivers']),'changed_files':['cohort.json'],'all_application_and_provider_bytes_preserved':True,'physical_page_layout_preserved':True,'sha256':sha(fs),'inventory':{n:sha(v) for n,v in sorted(after.items())}},'unchanged_regions':[{'offset':lo,'bytes':hi-lo,'sha256':sha(raw[lo:hi])} for lo,hi in untouched],'bank_sha256_and_crc_verified':True,'flash_warning':'Full initial image at offset 0 overwrites internal settings, Wi-Fi credentials, Bluetooth bonds and app data. SD is not modified.','hardware_tested':False}
    (a.output/(a.device+'-build-report.json')).write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report['image'],indent=2))
if __name__=='__main__':main()
