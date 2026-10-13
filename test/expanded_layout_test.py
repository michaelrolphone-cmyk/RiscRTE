import ast
import csv
import os
import re
import hashlib
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
import paired_bank_images as images
import paired_candidate as candidate

class ExpandedLayout(unittest.TestCase):
    def test_exact_csv_and_bounds(self):
        kinds={'data':1,'app':0};types={'nvs':2,'ota_0':0x10,'ota_1':0x11,'spiffs':0x82,'ota':0}
        found={}
        for row in csv.reader(line for line in (ROOT/'partitions-paired-appdata-expanded.csv').read_text().splitlines() if line and not line.startswith('#')):
            name,kind,subtype,offset,size=row
            found[name]=(kinds[kind],types.get(subtype) if subtype in types else int(subtype,0),int(offset,0),int(size,0))
        self.assertEqual(found,candidate.EXPANDED_EXPECTED)
        spans=sorted((value[2],value[2]+value[3]) for value in found.values())
        self.assertTrue(all(a[1]<=b[0] for a,b in zip(spans,spans[1:])))
        self.assertEqual(spans[-1][1],0xff4000)
        self.assertEqual(found['appdata'],candidate.APP_DATA_EXPECTED['appdata'])
        for key in ('nvs','otadata','bank_state','app0'):
            self.assertEqual(found[key],candidate.APP_DATA_EXPECTED[key])
        self.assertEqual(found['bootfs0'][3]+found['bootfs1'][3]-2*images.APP_DATA_STORE_BYTES,0x80000)
        rows=b''.join(struct.pack('<HBBII16sI',0x50aa,*value,name.encode(),0) for name,value in found.items())
        table=rows+b'\xeb\xeb'+b'\xff'*14+hashlib.md5(rows).digest();table+=b'\xff'*(3072-len(table))
        self.assertEqual(candidate.partitions(table,found),found)
        for other in (candidate.EXPECTED,candidate.APP_DATA_EXPECTED):
            with self.assertRaises(ValueError):candidate.partitions(table,other)
        for at in (0,15,len(rows)+4,len(rows)+31,len(table)-1):
            damaged=bytearray(table);damaged[at]^=1
            with self.assertRaises(ValueError):candidate.partitions(damaged,found)
    def test_journal_cross_layout_and_payload_boundaries(self):
        for appdata,expanded,size,abi in ((False,False,images.STORE_BYTES,1),(True,False,images.APP_DATA_STORE_BYTES,2),(True,True,images.EXPANDED_STORE_BYTES,3)):
            store=b'\x99'*size
            for bank in (0,1):
                record=images.record(bank,b'f'*64,store,appdata,expanded)
                self.assertEqual(images.parse_record(record,appdata,expanded)[2:6],(bank,64,size,abi))
                for a,e in ((False,False),(True,False),(True,True)):
                    if (a,e)!=(appdata,expanded):
                        with self.assertRaises(ValueError):images.parse_record(record,a,e)
            for length in (size-1,size+1):
                with self.assertRaises(ValueError):images.record(0,b'f'*64,b's'*length,appdata,expanded)
        with self.assertRaises(ValueError):images.record(0,b'f'*64,b's'*images.EXPANDED_STORE_BYTES,False,True)
        for length in (31,0x260001):
            with self.assertRaises(ValueError):images.record(0,b'f'*length,b's'*images.EXPANDED_STORE_BYTES,True,True)
        state=images.initial_bank_state(b'f'*64,b's'*images.EXPANDED_STORE_BYTES,True,True)
        self.assertEqual(len(state),8192);self.assertEqual(state[96:],b'\xff'*(8192-96))
    def test_profile_is_explicit_and_ordinary(self):
        text=(ROOT/'watch-expanded-profile.ini').read_text()
        for value in ('extends = env:watch-file-source','partitions-paired-appdata-expanded.csv','-DRISC_PAIRED_APP_DATA_EXPANDED=1','esp32s3-16mb-appdata-expanded-iq'):
            self.assertIn(value,text)
        unflags=text.split('build_unflags =')[1].split('build_flags =')[0]
        self.assertIn('-DRISC_OWNER_INSTALLER=1',unflags)
        self.assertIn('-Wl,-u,risc_owner_maintenance_target',unflags)
        self.assertIn('-DRISC_PROVIDER_CAPACITY=32',unflags)
        self.assertIn('-DRISC_PROVIDER_CAPACITY=40',text)
        for key in ('RISC_APP_POLICY_ROWS','RISC_APP_REQUIREMENT_ROWS'):
            self.assertIn('-D'+key+'=24',unflags)
            self.assertIn('-D'+key+'=32',text.split('build_flags =')[1])
        self.assertIn('-std=gnu++11',unflags)
        self.assertNotIn('\n  -U',text)
        self.assertNotIn('RISC_INSTALLED_APP_CAPACITY',text)
        inherited=(ROOT/'platformio.ini').read_text().split('[env:esp32s3-16mb-appdata-iq-sharing]')[1].split('[env:')[0]
        for flag in ('-DRISC_INSTALLED_APP_CAPACITY=64','-DRISC_APP_POLICY_ROWS=24','-DRISC_APP_REQUIREMENT_ROWS=24'):
            self.assertIn(flag,inherited)
    @unittest.skipUnless(os.environ.get('EXPANDED_ENVDUMP_PATH'), 'optional actual PlatformIO envdump')
    def test_actual_platformio_compiler_environment(self):
        text=Path(os.environ['EXPANDED_ENVDUMP_PATH']).read_text()
        def field(name, wrapper=False):
            pattern=r"  '"+name+r"': "+(r"deque\((\[.*?\])\)" if wrapper else r"(\[.*?\])")+r",\n"
            match=re.search(pattern,text,re.S);self.assertIsNotNone(match,name)
            return ast.literal_eval(match.group(1))
        cxx=field('CXXFLAGS');self.assertIn('-std=gnu++17',cxx);self.assertNotIn('-std=gnu++11',cxx)
        defines=field('CPPDEFINES',True)
        pairs={};counts={}
        for value in defines:
            key,item=value if isinstance(value,tuple) else (value,True)
            pairs[key]=item;counts[key]=counts.get(key,0)+1
        for key,value in {'RISC_PROVIDER_CAPACITY':40,'RISC_INSTALLED_APP_CAPACITY':64,
                          'RISC_APP_POLICY_ROWS':32,'RISC_APP_REQUIREMENT_ROWS':32,
                          'RISC_PAIRED_APP_DATA_EXPANDED':1,'RISC_PAIRED_APP_DATA':1,
                          'RISC_NATIVE_RECOVERY_AXP2101':1,'RISC_NATIVE_FILE_SOURCE':1,
                          'RISC_NATIVE_APP_DATA_MOUNT':1}.items():
            self.assertEqual(pairs[key],value);self.assertEqual(counts[key],1)
        self.assertEqual(pairs['RISC_TARGET'].replace(chr(92),'').strip('"'),'esp32s3-16mb-appdata-expanded-iq')
        self.assertEqual(counts['RISC_TARGET'],1);self.assertNotIn('RISC_OWNER_INSTALLER',pairs)
        self.assertNotIn('-Wl,-u,risc_owner_maintenance_target',field('LINKFLAGS'))
        cppflags=text.split("  '_CPPDEFFLAGS':",1)[1].split("\n  '",1)[0]
        self.assertNotIn('-URISC_',cppflags)
        self.assertIn('[SUCCESS]',text)
if __name__=='__main__':unittest.main()
