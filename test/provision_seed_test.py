#!/usr/bin/env python3
"""Host corruption/no-overwrite checks; real mkspiffs roundtrip when supplied."""
import importlib.util,json,os,sys,tempfile,unittest
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
            record={'firmware_version':'0.1.25','layout':'riscrte-paired-16m-v1'}
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
