#!/usr/bin/env python3
"""Both offline layouts, phase interruption/refusal, and exact built artifacts."""
import argparse, json, struct, sys, tempfile, unittest, zlib
from contextlib import ExitStack
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import maintenance_plan as p
import paired_bank_images as bank


def inventory(firmware,source='a'*40,version='0.1.76',app_data=False,active=0):
    return {'schema':'riscrte.maintenance-inventory','schema_version':1,'layout':bank.APP_DATA_LAYOUT if app_data else bank.LAYOUT,'flash_bytes':p.FLASH,'active_bank':active,'runtime_version':version,'running_firmware_sha256':p.sha(firmware),'maintenance_source_sha':source,'quiescent':True}

def partition_image(app_data):
    raw=b''
    for label,(kind,subtype,offset,size) in (p.APP_DATA_EXPECTED if app_data else p.EXPECTED).items():
        raw+=struct.pack('<HBBII16sI',0x50aa,kind,subtype,offset,size,label.encode(),0)
    raw+=b'\xeb\xeb'+b'\xff'*14+__import__('hashlib').md5(raw).digest()
    return raw+b'\xff'*(3072-len(raw))

def snapshot(root,firmware,store,bootloader,partitions,app_data=False):
    expected=p.APP_DATA_EXPECTED if app_data else p.EXPECTED
    raw=bytearray(b'\xff'*p.FLASH); raw[:len(bootloader)]=bootloader; raw[0x8000:0x8000+len(partitions)]=partitions
    raw[0x9000:0xf000]=(b'PRIVATE-DUMMY-NVS'*2000)[:0x6000]
    if app_data:
        raw[bank.APP_DATA_OFFSET:bank.APP_DATA_OFFSET+bank.APP_DATA_BYTES]=(b'PRIVATE-APP-DATA!'*40000)[:bank.APP_DATA_BYTES]
    raw[p.APPS[0]:p.APPS[0]+len(firmware)]=firmware
    at=expected['bootfs0'][2]; raw[at:at+len(store)]=store
    raw[p.OTA:p.OTA+8192]=bank.initial_otadata(); raw[p.JOURNAL:p.JOURNAL+8192]=bank.initial_bank_state(firmware,store,app_data)
    assert len(raw)==p.FLASH
    path=root/'snapshot.bin'; path.write_bytes(raw); return path

def synthetic_firmware(app_data):
    return b'\xe9\x00\x00\x40'+('RISC_PAIRED_STORE_ABI:'+str(2 if app_data else 1)).encode()+b'\0RISC_RUNTIME_VERSION:0.1.76\0'+b'x'*100

def artifact(root,app_data,source='a'*40):
    layout=bank.APP_DATA_LAYOUT if app_data else bank.LAYOUT
    target=p.LAYOUTS[layout][2]
    image=b'\xe9\x00\x00\x40RISC_OWNER_INSTALLER:1\0RTE_OWNER_MAINTENANCE=1\0RTE_SOURCE='+source.encode()+b'\0'+('RISC_OWNER_TARGET:'+target).encode()+b'\0RISC_RUNTIME_VERSION:0.1.76\0'
    path=root/'artifact'; path.mkdir()
    (path/'firmware.bin').write_bytes(image)
    (path/'candidate.json').write_text(json.dumps({'schema':1,'target':target,'source_sha':source,'layout':layout,'store_abi':2 if app_data else 1,'bytes':len(image),'sha256':p.sha(image)}))
    return path

class PlanTest(unittest.TestCase):
    def fixture(self,root,app_data=False,active=0):
        fw=synthetic_firmware(app_data); store=b'z'*(bank.APP_DATA_STORE_BYTES if app_data else bank.STORE_BYTES); loader=b'l'*bank.BOOTLOADER_BYTES
        snap=snapshot(root,fw,store,loader,partition_image(app_data),app_data)
        if active:
            raw=bytearray(snap.read_bytes()); expected=p.APP_DATA_EXPECTED if app_data else p.EXPECTED
            raw[p.APPS[1]:p.APPS[1]+len(fw)]=fw
            at=expected['bootfs1'][2]; raw[at:at+len(store)]=store
            raw[p.JOURNAL+4096:p.JOURNAL+4096+96]=bank.record(1,fw,store,app_data)
            page=bytearray(bank.initial_otadata()[:4096]); struct.pack_into('<I',page,0,2); struct.pack_into('<I',page,28,zlib.crc32(page[:4],0xffffffff)&0xffffffff)
            raw[p.OTA+4096:p.OTA+8192]=page; snap.write_bytes(raw)
        inv=root/'inventory.json'; inv.write_text(json.dumps(inventory(fw,app_data=app_data,active=active)))
        return inv,snap,artifact(root,app_data),loader

    def test_ota_refusal(self):
        base=bank.initial_otadata(); self.assertEqual(p.ota_selection(base),(0,1,0))
        for state in (0,1,5,0xffffffff):
            value=bytearray(base); struct.pack_into('<I',value,24,state)
            with self.assertRaisesRegex(ValueError,'unconfirmed'): p.ota_selection(value)
        with self.assertRaisesRegex(ValueError,'no confirmed'): p.ota_selection(b'\xff'*8192)
        for offset,message in ((28,'CRC'),(100,'unknown OTA'),(4,'label')):
            bad=bytearray(base); bad[offset]^=1
            with self.assertRaisesRegex(ValueError,message): p.ota_selection(bad)
        with self.assertRaisesRegex(ValueError,'ambiguous'): p.ota_selection(base[:4096]*2)
        for seq in (0,0xffffffef,0xfffffff0,0xffffffff):
            bad=bytearray(base); struct.pack_into('<I',bad,0,seq); struct.pack_into('<I',bad,28,zlib.crc32(bad[:4],0xffffffff)&0xffffffff)
            with self.assertRaisesRegex(ValueError,'unsupported'): p.ota_selection(bad)

    def test_inventory_rejections(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); inv=root/'inventory.json'; good=inventory(b'dummy')
            for key in good:
                bad=dict(good); del bad[key]; inv.write_text(json.dumps(bad))
                with self.assertRaisesRegex(ValueError,'inventory fields'): p.plan(inv,root/'missing',root,root/'out')
            for key,value in [('schema_version',True),('flash_bytes',True),('layout',{}),('layout','unknown'),('quiescent',False),('active_bank',True),('maintenance_source_sha','invalid'),('runtime_version','0.1.25')]:
                bad=dict(good); bad[key]=value; inv.write_text(json.dumps(bad))
                with self.assertRaises(ValueError): p.plan(inv,root/'missing',root,root/'out')
            inv.write_text(json.dumps(good))
            with self.assertRaisesRegex(ValueError,'full frozen'): p.plan(inv,root/'missing',root,root/'out')
            self.assertFalse((root/'out').exists())
        with self.assertRaisesRegex(ValueError,'duplicate JSON'): p.json_object(b'{"active_bank":0,"active_bank":1}')
        with self.assertRaisesRegex(ValueError,'JSON object'): p.json_object(b'[]')

    def test_both_layouts_banks_and_preservation(self):
        for app_data in (False,True):
            for active in (0,1):
                with self.subTest(app_data=app_data,active=active), tempfile.TemporaryDirectory() as tmp:
                    root=Path(tmp); inv,snap,art,loader=self.fixture(root,app_data,active); before=snap.read_bytes(); reads=[]
                    original_read=p.read_range
                    def read(path,offset,size):
                        reads.append((offset,size)); return original_read(path,offset,size)
                    with patch.object(p,'esp_image'),patch.object(p,'BOOTLOADER_SHA256',p.sha(loader)),patch.object(p,'read_range',side_effect=read):
                        result=p.plan(inv,snap,art,root/'plan'); repeat=p.plan(inv,snap,art,root/'repeat')
                        self.assertEqual(result,repeat)
                        self.assertEqual(snap.read_bytes(),before)
                        self.assertEqual((root/'plan').stat().st_mode&0o777,0o700)
                        app_bytes=bank.APP_DATA_FIRMWARE_MAX if app_data else p.APP_BYTES
                        self.assertEqual(result['assets']['enter-application.bin']['bytes'],app_bytes)
                        self.assertEqual(result['maintenance_bank'],1-active)
                        self.assertEqual(len(result['assets']),4)
                        for offset,size in reads: self.assertTrue(offset+size<=0x9000 or offset>=0xf000)
                        for name,asset in result['assets'].items():
                            self.assertEqual((root/'plan'/name).read_bytes(),(root/'repeat'/name).read_bytes())
                            if app_data: self.assertTrue(asset['offset']+asset['bytes']<=bank.APP_DATA_OFFSET or asset['offset']>=bank.APP_DATA_OFFSET+bank.APP_DATA_BYTES)
                        self.assertEqual(result['hardware_status'],'UNRUN')
                        if app_data: self.assertEqual(result['preserve']['appdata']['sha256'],p.sha(before[bank.APP_DATA_OFFSET:bank.APP_DATA_OFFSET+bank.APP_DATA_BYTES]))
                        with self.assertRaisesRegex(ValueError,'already exists'): p.plan(inv,snap,art,root/'plan')

    def test_phase_boundaries_and_interrupted_order(self):
        for app_data in (False,True):
            for active in (0,1):
                with self.subTest(app_data=app_data,active=active),tempfile.TemporaryDirectory() as tmp:
                    root=Path(tmp); inv,snap,art,loader=self.fixture(root,app_data,active)
                    with patch.object(p,'esp_image'),patch.object(p,'BOOTLOADER_SHA256',p.sha(loader)):
                        plan=p.plan(inv,snap,art,root/'plan'); current=root/'current.bin'; current.write_bytes(snap.read_bytes())
                        def check(phase): return p.verify_phase(inv,snap,art,root/'plan',current,phase)
                        def write(name):
                            raw=bytearray(current.read_bytes()); item=plan['assets'][name]; raw[item['offset']:item['offset']+item['bytes']]=(root/'plan'/name).read_bytes(); current.write_bytes(raw)
                        check('entry-ready')
                        # OTA first is not accepted as maintenance entry.
                        write('enter-otadata-page.bin')
                        with self.assertRaisesRegex(ValueError,'application'): check('maintenance')
                        write('restore-otadata-page.bin'); write('enter-application.bin'); check('application-staged')
                        with self.assertRaisesRegex(ValueError,'OTA state'): check('maintenance')
                        write('enter-otadata-page.bin'); check('maintenance')
                        at=plan['maintenance_ota']['page_offset']
                        for state in (0,1,4):
                            raw=bytearray(current.read_bytes()); struct.pack_into('<I',raw,at+24,state); current.write_bytes(raw); check('maintenance')
                        raw=bytearray(current.read_bytes()); raw[0x9000:0xf000]=b'n'*0x6000; current.write_bytes(raw); check('maintenance')
                        # Restore application first; NVS installed in maintenance survives.
                        write('restore-application.bin'); check('application-restored')
                        with self.assertRaisesRegex(ValueError,'OTA page'): check('restored')
                        write('restore-otadata-page.bin'); check('restored')
                        self.assertEqual(current.read_bytes()[0x9000:0xf000],b'n'*0x6000)

    def test_phase_refuses_torn_or_changed_regions(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); inv,snap,art,loader=self.fixture(root,True)
            with patch.object(p,'esp_image'),patch.object(p,'BOOTLOADER_SHA256',p.sha(loader)):
                plan=p.plan(inv,snap,art,root/'plan'); raw=bytearray(snap.read_bytes())
                for name in ('enter-application.bin','enter-otadata-page.bin'):
                    a=plan['assets'][name]; raw[a['offset']:a['offset']+a['bytes']]=(root/'plan'/name).read_bytes()
                current=root/'current.bin'; at=plan['maintenance_ota']['page_offset']
                # Even the last byte of app-data, stores and unallocated space is protected.
                for offset in (0,0x8000,0x10000,0x270000,0x2effff,0x2f0000,0xae0000,p.JOURNAL,p.OTA,0xfffffe,p.APPS[1]+1,at+28,at+100):
                    changed=bytearray(raw); changed[offset]^=1; current.write_bytes(changed)
                    with self.subTest(offset=hex(offset)),self.assertRaises(ValueError): p.verify_phase(inv,snap,art,root/'plan',current,'maintenance')
                for state in (2,3,5,0xffffffff):
                    changed=bytearray(raw); struct.pack_into('<I',changed,at+24,state); current.write_bytes(changed)
                    with self.assertRaisesRegex(ValueError,'OTA state'): p.verify_phase(inv,snap,art,root/'plan',current,'maintenance')
                changed=bytearray(raw); struct.pack_into('<I',changed,at,4); struct.pack_into('<I',changed,at+28,zlib.crc32(changed[at:at+4],0xffffffff)&0xffffffff); current.write_bytes(changed)
                with self.assertRaisesRegex(ValueError,'sequence'): p.verify_phase(inv,snap,art,root/'plan',current,'maintenance')
                current.write_bytes(raw); payload=root/'plan/restore-application.bin'; bad=bytearray(payload.read_bytes()); bad[0]^=1; payload.write_bytes(bad)
                with self.assertRaisesRegex(ValueError,'payload differs'): p.verify_phase(inv,snap,art,root/'plan',current,'maintenance')

    def test_journal_and_snapshot_refusal(self):
        for app_data in (False,True):
            with tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp); inv,snap,art,loader=self.fixture(root,app_data,1); good=snap.read_bytes()
                with patch.object(p,'esp_image'),patch.object(p,'BOOTLOADER_SHA256',p.sha(loader)):
                    for offset in (0x8000,0,p.JOURNAL+95,p.JOURNAL+4096+95,p.JOURNAL+96,p.JOURNAL+300,p.APPS[0],p.APPS[1],p.LAYOUTS[json.loads(inv.read_text())['layout']][0]['bootfs0'][2]):
                        raw=bytearray(good); raw[offset]^=1; snap.write_bytes(raw)
                        with self.subTest(app_data=app_data,offset=hex(offset)),self.assertRaises(ValueError): p.plan(inv,snap,art,root/'bad')
                        self.assertFalse((root/'bad').exists())
                    snap.write_bytes(good); data=json.loads(inv.read_text()); data['active_bank']=0; inv.write_text(json.dumps(data))
                    with self.assertRaisesRegex(ValueError,'active-bank'): p.plan(inv,snap,art,root/'bad')

    def test_valid_receipts_and_corrupt_inactive_journal(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); inv,snap,art,loader=self.fixture(root,True,1); raw=bytearray(snap.read_bytes())
            records=p.journal_records(raw[p.JOURNAL:p.JOURNAL+8192],True)
            for index,record in enumerate(records):
                other=records[1-index]
                trailer=struct.pack('<3I32s32s32s32s32s',0x31545052,1,index,b'd'*32,record[6],record[7],other[6],other[7]); trailer+=struct.pack('<I',zlib.crc32(trailer)&0xffffffff)
                at=p.JOURNAL+index*4096+96; raw[at:at+176]=trailer
            snap.write_bytes(raw)
            with patch.object(p,'esp_image'),patch.object(p,'BOOTLOADER_SHA256',p.sha(loader)):
                p.plan(inv,snap,art,root/'plan')
                raw[p.JOURNAL+96+12+32]^=1; snap.write_bytes(raw)
                with self.assertRaisesRegex(ValueError,'receipt integrity'): p.plan(inv,snap,art,root/'bad')

    def test_artifact_layout_and_marker_refusal(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); art=artifact(root,True); meta=json.loads((art/'candidate.json').read_text()); image=(art/'firmware.bin').read_bytes()
            with patch.object(p,'esp_image'):
                p.maintenance(art,'a'*40,bank.APP_DATA_LAYOUT)
                with self.assertRaisesRegex(ValueError,'identity'): p.maintenance(art,'a'*40,bank.LAYOUT)
                for key,value in [('layout',bank.LAYOUT),('store_abi',1),('bytes',1),('source_sha','b'*40)]:
                    bad=dict(meta); bad[key]=value; (art/'candidate.json').write_text(json.dumps(bad))
                    with self.assertRaises(ValueError): p.maintenance(art,'a'*40,bank.APP_DATA_LAYOUT)
                for suffix in (b'RISC_PAIRED_STORE_ABI:1\0',b'RISC_PAIRED_STORE_ABI:2\0'):
                    bad_image=image+suffix; bad=dict(meta,bytes=len(bad_image),sha256=p.sha(bad_image)); (art/'candidate.json').write_text(json.dumps(bad)); (art/'firmware.bin').write_bytes(bad_image)
                    with self.assertRaisesRegex(ValueError,'ordinary paired'): p.maintenance(art,'a'*40,bank.APP_DATA_LAYOUT)

    def test_abi2_cannot_be_relabeled_as_legacy_abi1(self):
        with tempfile.TemporaryDirectory() as tmp:
            art=artifact(Path(tmp),True)
            meta=json.loads((art/'candidate.json').read_text())
            del meta['layout']; del meta['store_abi']
            meta['target']='esp32s3-16mb-maintenance'
            (art/'candidate.json').write_text(json.dumps(meta))
            with patch.object(p,'esp_image'),self.assertRaisesRegex(ValueError,'target marker'):
                p.maintenance(art,'a'*40)
            # Adding the expected marker must not hide a foreign marker.
            image=(art/'firmware.bin').read_bytes()+b'RISC_OWNER_TARGET:esp32s3-16mb-maintenance\0'
            meta.update(bytes=len(image),sha256=p.sha(image))
            (art/'candidate.json').write_text(json.dumps(meta)); (art/'firmware.bin').write_bytes(image)
            with patch.object(p,'esp_image'),self.assertRaisesRegex(ValueError,'target marker'):
                p.maintenance(art,'a'*40)

    def test_legacy_abi1_artifact_remains_supported(self):
        with tempfile.TemporaryDirectory() as tmp:
            art=artifact(Path(tmp),False)
            meta=json.loads((art/'candidate.json').read_text())
            del meta['layout']; del meta['store_abi']
            image=(art/'firmware.bin').read_bytes().replace(b'RISC_OWNER_TARGET:esp32s3-16mb-maintenance\0',b'')
            meta.update(bytes=len(image),sha256=p.sha(image))
            (art/'candidate.json').write_text(json.dumps(meta)); (art/'firmware.bin').write_bytes(image)
            with patch.object(p,'esp_image'):
                self.assertEqual(p.maintenance(art,'a'*40),image)

    def test_no_nvs_read(self):
        for offset,size in ((0x9000,1),(0x8fff,2),(0xefff,2)):
            with self.assertRaisesRegex(ValueError,'NVS read forbidden'): p.read_range(Path('/not-opened'),offset,size)


def real(seed,maintenance):
    with tempfile.TemporaryDirectory() as tmp:
        root=Path(tmp); metadata=json.loads((seed/'seed.json').read_text()); fw=(seed/'firmware.bin').read_bytes(); app_data=metadata['layout']==bank.APP_DATA_LAYOUT
        snap=snapshot(root,fw,(seed/'bootfs0.bin').read_bytes(),(seed/'bootloader.bin').read_bytes(),(seed/'partitions.bin').read_bytes(),app_data)
        inv=root/'inventory.json'; inv.write_text(json.dumps(inventory(fw,json.loads((maintenance/'candidate.json').read_text())['source_sha'],metadata['firmware_version'],app_data)))
        a=p.plan(inv,snap,maintenance,root/'one'); b=p.plan(inv,snap,maintenance,root/'two'); assert a==b
        for f in (root/'one').iterdir(): assert f.read_bytes()==(root/'two'/f.name).read_bytes()
        current=root/'current.bin'; raw=bytearray(snap.read_bytes()); current.write_bytes(raw)
        def check(phase): p.verify_phase(inv,snap,maintenance,root/'one',current,phase)
        check('entry-ready')
        for name,phase in (('enter-application.bin','application-staged'),('enter-otadata-page.bin','maintenance'),('restore-application.bin','application-restored'),('restore-otadata-page.bin','restored')):
            item=a['assets'][name]; raw[item['offset']:item['offset']+item['bytes']]=(root/'one'/name).read_bytes(); current.write_bytes(raw); check(phase)
        assert current.read_bytes()==snap.read_bytes()
        print('Exact-artifact offline entry/restoration: deterministic, '+metadata['layout']+', all phases checked, NVS excluded, app-data preserved; hardware UNRUN')
if __name__=='__main__':
    if '--seed' in sys.argv:
        parser=argparse.ArgumentParser(); parser.add_argument('--seed',type=Path,required=True); parser.add_argument('--maintenance',type=Path,required=True); args=parser.parse_args(); real(args.seed,args.maintenance)
    else: unittest.main()
