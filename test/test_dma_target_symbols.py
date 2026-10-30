import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from verify_resident_worker_target import required_dram_symbols
class Symbols(unittest.TestCase):
 def test_actual_data(self):
  self.assertEqual(required_dram_symbols('3fc00100 000000a0 b ns::authority',['ns::authority']),{'ns::authority':{'address':0x3fc00100,'bytes':160}})
 def test_empty_optional(self):self.assertEqual(required_dram_symbols('',[]),{})
 def test_missing(self):
  with self.assertRaises(AssertionError):required_dram_symbols('',['ns::authority'])
 def test_duplicate_symbol(self):
  with self.assertRaises(AssertionError):required_dram_symbols('3fc00100 10 b ns::authority\n3fc00200 10 b ns::authority',['ns::authority'])
 def test_duplicate_request(self):
  with self.assertRaises(AssertionError):required_dram_symbols('3fc00100 10 b ns::authority',['ns::authority','ns::authority'])
 def test_wrong_region_or_extent(self):
  for address,size in [('42000000','10'),('3fcfffff','10'),('3fc00100','0'),('3fc00100','1001')]:
   with self.subTest(address=address,size=size),self.assertRaises(AssertionError):required_dram_symbols(f'{address} {size} b ns::authority',['ns::authority'])
 def test_nonallocated_or_code(self):
  for kind in ['A','T','t','U']:
   with self.subTest(kind=kind),self.assertRaises(AssertionError):required_dram_symbols(f'3fc00100 10 {kind} ns::authority',['ns::authority'])
if __name__=='__main__':unittest.main()
