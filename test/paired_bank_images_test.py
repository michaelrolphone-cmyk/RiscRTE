#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import struct
import unittest
import tempfile
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('paired_bank_images',Path(__file__).resolve().parents[1]/'scripts/paired_bank_images.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)

def crc(data, initial=0):
    value=initial ^ 0xffffffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value=(value>>1) ^ (0xedb88320 if value&1 else 0)
    return value ^ 0xffffffff

class MetadataTest(unittest.TestCase):
    def test_otadata(self):
        image=p.initial_otadata()
        self.assertEqual(len(image),8192)
        seq,label,state,digest=struct.unpack('<I20sII',image[:32])
        self.assertEqual((seq,label,state,digest),(1,b'\xff'*20,2,0x4743989a))
        self.assertEqual(digest,crc(image[:4],0xffffffff))
        self.assertEqual(image[32:],b'\xff'*(8192-32))
    def test_journal(self):
        image=p.initial_bank_state(b'x'*256,b'y'*p.STORE_BYTES)
        self.assertEqual(len(image),8192)
        r=p.parse_record(image[:96]);self.assertEqual(r[2:6],(0,256,p.STORE_BYTES,1))
        self.assertEqual(r[-1],crc(image[:92]))
        self.assertEqual(image[96:],b'\xff'*(8192-96))
        for at in (0,8,12,16,24,55,91,95):
            changed=bytearray(image[:96]);changed[at]^=1
            with self.assertRaises(ValueError):p.parse_record(changed)
    def test_bounds(self):
        for bank,fw,store in ((2,b'x'*256,b'y'*p.STORE_BYTES),(0,b'x'*31,b'y'*p.STORE_BYTES),(0,b'x'*256,b'y'*64)):
            with self.assertRaises(ValueError):p.record(bank,fw,store)
    def test_installer_rejects_unknown_bootloader_before_commit(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);loader=root/'bootloader.bin';loader.write_bytes(b'\xff'*p.BOOTLOADER_BYTES)
            output=root/'metadata'
            with patch('sys.argv',['paired_bank_images','--firmware',str(root/'firmware.bin'),
                                   '--store',str(root/'store.bin'),'--bootloader',str(loader),'--output',str(output)]):
                with self.assertRaisesRegex(ValueError,'unverified rollback bootloader'):p.main()
            self.assertFalse(output.exists())

if __name__=='__main__':unittest.main()
