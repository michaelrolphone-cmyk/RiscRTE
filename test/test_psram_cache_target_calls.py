import importlib.util, pathlib, unittest
p=pathlib.Path(__file__).resolve().parents[1]/'scripts/verify_psram_cache_target.py'
s=importlib.util.spec_from_file_location('target',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Calls(unittest.TestCase):
 def setUp(self):
  self.symbols={'sdk':(0x100,20,'t'),'esp_cache_msync':(0x500,20,'T'),'entry':(200,20,'t'),'lambda':(300,20,'t'),'alias':(300,20,'t'),'__wrap_esp_cache_msync':(0x400,20,'t')}
  self.d={'sdk':'100: 000005 call8 400 <__wrap_esp_cache_msync>','entry':'200: 000005 call8 999 <core>','lambda':'300: 000005 call8 400 <__wrap_esp_cache_msync>','alias':'300: 000005 call8 400 <__wrap_esp_cache_msync>','__wrap_esp_cache_msync':'400: 000005 call8 500 <esp_cache_msync>'}
 def run_check(self):return m.require_linked_calls(self.symbols,self.d,['sdk'],['entry','lambda','alias'])
 def test_lambda_and_alias(self):self.assertEqual(self.run_check(),[300])
 def test_missing_sdk(self):
  self.d['sdk']='100: nop'
  with self.assertRaisesRegex(AssertionError,'SDK wrap'):self.run_check()
 def test_missing_native(self):
  self.d['lambda']=self.d['alias']='300: nop'
  with self.assertRaisesRegex(AssertionError,'one native'):self.run_check()
 def test_header_is_not_call(self):
  self.d['lambda']=self.d['alias']='300 <__wrap_esp_cache_msync>:\n300: nop'
  with self.assertRaisesRegex(AssertionError,'one native'):self.run_check()
 def test_multiple_native_addresses(self):
  self.symbols['alias']=(301,20,'t')
  with self.assertRaisesRegex(AssertionError,'one native'):self.run_check()
 def test_no_forward(self):
  self.d['__wrap_esp_cache_msync']='400: nop'
  with self.assertRaisesRegex(AssertionError,'forward'):self.run_check()
 def test_indirect_unproven(self):
  self.d['sdk']='100: callx8 a8'
  with self.assertRaisesRegex(AssertionError,'SDK wrap'):self.run_check()
class LongCalls(unittest.TestCase):
 def setUp(self):
  self.symbols={'target':(0x4037b9c0,16,'T')}
  self.text='4200a653: d99981 l32r a8, 42000cb8 <literal> (4037b9c0 <target>)\n4200a656: 0008e0 callx8 a8'
 def check(self,text=None,value=0x4037b9c0):return m.demonstrated_call(self.text if text is None else text,'target',self.symbols,lambda address:value if address==0x42000cb8 else None)
 def test_adjacent_actual_literal(self):self.assertTrue(self.check())
 def test_annotations_not_authority(self):self.assertTrue(self.check(self.text.replace('(4037b9c0 <target>)','(00000000 <wrong>)')))
 def test_wrong_literal(self):self.assertFalse(self.check(value=0x4037b9c4))
 def test_wrong_register(self):self.assertFalse(self.check(self.text.replace('callx8 a8','callx8 a9')))
 def test_gap(self):self.assertFalse(self.check(self.text.replace('4200a656','4200a659')))
 def test_clobber(self):self.assertFalse(self.check(self.text.replace('4200a656: 0008e0 callx8 a8','4200a656: 000006 movi a8,0\n4200a659: 0008e0 callx8 a8')))
 def test_unknown_indirect(self):self.assertFalse(self.check('4200a656: 0008e0 callx8 a8'))
 def test_wrong_direct_address(self):self.assertFalse(self.check('4200a656: 000005 call8 4037b9c4 <target>'))
 def test_elf_literal_bounds(self):
  import struct
  data=bytearray(96);data[:7]=b'\x7fELF\x01\x01\x01';struct.pack_into('<HH',data,16,2,94);struct.pack_into('<I',data,32,52);struct.pack_into('<HH',data,46,40,1);struct.pack_into('<IIIIIIIIII',data,52,0,1,2,0x42000cb8,92,4,0,0,4,0);struct.pack_into('<I',data,92,0x4037b9c0)
  self.assertEqual(m.elf_word(data,0x42000cb8),0x4037b9c0)
  with self.assertRaisesRegex(AssertionError,'uniquely'):m.elf_word(data,0x42000cb9)
  struct.pack_into('<I',data,56,8)
  with self.assertRaisesRegex(AssertionError,'uniquely'):m.elf_word(data,0x42000cb8)
if __name__=='__main__':unittest.main()
