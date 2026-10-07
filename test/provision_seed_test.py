#!/usr/bin/env python3
"""Host corruption/no-overwrite checks; real mkspiffs roundtrip when supplied."""
import importlib.util,json,os,sys,tempfile,unittest,types
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import provision_seed as p

class SeedTest(unittest.TestCase):
    def test_candidate_hash_refusal(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            record={'target':p.TARGET,'source_sha':'a'*40,'layout':'riscrte-paired-16m-v1','store_abi':1,
                    'assets':{name:{'bytes':4,'sha256':'0'*64} for name in p.PAYLOADS}}
            (folder/'candidate.json').write_text(json.dumps(record))
            for name in p.PAYLOADS:(folder/name).write_bytes(b'fake')
            with self.assertRaisesRegex(ValueError,'candidate asset digest'):p.candidate(folder,'a'*40)
            with self.assertRaisesRegex(ValueError,'candidate identity'):p.candidate(folder,'b'*40)
    def test_compose_offline_contract(self):
        # Model candidate/ELF checks only here; target CI uses actual validators.
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);default=root/'default.elf';default.write_bytes(b'RTE_HEARTBEAT version=1.0.0 dummy')
            record={'firmware_version':'0.1.25','layout':'riscrte-paired-16m-v1','target':p.TARGET,'store_abi':1}
            (root/'candidate.json').write_text(json.dumps(record))
            blobs={name:b'x'*256 for name in p.PAYLOADS};store=b'\xff'*p.STORE_BYTES
            with patch.object(p,'head',return_value='a'*40),patch.object(p.subprocess,'check_output',return_value=b''),patch.object(p,'candidate',return_value=(record,blobs)),patch.object(p,'elf'),patch.object(p,'verify_store',return_value=store),patch.object(p,'build_heartbeat',return_value=default.read_bytes()):
                a=p.compose(root,default,root/'unused-tool',root/'one','a'*40,root/'unused-cc')
                b=p.compose(root,default,root/'unused-tool',root/'two','a'*40,root/'unused-cc')
                self.assertEqual(a,b)
                self.assertNotIn('nvs.bin',a['segments']);self.assertNotIn('app1.bin',a['segments'])
                for item in (root/'one').iterdir():self.assertEqual(item.read_bytes(),(root/'two'/item.name).read_bytes())
                self.assertEqual(p.parse_record((root/'one/bank_state.bin').read_bytes()[:96])[2],0)
                with self.assertRaisesRegex(ValueError,'output already exists'):p.compose(root,default,root/'unused-tool',root/'one','a'*40,root/'unused-cc')
                (root/'link').symlink_to(root/'one',target_is_directory=True)
                with self.assertRaisesRegex(ValueError,'output already exists'):p.compose(root,default,root/'unused-tool',root/'link','a'*40,root/'unused-cc')
                with self.assertRaisesRegex(ValueError,'exact current source'):p.compose(root,default,root/'unused-tool',root/'new','b'*40,root/'unused-cc')
    def test_abi2_seed_journal_offsets_and_interruption(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);default=root/'default.elf';default.write_bytes(b'RTE_HEARTBEAT version=1.0.0 dummy')
            record={'firmware_version':'0.1.31','layout':'riscrte-paired-appdata-v2','target':'esp32s3-16mb-appdata','store_abi':2}
            (root/'candidate.json').write_text(json.dumps(record))
            blobs={name:b'x'*256 for name in p.PAYLOADS};blobs.update({'appdata.bin':b'\xff'*0x80000,'appdata-image.json':b'{}'})
            store=b'\xff'*p.APP_DATA_STORE_BYTES
            with patch.object(p,'head',return_value='a'*40),patch.object(p.subprocess,'check_output',return_value=b''),patch.object(p,'candidate',return_value=(record,blobs)),patch.object(p,'elf'),patch.object(p,'verify_store',return_value=store),patch.object(p,'build_heartbeat',return_value=default.read_bytes()):
                result=p.compose(root,default,root/'unused-tool',root/'out','a'*40,root/'unused-cc')
                self.assertEqual(result['segments']['bootfs0.bin'],0x2f0000)
                self.assertEqual(result['segments']['appdata.bin'],0x270000)
                self.assertEqual(p.parse_record((root/'out/bank_state.bin').read_bytes()[:96],True)[5],2)
                self.assertRaises(ValueError,p.parse_record,(root/'out/bank_state.bin').read_bytes()[:96])
                original=Path.write_bytes
                def stop(path,data):
                    if path.name=='bootfs0.bin':raise KeyboardInterrupt()
                    return original(path,data)
                with patch.object(Path,'write_bytes',stop):
                    self.assertRaises(KeyboardInterrupt,p.compose,root,default,root/'unused-tool',root/'interrupted','a'*40,root/'unused-cc')
                self.assertFalse((root/'interrupted').exists())
            for bad in ({**record,'store_abi':1},{**record,'target':p.TARGET},{**record,'store_abi':True}):
                self.assertRaises(ValueError,p.layout,bad)
    def test_iq_candidate_requires_fresh_linked_reservation_proof(self):
        import app_data_image
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source='a'*40
            linked={'reservation_table':[(1,2)],'bank_bytes':65536}
            encoded=json.loads(json.dumps(linked))
            record={'target':'esp32s3-16mb-appdata-iq','source_sha':source,'layout':'riscrte-paired-appdata-v2','store_abi':2,
                    'firmware_version':'0.1.35','initial_appdata':{'fixture':True},'native_proof':{'fixture':True,'radio_iq':encoded}}
            identity=(b'fixture-target'+b'\0esp32s3-16mb-appdata-iq\0RTE_SOURCE='+source.encode()+b'\0RISC_RUNTIME_VERSION:0.1.35\0RISC_PAIRED_STORE_ABI:2\0')
            binary=bytearray(identity);binary[3]=0x40
            blobs={name:bytes(binary) for name in p.PAYLOADS}
            blobs.update({'appdata.bin':b'fixture','appdata-image.json':b'{}','radio-iq-proof.json':json.dumps(encoded).encode()})
            for name,data in blobs.items():(root/name).write_bytes(data)
            record['assets']={name:{'bytes':len(data),'sha256':p.sha(data)} for name,data in blobs.items()}
            (root/'candidate.json').write_text(json.dumps(record))
            fake=types.ModuleType('radio_iq_proof')
            from unittest.mock import Mock
            fake.prove=Mock(return_value=linked)
            with patch.dict(sys.modules,{'radio_iq_proof':fake}),patch.object(p,'partitions'),patch.object(p,'esp_image'),patch.object(p,'elf'),patch.object(p,'native_proof',return_value={'fixture':True}),patch.object(p,'BOOTLOADER_BYTES',len(binary)),patch.object(p,'BOOTLOADER_SHA256',p.sha(binary)),patch.object(app_data_image,'verify_initial',return_value={'fixture':True}):
                loaded,_=p.candidate(root,source)
                self.assertEqual(loaded['target'],'esp32s3-16mb-appdata-iq')
                fake.prove.assert_called_once_with(blobs['firmware.elf'])
                fake.prove.return_value={'reservation_table':[(2,3)],'bank_bytes':65536}
                self.assertRaisesRegex(ValueError,'radio IQ linked reservation proof',p.candidate,root,source)
                fake.prove.side_effect=ValueError('unreserved IQ bank')
                self.assertRaisesRegex(ValueError,'unreserved IQ bank',p.candidate,root,source)
    @unittest.skipUnless(os.environ.get('MKSPIFFS'),'real SPIFFS tool runs in target CI')
    def test_real_store(self):
        members={'boot.json':b'{"board":"board.json","default_app":"default.elf","drivers":[]}',
                 'board.json':b'{"dummy":"test-only"}','default.elf':b'dummy-ELF-not-executed'}
        with tempfile.TemporaryDirectory() as tmp:
            work=Path(tmp);source=work/'source';source.mkdir()
            for name,data in members.items():(source/name).write_bytes(data)
            image=work/'image.bin'
            p.subprocess.run([os.environ['MKSPIFFS'],'-c',str(source),'-b','4096','-p','256','-s',str(p.STORE_BYTES),str(image)],check=True,capture_output=True)
            data=p.verify_store(Path(os.environ['MKSPIFFS']),image.read_bytes(),members,work)
            self.assertEqual(len(data),p.STORE_BYTES)
            members['default.elf']=b'changed'
            other=work/'other';other.mkdir()
            with self.assertRaisesRegex(ValueError,'seed readback mismatch'):p.verify_store(Path(os.environ['MKSPIFFS']),data,members,other)

if __name__=='__main__':unittest.main()
