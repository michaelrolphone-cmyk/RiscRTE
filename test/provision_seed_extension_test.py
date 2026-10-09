#!/usr/bin/env python3
"""Trusted product validators cannot replace the generic native custody gates."""
import copy
from contextlib import ExitStack
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import provision_seed as p
import provision_device as d
import provision_profile as profile_tools


class Extensions(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.input=self.root/'candidate';self.input.mkdir();self.source='a'*40
        binary=bytearray(b'fixture\0esp32s3-16mb-paired\0RTE_SOURCE='+self.source.encode()+
                         b'\0RISC_RUNTIME_VERSION:0.1.72\0RISC_PAIRED_STORE_ABI:1\0');binary[3]=0x40
        self.core={name:bytes(binary) for name in p.PAYLOADS}
        self.extra={'composition.json':b'{"source":"fixture"}', 'platform-proof.json':b'{"startup":true}',
                    'options-proof.json':b'{"stage_logs":true}'}
        self.record={'schema':1,'source_sha':self.source,'target':p.TARGET,'layout':'riscrte-paired-16m-v1','store_abi':1,
                     'firmware_version':'0.1.72','native_proof':{'core':1,'stage_logs':{'enabled':True}},
                     'build_environment':'fixture-stage','build_options':{'cache':False},'stage_logs':True}
        self.record['assets']={name:{'bytes':len(raw),'sha256':p.sha(raw)} for name,raw in {**self.core,**self.extra}.items()}
        for name,raw in {**self.core,**self.extra}.items():(self.input/name).write_bytes(raw)
        self.save()
        self.stack=ExitStack();self.addCleanup(self.stack.close)
        for name in ('partitions','esp_image','elf'):
            self.stack.enter_context(patch.object(p,name))
        self.stack.enter_context(patch.object(p,'native_proof',return_value={'core':1}))
        self.stack.enter_context(patch.object(p,'BOOTLOADER_BYTES',len(binary)))
        self.stack.enter_context(patch.object(p,'BOOTLOADER_SHA256',p.sha(binary)))
        self.calls=0

    def save(self):
        (self.input/'candidate.json').write_text(json.dumps(self.record))

    def extension(self,candidate,blobs):
        self.calls+=1
        self.assertEqual(blobs,{**self.core,**self.extra})
        self.assertEqual(json.loads(blobs['platform-proof.json']),{'startup':True})
        return {'id':'test.native-composition-v1','native_proof':{'stage_logs':{'enabled':True}},
                'assets':self.extra.copy(),'metadata':{key:candidate[key] for key in ('build_environment','build_options','stage_logs')}}

    def candidate(self,callback=None):
        return p.candidate(self.input,self.source,callback)

    def test_exact_assets_proof_metadata_and_explicit_callback(self):
        self.assertRaisesRegex(ValueError,'explicit candidate extension',self.candidate)
        record,blobs=self.candidate(self.extension)
        self.assertEqual(blobs,{**self.core,**self.extra})
        self.assertEqual(record['_seed_extension']['assets'],{name:self.record['assets'][name] for name in self.extra})
        self.assertEqual(record['_seed_extension']['id'],'test.native-composition-v1')
        self.assertNotIn('_seed_extension',json.loads((self.input/'candidate.json').read_text()))
        self.assertEqual(self.calls,1)

    def test_no_core_override_or_type_coercion(self):
        original=self.extension(self.record,{**self.core,**self.extra})
        variants=[]
        bad=copy.deepcopy(original);bad['native_proof']['core']=1;variants.append(bad)
        bad=copy.deepcopy(original);bad['native_proof']['stage_logs']['enabled']=1;variants.append(bad)
        bad=copy.deepcopy(original);bad['assets']['firmware.bin']=b'override';variants.append(bad)
        bad=copy.deepcopy(original);bad['assets']['composition.json']=b'changed';variants.append(bad)
        bad=copy.deepcopy(original);bad['metadata']['source_sha']=self.source;variants.append(bad)
        bad=copy.deepcopy(original);bad['metadata']['stage_logs']=1;variants.append(bad)
        bad=copy.deepcopy(original);bad['id']='../validator.py';variants.append(bad)
        bad=copy.deepcopy(original);bad['unexpected']=True;variants.append(bad)
        for value in variants:
            self.assertRaises(ValueError,self.candidate,lambda candidate,blobs:value)

    def test_sidecar_hash_paths_size_and_total_bounds(self):
        self.record['assets']['composition.json']['sha256']='0'*64;self.save()
        self.assertRaisesRegex(ValueError,'asset digest',self.candidate,self.extension)
        self.record['assets']['composition.json']['sha256']=p.sha(self.extra['composition.json'])
        self.record['assets']['../escape.json']={'bytes':2,'sha256':p.sha(b'{}')};self.save()
        self.assertRaisesRegex(ValueError,'unsafe candidate',self.candidate,self.extension)
        del self.record['assets']['../escape.json']
        self.record['assets']['profile.json']={'bytes':2,'sha256':p.sha(b'{}')};self.save()
        self.assertRaisesRegex(ValueError,'unsafe candidate',self.candidate,self.extension)
        del self.record['assets']['profile.json']
        for name,size in [('composition.json',p.EXTENSION_FILE_BYTES+1),('composition.json',p.EXTENSION_FILE_BYTES)]:
            raw=b'"'+b'x'*(size-2)+b'"';(self.input/name).write_bytes(raw)
            self.record['assets'][name]={'bytes':size,'sha256':p.sha(raw)};self.save()
            if size>p.EXTENSION_FILE_BYTES:self.assertRaisesRegex(ValueError,'asset bounds',self.candidate,self.extension)
        for name in ('platform-proof.json','options-proof.json'):
            raw=b'"'+b'x'*(p.EXTENSION_FILE_BYTES-2)+b'"';(self.input/name).write_bytes(raw)
            self.record['assets'][name]={'bytes':len(raw),'sha256':p.sha(raw)}
        self.save();self.assertRaisesRegex(ValueError,'total bounds',self.candidate,self.extension)

    def test_callback_cannot_mutate_the_generic_candidate(self):
        def bad(candidate,blobs):
            answer=self.extension(candidate,blobs)
            candidate['firmware_version']='9.9.9';blobs['firmware.bin']=b'changed'
            return answer
        record,blobs=self.candidate(bad)
        self.assertEqual(record['firmware_version'],'0.1.72')
        self.assertEqual(blobs['firmware.bin'],self.core['firmware.bin'])

    def test_seed_receipt_and_private_revalidation(self):
        store=self.root/'store.bin';store.write_bytes(b'\xff'*p.STORE_BYTES)
        heartbeat=b'RTE_HEARTBEAT version=1.0.0 fixture'
        with patch.object(p,'head',return_value=self.source),patch.object(p.subprocess,'check_output',return_value=b''),\
             patch.object(p,'verify_store',return_value=store.read_bytes()),patch.object(p,'build_heartbeat',return_value=heartbeat):
            manifest=p.compose(self.input,store,self.root/'unused',self.root/'seed',self.source,self.root/'unused',self.extension)
        self.assertEqual(manifest['extension']['id'],'test.native-composition-v1')
        for name,raw in self.extra.items():self.assertEqual((self.root/'seed'/name).read_bytes(),raw)
        work=self.root/'verify';work.mkdir()
        self.assertRaisesRegex(ValueError,'explicit candidate extension',d.verify_seed,self.root/'seed',work)
        result,blobs=d.verify_seed(self.root/'seed',work,self.extension)
        self.assertEqual(result,manifest);self.assertTrue(set(self.extra)<=set(blobs))
        # Private composition reruns the callback and retains proof artifacts,
        # without treating sidecar JSON as executable flash segments.
        store_dir=self.root/'product';store_dir.mkdir()
        for name in p.SEED_FILES:(store_dir/name).write_bytes(b'product-fixture-only')
        inventory=self.root/'inventory.json'
        inventory.write_bytes(profile_tools.encode(profile_tools.pin_store(store_dir,'riscrte-paired-16m-v1',
                              'https://example.test/fixture/',target=p.TARGET)))
        wifi=self.root/'wifi.json';wifi.write_text('{"ssid":"test-only","password":""}')
        validator=self.root/'validator'
        subprocess.run(['bash',str(p.ROOT/'scripts/build_provision_input_tool.sh'),str(validator)],check=True,capture_output=True)
        profile_tools.build_profile(inventory,store_dir,wifi,validator,'time.example.invalid',self.root/'owner','https://example.test/fixture/')
        before=self.calls
        with patch.object(d,'make_nvs',return_value=b'\xff'*d.NVS_BYTES):
            installed=d.compose(self.root/'seed',self.root/'owner',validator,self.root/'unused-generator',
                                self.root/'private',True,self.extension)
        self.assertEqual(self.calls,before+1)
        self.assertEqual(installed['extension'],manifest['extension'])
        self.assertFalse(set(self.extra)&set(installed['segments']))
        for name,raw in self.extra.items():self.assertEqual((self.root/'private'/name).read_bytes(),raw)
        self.assertEqual((self.root/'private/candidate.json').read_bytes(),(self.input/'candidate.json').read_bytes())
        # A hash-consistent edited seed receipt still cannot rename its validator.
        manifest['extension']['id']='different.validator'
        seed=self.root/'seed';(seed/'seed.json').write_text(json.dumps(manifest))
        (seed/'SHA256SUMS').write_text(''.join(f'{p.sha(path.read_bytes())}  {path.name}\n' for path in sorted(seed.iterdir()) if path.name!='SHA256SUMS'))
        other=self.root/'verify-again';other.mkdir()
        self.assertRaisesRegex(ValueError,'extension revalidation mismatch',d.verify_seed,seed,other,self.extension)

    def test_pretty_seed_manifest_limit_precedes_output(self):
        # Small compact receipt, large indented manifest: a producer must not
        # create a seed that its own bounded consumer then refuses to read.
        self.record['build_options']['padding']=['x']*10000;self.save()
        receipt=self.extension(self.record,{**self.core,**self.extra})
        self.assertLess(len(json.dumps(receipt['metadata'],separators=(',',':'))),128*1024)
        store=self.root/'store.bin';store.write_bytes(b'\xff'*p.STORE_BYTES)
        with patch.object(p,'head',return_value=self.source),patch.object(p.subprocess,'check_output',return_value=b''),\
             patch.object(p,'verify_store',return_value=store.read_bytes()),patch.object(p,'build_heartbeat',return_value=b'RTE_HEARTBEAT version=1.0.0 fixture'):
            self.assertRaisesRegex(ValueError,'seed manifest size bound',p.compose,self.input,store,self.root/'unused',
                                   self.root/'oversized',self.source,self.root/'unused',self.extension)
        self.assertFalse((self.root/'oversized').exists())


if __name__=='__main__':unittest.main()
