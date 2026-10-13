import configparser, importlib.util, io, os, pathlib, struct, unittest
import runpy, tempfile
from unittest.mock import Mock, patch
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('radio_iq_proof',ROOT/'scripts/radio_iq_proof.py')
proof=importlib.util.module_from_spec(spec);spec.loader.exec_module(proof)
class RadioIqTarget(unittest.TestCase):
 def test_opt_in_only(self):
  c=configparser.ConfigParser(interpolation=None);c.read(ROOT/'platformio.ini')
  for section in c.sections():
   if section=='env:esp32s3-16mb-appdata-iq':continue
   self.assertNotIn('RISC_ENABLE_RADIO_IQ',c[section].get('build_flags',''))
  self.assertIn('-DRISC_ENABLE_RADIO_IQ=1',c['env:esp32s3-16mb-appdata-iq']['build_flags'])
  self.assertIn('radio_iq_build.py',c['env:esp32s3-16mb-appdata-iq']['extra_scripts'])
  self.assertEqual(c['env:esp32s3-16mb-appdata-iq']['build_flags'].count('RISC_TARGET'),1)
@unittest.skipUnless(os.environ.get('RADIO_IQ_ELF'),'requires the actual linked IQ target ELF')
class RadioIqElf(unittest.TestCase):
 def setUp(self):
  self.data=pathlib.Path(os.environ['RADIO_IQ_ELF']).read_bytes()
  self.elf=proof.ELFFile(io.BytesIO(self.data))
  self.expanded=os.environ.get('RADIO_IQ_EXPANDED')=='1'
  self.good=proof.prove(self.data,expanded=self.expanded)
 def test_final_image(self):
  self.assertEqual(self.good['bank_bytes'],65536)
 def test_every_bank_alias_and_load_segment(self):
  for address in (proof.BANK[0],proof.ALIAS[0]):
   for kind in ('section','segment'):
    data=bytearray(self.data)
    if kind=='section':
     index=next(i for i,s in enumerate(self.elf.iter_sections()) if s.name=='.dram0.data')
     offset=self.elf['e_shoff']+index*self.elf['e_shentsize']+12
    else:
     index=next(i for i,s in enumerate(self.elf.iter_segments()) if s['p_type']=='PT_LOAD' and s['p_memsz'])
     offset=self.elf['e_phoff']+index*self.elf['e_phentsize']+8
    struct.pack_into('<I',data,offset,address)
    with self.assertRaisesRegex(ValueError,'overlaps IQ bank'):proof.prove(data,expanded=self.expanded)
 def test_wrong_target(self):
  target=b'esp32s3-16mb-appdata-expanded-iq\0' if self.expanded else b'esp32s3-16mb-appdata-iq\0'
  data=self.data.replace(target,b'x'*(len(target)-1)+b'\0')
  with self.assertRaisesRegex(ValueError,'target identity'):proof.prove(data,expanded=self.expanded)
 def test_wrong_reservation(self):
  data=bytearray(self.data);needle=struct.pack('<II',*proof.BANK)
  # Match the actual table location rather than any unrelated constants.
  start=self.good['reservation_table_address']
  section=next(s for s in self.elf.iter_sections() if s['sh_addr']<=start<s['sh_addr']+s['sh_size'])
  at=section['sh_offset']+start-section['sh_addr']
  position=data.index(needle,at,at+8*len(self.good['reservation_table']))
  for changed_end in (proof.BANK[1]-4,proof.BANK[0],0):
   struct.pack_into('<I',data,position+4,changed_end)
   with self.assertRaisesRegex(ValueError,'pre-heap bank reservation'):proof.prove(data,expanded=self.expanded)
  data=bytearray(self.data)
  other=at if position!=at else at+8
  data[other:other+8]=needle
  with self.assertRaisesRegex(ValueError,'pre-heap bank reservation'):proof.prove(data,expanded=self.expanded)
@unittest.skipUnless(os.environ.get('RADIO_IQ_DISABLED_ELF'),'requires an actual non-IQ target ELF')
class RadioIqDisabledElf(unittest.TestCase):
 def test_no_reservation_in_existing_target(self):
  data=pathlib.Path(os.environ['RADIO_IQ_DISABLED_ELF']).read_bytes()
  elf=proof.ELFFile(io.BytesIO(data))
  symbols={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()}
  self.assertFalse(any('reserved_region_risc_radio_iq' in n for n in symbols))
  start=symbols['soc_reserved_memory_region_start']['st_value']
  end=symbols['soc_reserved_memory_region_end']['st_value']
  section=next(s for s in elf.iter_sections() if s['sh_addr']<=start and end<=s['sh_addr']+s['sh_size'])
  table=section.data()[start-section['sh_addr']:end-section['sh_addr']]
  self.assertNotIn(proof.BANK,list(struct.iter_unpack('<II',table)))
  with self.assertRaisesRegex(ValueError,'target identity'):proof.prove(data)
class RadioIqLayoutIdentity(unittest.TestCase):
 def test_exact_identity_before_unchanged_elf_checks(self):
  old=b'esp32s3-16mb-appdata-iq\0'
  new=b'esp32s3-16mb-appdata-expanded-iq\0RISC_PAIRED_STORE_ABI:3\0'
  with patch.object(proof,'ELFFile',side_effect=RuntimeError('structural verifier reached')):
   for data,expanded in ((old,False),(new,True)):
    with self.assertRaisesRegex(RuntimeError,'structural verifier reached'):proof.prove(data,expanded=expanded)
   for data,expanded in ((old,True),(new,False),(b'esp32s3-16mb-appdata-expanded-iq\0',True)):
    with self.assertRaises(ValueError):proof.prove(data,expanded=expanded)
   for marker in (old,b'RISC_PAIRED_STORE_ABI:1\0',b'RISC_PAIRED_STORE_ABI:2\0',b'RISC_OWNER_INSTALLER:1\0'):
    with self.assertRaisesRegex(ValueError,'exclusive ordinary ABI3'):proof.prove(new+marker,expanded=True)
 def test_post_link_hook_uses_final_compiler_definitions(self):
  class Env(dict):
   def subst(self,_):return str(ROOT)
   def AddPostAction(self,*_):pass
  for defines,expanded in (([],False),([('RISC_PAIRED_APP_DATA_EXPANDED',1)],True),(['RISC_PAIRED_APP_DATA_EXPANDED'],True)):
   env=Env(CPPDEFINES=defines)
   module=runpy.run_path(str(ROOT/'scripts/radio_iq_build.py'),init_globals={'env':env,'Import':lambda _:None})
   fake=Mock(return_value={'bank_base':proof.BANK[0]})
   module['verify'].__globals__['prove']=fake
   with tempfile.TemporaryDirectory() as tmp:
    target=pathlib.Path(tmp)/'firmware.elf';target.write_bytes(b'fixture')
    module['verify'](None,[target],env)
    fake.assert_called_once_with(b'fixture',expanded=expanded)
if __name__=='__main__':unittest.main()
