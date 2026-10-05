#!/usr/bin/env python3
"""Offline plan models plus optional exact built-artifact validation."""
import argparse,hashlib,json,struct,sys,tempfile,unittest,zlib
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import maintenance_plan as p
import paired_bank_images as bank

def inventory(firmware,source='a'*40,version='0.1.31'):
    return {'schema':'riscrte.maintenance-inventory','schema_version':1,'layout':'riscrte-paired-16m-v1','flash_bytes':p.FLASH,'active_bank':0,'runtime_version':version,'running_firmware_sha256':p.sha(firmware),'maintenance_source_sha':source,'quiescent':True}
def snapshot(root,firmware,store,bootloader,partitions):
    raw=bytearray(b'\xff'*p.FLASH);raw[:len(bootloader)]=bootloader;raw[0x8000:0x8000+len(partitions)]=partitions
    raw[0x9000:0xf000]=(b'PRIVATE-DUMMY-NVS'*2000)[:0x6000]
    raw[p.APPS[0]:p.APPS[0]+len(firmware)]=firmware;raw[p.STORES[0]:p.STORES[0]+len(store)]=store
    raw[p.OTA:p.OTA+8192]=bank.initial_otadata();raw[p.JOURNAL:p.JOURNAL+8192]=bank.initial_bank_state(firmware,store)
    assert len(raw)==p.FLASH
    path=root/'snapshot.bin';path.write_bytes(raw);return path
class PlanTest(unittest.TestCase):
    def test_ota_refusal(self):
        self.assertEqual(p.ota_selection(bank.initial_otadata()),(0,1,0))
        for state in (0,1):
            value=bytearray(bank.initial_otadata());struct.pack_into('<I',value,24,state)
            with self.assertRaisesRegex(ValueError,'unconfirmed'):p.ota_selection(value)
        with self.assertRaisesRegex(ValueError,'no confirmed'):p.ota_selection(b'\xff'*8192)
    def test_inventory_rejections(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);inv=root/'inventory.json';good=inventory(b'dummy')
            for key in good:
                bad=dict(good);del bad[key];inv.write_text(json.dumps(bad))
                with self.assertRaisesRegex(ValueError,'inventory fields'):p.plan(inv,root/'missing-snapshot',root,root/'out')
            for key,value in [('schema_version',True),('layout','riscrte-paired-appdata-v2'),('quiescent',False),('active_bank',True),('maintenance_source_sha','invalid'),('runtime_version','0.1.25')]:
                bad=dict(good);bad[key]=value;inv.write_text(json.dumps(bad))
                with self.assertRaises(ValueError):p.plan(inv,root/'missing-snapshot',root,root/'out')
            inv.write_text(json.dumps(good))
            with self.assertRaisesRegex(ValueError,'full frozen ABI1 snapshot'):p.plan(inv,root/'missing-snapshot',root,root/'out')
            self.assertFalse((root/'out').exists())
        with self.assertRaisesRegex(ValueError,'duplicate JSON'):p.json_object(b'{"active_bank":0,"active_bank":1}')
    def test_adversarial_ota(self):
        base=bank.initial_otadata()
        bad=bytearray(base);bad[28]^=1
        with self.assertRaisesRegex(ValueError,'CRC'):p.ota_selection(bad)
        with self.assertRaisesRegex(ValueError,'ambiguous'):p.ota_selection(base[:4096]*2)
        bad=bytearray(base);bad[100]=0
        with self.assertRaisesRegex(ValueError,'unknown OTA'):p.ota_selection(bad)
        bad=bytearray(base);struct.pack_into('<I',bad,0,0xfffffff0);struct.pack_into('<I',bad,28,zlib.crc32(bad[:4],0xffffffff)&0xffffffff)
        with self.assertRaisesRegex(ValueError,'unsupported'):p.ota_selection(bad)
    def test_plan_and_inventory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);fw=b'RISC_PAIRED_STORE_ABI:1\0RISC_RUNTIME_VERSION:0.1.31\0'+b'x'*100;store=b'z'*p.STORE_BYTES;loader=b'l'*bank.BOOTLOADER_BYTES
            snap=snapshot(root,fw,store,loader,b'p'*3072);data=inventory(fw);inv=root/'inventory.json';inv.write_text(json.dumps(data));before=p.sha(snap.read_bytes())
            with patch.object(p,'esp_image'),patch.object(p,'partitions'),patch.object(p,'BOOTLOADER_SHA256',p.sha(loader)),patch.object(p,'maintenance',return_value=b'maintenance'):
                result=p.plan(inv,snap,root,root/'plan')
                self.assertEqual((root/'plan/restore-application.bin').read_bytes(),b'\xff'*p.APP_BYTES)
                self.assertEqual(p.sha(snap.read_bytes()),before)
                self.assertEqual((root/'plan').stat().st_mode&0o777,0o700)
                for item in result['assets'].values():self.assertTrue(item['offset']>=0xf000 or item['offset']+item['bytes']<=0x9000)
                self.assertEqual(result['hardware_status'],'UNRUN')
                with self.assertRaisesRegex(ValueError,'already exists'):p.plan(inv,snap,root,root/'plan')
                for key,value in [('layout','riscrte-paired-appdata-v2'),('quiescent',False),('active_bank',True),('active_bank',1),('runtime_version','0.1.25'),('running_firmware_sha256','0'*64)]:
                    bad=dict(data);bad[key]=value;inv.write_text(json.dumps(bad))
                    with self.assertRaises(ValueError):p.plan(inv,snap,root,root/'bad')
                    self.assertFalse((root/'bad').exists())
                bad=dict(data);del bad['active_bank'];inv.write_text(json.dumps(bad))
                with self.assertRaisesRegex(ValueError,'inventory fields'):p.plan(inv,snap,root,root/'missing')
                raw=bytearray(snap.read_bytes());raw[p.APPS[1]:p.APPS[1]+len(fw)]=fw;raw[p.STORES[1]:p.STORES[1]+len(store)]=store
                raw[p.JOURNAL+4096:p.JOURNAL+4096+96]=bank.record(1,fw,store)
                page=bytearray(bank.initial_otadata()[:4096]);struct.pack_into('<I',page,0,2);struct.pack_into('<I',page,28,zlib.crc32(page[:4],0xffffffff)&0xffffffff)
                raw[p.OTA+4096:p.OTA+8192]=page;snap.write_bytes(raw);data['active_bank']=1;inv.write_text(json.dumps(data))
                reverse=p.plan(inv,snap,root,root/'reverse');self.assertEqual(reverse['maintenance_bank'],0)
                self.assertEqual(reverse['assets']['enter-otadata-page.bin']['offset'],p.OTA)
                self.assertEqual((root/'reverse/restore-application.bin').read_bytes(),bytes(raw[p.APPS[0]:p.APPS[0]+p.APP_BYTES]))
    def test_no_nvs_read(self):
        with self.assertRaisesRegex(ValueError,'NVS read forbidden'):p.read_range(Path('/not-opened'),0x9000,1)

def real(seed,maintenance):
    with tempfile.TemporaryDirectory() as tmp:
        root=Path(tmp);metadata=json.loads((seed/'seed.json').read_text());fw=(seed/'firmware.bin').read_bytes()
        snap=snapshot(root,fw,(seed/'bootfs0.bin').read_bytes(),(seed/'bootloader.bin').read_bytes(),(seed/'partitions.bin').read_bytes())
        inv=root/'inventory.json';inv.write_text(json.dumps(inventory(fw,metadata['source_sha'],metadata['firmware_version'])))
        a=p.plan(inv,snap,maintenance,root/'one');b=p.plan(inv,snap,maintenance,root/'two');assert a==b
        for f in (root/'one').iterdir():assert f.read_bytes()==(root/'two'/f.name).read_bytes()
        print('Exact-artifact offline entry/restoration plan: deterministic, ABI1 inventory verified, NVS excluded; hardware UNRUN')
if __name__=='__main__':
    if '--seed' in sys.argv:
        parser=argparse.ArgumentParser();parser.add_argument('--seed',type=Path,required=True);parser.add_argument('--maintenance',type=Path,required=True);args=parser.parse_args();real(args.seed,args.maintenance)
    else:unittest.main()
