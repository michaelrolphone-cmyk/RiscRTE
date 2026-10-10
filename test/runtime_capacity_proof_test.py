#!/usr/bin/env python3
"""Linked object/load coverage and fail-closed candidate proof regressions."""
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from runtime_capacity_proof import LIMITS, PREFIX, SYMBOL, prove, verify_candidate

class CapacityProof(unittest.TestCase):
    def fixture(self, providers, declaration='const char', symbol=SYMBOL, section='.flash.rodata', extra=''):
        marker = PREFIX + ':'.join(map(str, LIMITS[providers])).encode() + b'\0'
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/'input.c').write_text(declaration+' '+symbol+'[] __attribute__((section('+json.dumps(section)+')))='+json.dumps(marker[:-1].decode())+';\n'+extra+'\nvoid _start(void){}\n')
            subprocess.run(['cc','-m32','-nostdlib','-no-pie','-Wl,--build-id=none','-Wl,--section-start=.flash.rodata=0x3c000000',str(root/'input.c'),'-o',str(root/'image')],check=True,capture_output=True)
            image=bytearray((root/'image').read_bytes())
        # The parser is architecture-specific; fixture machine only is replaced.
        # No synthetic or real target instruction executes in these tests.
        struct.pack_into('<H', image, 18, 94)
        return {'firmware.elf':bytes(image), 'firmware.bin':b'fixture'+marker}

    def test_exact_options_and_mismatches(self):
        for providers in LIMITS:
            blobs=self.fixture(providers);proof=prove(blobs,providers)
            self.assertEqual((proof['apps'],proof['providers'],proof['graph_grants']),LIMITS[providers])
            self.assertEqual(proof['shared_nonboot_grants'],15 if providers==17 else 16)
            for other in LIMITS:
                if other!=providers:
                    with self.assertRaises(ValueError):prove(blobs,other)
            for name in blobs:
                for bad in (b'',blobs[name].replace(PREFIX,b'X'*len(PREFIX),1)):
                    with self.assertRaises(ValueError):prove({**blobs,name:bad},providers)
            with self.assertRaises(ValueError):prove({**blobs,'firmware.bin':blobs['firmware.bin']+PREFIX+b'bad\0'},providers)
        for unsupported in (None,True,0,25,27,28,30,'29'):
            with self.assertRaises(ValueError):prove({},unsupported)

    def test_requires_exact_linked_flash_object(self):
        for blobs in (self.fixture(29,'const char','unrelated'),self.fixture(29,'char',section='.data'),self.fixture(29,extra='const char extra[] = "RISC_RUNTIME_CAPACITY:bad";')):
            with self.assertRaises(ValueError):prove(blobs,29)
        original=self.fixture(29)
        # Non-loaded debug/trailing text cannot claim capacity or conflict with it.
        prove({**original,'firmware.elf':original['firmware.elf']+PREFIX+b'debug-copy\0'},29)
        # A file with a marker but no final loadable mapping is insufficient.
        image=bytearray(original['firmware.elf'])
        struct.pack_into('<H',image,44,0) # ELF32 e_phnum
        with self.assertRaises(ValueError):prove({**original,'firmware.elf':bytes(image)},29)
        image=bytearray(original['firmware.elf']);struct.pack_into('<H',image,18,3)
        with self.assertRaises(ValueError):prove({**original,'firmware.elf':bytes(image)},29)

    def test_candidate_recomputes_proof_and_preserves_only_old_inputs(self):
        blobs=self.fixture(29);proof=prove(blobs,29)
        record={'firmware_version':'0.2.4','native_proof':{'runtime_capacity':proof}}
        self.assertEqual(verify_candidate(blobs,record),proof)
        for key in proof:
            changed=copy.deepcopy(record);changed['native_proof']['runtime_capacity'][key]='forged'
            with self.assertRaises(ValueError):verify_candidate(blobs,changed)
        changed=copy.deepcopy(record);changed['native_proof']['runtime_capacity']['live_grants_per_invocation']=True
        with self.assertRaises(ValueError):verify_candidate(blobs,changed)
        for version in ('0.2.4','0.2.5','1.0.0','bad'):
            with self.assertRaises(ValueError):verify_candidate({}, {'firmware_version':version,'native_proof':{}})
        self.assertIsNone(verify_candidate({}, {'firmware_version':'0.2.3','native_proof':{}}))
        with self.assertRaises(ValueError):verify_candidate(blobs,{'firmware_version':'0.2.3','native_proof':{}})
        legacy=self.fixture(17)
        with self.assertRaises(ValueError):verify_candidate(legacy,{'firmware_version':'0.2.4','native_proof':{'runtime_capacity':prove(legacy,17)}})

if __name__=='__main__':unittest.main()
