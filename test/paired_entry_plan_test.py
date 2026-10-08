#!/usr/bin/env python3
"""Offline transaction geometry/refusal tests; product admission is a test stub.

Real product adapters must separately run exact native/graph/namespace admission.
The record/receipt witness below compiles the production C++ implementation.
"""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import paired_entry_plan as p
import paired_bank_images as bank
from maintenance_plan_test import partition_image,snapshot,synthetic_firmware


def admission(old_fw,old_store,new_fw,new_store):
    return {'schema':'test.synthetic-admission','schema_version':1,'layout':p.LAYOUT,
            **{name+'_sha256':p.sha(raw) for name,raw in
               [('source_firmware',old_fw),('source_store',old_store),('target_firmware',new_fw),('target_store',new_store)]},
            'admissions':{name:{'scope':'Synthetic transaction fixture, no real product/ELF admission'}
                          for name in ('source_self','receiving_transition','target_self')}}


class Plans(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.fw=synthetic_firmware(True);self.store=b's'*bank.APP_DATA_STORE_BYTES
        self.loader=b'L'*bank.BOOTLOADER_BYTES
        self.original=snapshot(self.root,self.fw,self.store,self.loader,partition_image(True),True)
        self.firmware=self.root/'firmware.bin';self.firmware.write_bytes(self.fw+b'new-version-fixture')
        self.nextstore=self.root/'store.bin';self.nextstore.write_bytes(b't'*bank.APP_DATA_STORE_BYTES)
        self.inventory=self.root/'inventory.json';self.write_inventory(0)
        self.output=self.root/'plan';self.current=self.root/'current.bin'
        self.addCleanup(patch.stopall)
        patch.object(p,'esp_image').start();patch.object(p.banks,'BOOTLOADER_SHA256',p.sha(self.loader)).start()
        patch.object(p,'read_image',return_value={}).start()

    def write_inventory(self,active):
        self.inventory.write_bytes(p.encoded({'schema':'riscrte.offline-paired-inventory','schema_version':1,
            'layout':p.LAYOUT,'flash_bytes':p.FLASH,'active_bank':active,'running_firmware_sha256':p.sha(self.fw),'quiescent':True}))

    def reverse(self):
        raw=bytearray(self.original.read_bytes());raw[0x800000:0x800000+len(self.fw)]=self.fw
        raw[0xae0000:0xff0000]=self.store
        raw[0xff3000:0xff3000+96]=bank.record(1,self.fw,self.store,True)
        page=bytearray(bank.initial_otadata()[:4096]);struct.pack_into('<I',page,0,2)
        struct.pack_into('<I',page,28,zlib.crc32(page[:4],0xffffffff)&0xffffffff)
        raw[0xff1000:0xff2000]=page;self.original.write_bytes(raw);self.write_inventory(1)

    def plan(self,callback=admission):
        return p.plan(self.inventory,self.original,self.firmware,self.nextstore,self.output,callback)

    def check(self,phase,callback=admission):
        return p.verify_phase(self.inventory,self.original,self.firmware,self.nextstore,self.output,self.current,phase,callback)

    def apply(self,report,name):
        raw=bytearray(self.current.read_bytes());item=report['assets'][name]
        raw[item['offset']:item['offset']+item['bytes']]=(self.output/name).read_bytes();self.current.write_bytes(raw)

    def progression(self,active):
        if active:self.reverse()
        report=self.plan();self.current.write_bytes(self.original.read_bytes());self.check('ready')
        self.assertEqual(report['target_bank'],1-active)
        self.assertEqual(report['write_order'],['invalidate-journal.bin','enter-application.bin','enter-store.bin','enter-journal.bin','enter-otadata.bin'])
        steps=zip(report['write_order'],p.PHASES[1:6])
        for name,phase in steps:
            self.apply(report,name);self.check(phase)
        offset=report['assets']['enter-otadata.bin']['offset']
        for state,phase in ((1,'pending'),(2,'confirmed'),(4,'rolled-back')):
            raw=bytearray(self.current.read_bytes());struct.pack_into('<I',raw,offset+24,state);self.current.write_bytes(raw)
            self.check(phase)
        self.assertEqual(self.current.read_bytes()[0x9000:0xf000],self.original.read_bytes()[0x9000:0xf000])
        self.assertEqual(self.current.read_bytes()[0x270000:0x2f0000],self.original.read_bytes()[0x270000:0x2f0000])
        self.assertEqual(self.output.stat().st_mode&0o777,0o700)
        with self.assertRaisesRegex(ValueError,'output already exists'):self.plan()

    def test_forward_phases_and_persistence(self):self.progression(0)
    def test_reverse_phases_and_persistence(self):self.progression(1)

    def test_no_callback_failed_callback_and_false_receipt(self):
        with self.assertRaisesRegex(ValueError,'callback'):self.plan(None)
        def refused(*args):raise ValueError('Product migration refused')
        with self.assertRaisesRegex(ValueError,'migration'):self.plan(refused)
        def wrong(*args):
            result=admission(*args);result['target_firmware_sha256']='0'*64;return result
        with self.assertRaisesRegex(ValueError,'byte identity'):self.plan(wrong)
        def incomplete(*args):
            result=admission(*args);del result['admissions']['receiving_transition'];return result
        with self.assertRaisesRegex(ValueError,'complete product'):self.plan(incomplete)
        self.assertFalse(self.output.exists())

    def test_torn_out_of_order_and_changed_preserved_regions(self):
        report=self.plan();self.current.write_bytes(self.original.read_bytes())
        self.apply(report,'enter-otadata.bin')
        with self.assertRaises(ValueError):self.check('pending')
        self.current.write_bytes(self.original.read_bytes())
        self.apply(report,'enter-application.bin')
        # Readiness must be explicitly invalidated, including an originally
        # populated destination. Reverse-bank fixture covers that condition.
        self.apply(report,'invalidate-journal.bin');self.check('firmware-staged')
        for name in ('enter-store.bin','enter-journal.bin','enter-otadata.bin'):self.apply(report,name)
        good=self.current.read_bytes()
        for offset in (0,0x8000,0x9000,0x10000,0x270000,0x2effff,0x2f0000,0x800001,0xae0001,0xff0000,0xff3001,0xfffffe):
            raw=bytearray(good);raw[offset]^=1;self.current.write_bytes(raw)
            with self.subTest(offset=hex(offset)),self.assertRaises(ValueError):self.check('pending')
        self.current.write_bytes(good);offset=report['assets']['enter-otadata.bin']['offset']
        for state in (2,3,4,0xffffffff):
            raw=bytearray(good);struct.pack_into('<I',raw,offset+24,state);self.current.write_bytes(raw)
            with self.assertRaisesRegex(ValueError,'state'):self.check('pending')

    def test_consumed_receipt_precedes_legacy_file_and_follows_pair(self):
        self.reverse();raw=bytearray(self.original.read_bytes());digest=b'P'*32
        prior_fw=self.fw+b'older-source';prior_store=b'v'*bank.APP_DATA_STORE_BYTES
        raw[0x10000:0x10000+len(prior_fw)]=prior_fw;raw[0x2f0000:0x800000]=prior_store
        raw[0xff2000:0xff2060]=bank.record(0,prior_fw,prior_store,True)
        source=bank.parse_record(raw[0xff2000:0xff2060],True)
        raw[0xff3000:0xff4000]=p.target_journal(1,self.fw,self.store,source,digest)
        self.original.write_bytes(raw)
        with patch.object(p,'read_image',side_effect=AssertionError('Active receipt must take priority')):
            report=self.plan()
        self.assertTrue(report['consumed_profile_inherited']);self.assertEqual(report['consumed_profile_sha256'],digest.hex())
        data=(self.output/'enter-journal.bin').read_bytes();self.assertEqual(data[108:140],digest)
        target=bank.parse_record(data[:96],True);attempt=struct.unpack('<3I32s32s32s32s32sI',data[96:272])
        self.assertEqual(attempt[4:6],target[6:8])
        active=bank.parse_record(raw[0xff3000:0xff3060],True)
        self.assertEqual(attempt[6:8],active[6:8]);self.assertNotEqual(attempt[6:8],source[6:8])
        self.assertEqual(attempt[8],zlib.crc32(data[96:268])&0xffffffff)

    def test_populated_destination_requires_invalidation_before_images(self):
        self.reverse();report=self.plan();self.current.write_bytes(self.original.read_bytes())
        self.apply(report,'enter-application.bin')
        with self.assertRaisesRegex(ValueError,'journal'):self.check('firmware-staged')
        self.apply(report,'invalidate-journal.bin');self.check('firmware-staged')

    def test_legacy_receipt_and_invalid_legacy_length(self):
        with patch.object(p,'read_image',return_value={'.provision-sha256':b'P'*31}):
            with self.assertRaisesRegex(ValueError,'legacy consumed'):self.plan()
        with patch.object(p,'read_image',return_value={'.provision-sha256':b'P'*32}):
            self.assertTrue(self.plan()['consumed_profile_inherited'])

    def test_private_plan_mutation_and_pending_source_refuse(self):
        report=self.plan();self.current.write_bytes(self.original.read_bytes())
        path=self.output/'restore-store.bin';raw=bytearray(path.read_bytes());raw[0]^=1;path.write_bytes(raw)
        with self.assertRaisesRegex(ValueError,'payload changed'):self.check('ready')
        raw=bytearray(self.original.read_bytes());struct.pack_into('<I',raw,0xff0000+24,1);self.original.write_bytes(raw)
        with self.assertRaisesRegex(ValueError,'unconfirmed'):p.prepare(self.inventory,self.original,self.firmware,self.nextstore,admission)

    def test_terminal_source_sequence_cannot_emit_unusable_confirmed_state(self):
        self.reverse();raw=bytearray(self.original.read_bytes());offset=0xff1000
        struct.pack_into('<I',raw,offset,0xffffffee)
        struct.pack_into('<I',raw,offset+28,zlib.crc32(raw[offset:offset+4],0xffffffff)&0xffffffff)
        self.original.write_bytes(raw)
        self.assertEqual(p.maintenance.ota_selection(raw[0xff0000:0xff2000])[0],1)
        with self.assertRaisesRegex(ValueError,'next selector sequence'):self.plan()
        self.assertFalse(self.output.exists())

    def test_production_record_and_receipt_parity(self):
        source=bank.record(0,self.fw,self.store,True)
        new_fw=self.firmware.read_bytes();new_store=self.nextstore.read_bytes();target=bank.record(1,new_fw,new_store,True);digest=b'P'*32
        code=self.root/'record.cpp';code.write_text('#include "runtime/update/PairedBank.h"\n#include <cstdio>\n#include <cassert>\nint main(){using namespace RiscUpdate;Record s{},t{};uint8_t p[32];assert(fread(&s,1,96,stdin)==96);assert(fread(&t,1,96,stdin)==96);assert(fread(p,1,32,stdin)==32);auto r=makeRecord(t.bank,t.firmwareSize,t.firmwareSha,t.storeSha);auto a=makeAttempt(t.bank,p,r,s);assert(validRecord(r,t.bank)&&validAttempt(a,r,t.bank)&&sameAttemptSource(a,s));fwrite(&r,1,96,stdout);fwrite(&a,1,176,stdout);}\n')
        executable=self.root/'record'
        subprocess.run(['c++','-std=c++17','-DRISC_PAIRED_APP_DATA=1','-I'+str(ROOT/'src'),'-I'+str(ROOT/'sdk/driver'),str(code),str(ROOT/'src/runtime/update/PairedBank.cpp'),'-o',str(executable)],check=True)
        actual=subprocess.check_output([str(executable)],input=source+target+digest)
        self.assertEqual(actual,p.target_journal(1,new_fw,new_store,bank.parse_record(source,True),digest)[:272])


if __name__=='__main__':unittest.main()
