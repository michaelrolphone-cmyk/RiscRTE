import configparser, importlib.util, io, os, pathlib, struct, unittest
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
@unittest.skipUnless(os.environ.get('RADIO_IQ_ELF'),'requires the actual linked IQ target ELF')
class RadioIqElf(unittest.TestCase):
 def setUp(self):
  self.data=pathlib.Path(os.environ['RADIO_IQ_ELF']).read_bytes()
  self.elf=proof.ELFFile(io.BytesIO(self.data))
  self.good=proof.prove(self.data)
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
    with self.assertRaisesRegex(ValueError,'overlaps IQ bank'):proof.prove(data)
 def test_wrong_reservation(self):
  data=bytearray(self.data);needle=struct.pack('<II',*proof.BANK)
  # Match the actual table location rather than any unrelated constants.
  start=self.good['reservation_table_address']
  section=next(s for s in self.elf.iter_sections() if s['sh_addr']<=start<s['sh_addr']+s['sh_size'])
  at=section['sh_offset']+start-section['sh_addr']
  position=data.index(needle,at,at+8*len(self.good['reservation_table']))
  struct.pack_into('<I',data,position+4,proof.BANK[1]-4)
  with self.assertRaisesRegex(ValueError,'pre-heap bank reservation'):proof.prove(data)
if __name__=='__main__':unittest.main()
