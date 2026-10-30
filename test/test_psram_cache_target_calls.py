import importlib.util, pathlib, unittest
p=pathlib.Path(__file__).resolve().parents[1]/'scripts/verify_psram_cache_target.py'
s=importlib.util.spec_from_file_location('target',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Calls(unittest.TestCase):
 def setUp(self):
  self.symbols={'sdk':(100,20,'t'),'entry':(200,20,'t'),'lambda':(300,20,'t'),'alias':(300,20,'t'),'__wrap_esp_cache_msync':(400,20,'t')}
  self.d={'sdk':'100: call8 400 <__wrap_esp_cache_msync>','entry':'200: call8 999 <core>','lambda':'300: call8 400 <__wrap_esp_cache_msync>','alias':'300: call8 400 <__wrap_esp_cache_msync>','__wrap_esp_cache_msync':'400: call8 500 <esp_cache_msync>'}
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
if __name__=='__main__':unittest.main()
