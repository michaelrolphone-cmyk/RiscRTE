#!/usr/bin/env python3
"""Synthetic producer-admission regressions, not target loader execution."""
import struct,unittest
from build_fixture import validate_image
class Image(unittest.TestCase):
 def setUp(self):
  self.data=bytearray(52);self.data[:7]=b'\x7fELF\x01\x01\x01';struct.pack_into('<HH',self.data,16,3,94)
  self.rows=['0: 00000000 0 NOTYPE LOCAL DEFAULT UND','1: 00001000 20 FUNC GLOBAL DEFAULT 1 t5_driver_get','2: 00000000 0 NOTYPE GLOBAL DEFAULT UND strcmp']
 def run_image(self):return validate_image(self.data,self.rows,'t5_driver_get',{'strcmp','memset'})
 def test_valid(self):self.assertEqual(self.run_image(),['strcmp'])
 def test_truncated(self):
  self.data=self.data[:20]
  with self.assertRaisesRegex(AssertionError,'ELF32'):self.run_image()
 def test_architecture(self):
  struct.pack_into('<H',self.data,18,62)
  with self.assertRaisesRegex(AssertionError,'Xtensa'):self.run_image()
 def test_endian(self):
  self.data[5]=2
  with self.assertRaisesRegex(AssertionError,'little-endian'):self.run_image()
 def test_entry_is_defined_function(self):
  self.rows[1]=self.rows[1].replace('FUNC','OBJECT')
  with self.assertRaisesRegex(AssertionError,'Missing exported entry'):self.run_image()
 def test_hidden_entry(self):
  self.rows[1]=self.rows[1].replace('DEFAULT','HIDDEN')
  with self.assertRaisesRegex(AssertionError,'Missing exported entry'):self.run_image()
 def test_unexpected_global_import(self):
  self.rows.append('3: 00000000 0 NOTYPE GLOBAL DEFAULT UND host_private')
  with self.assertRaisesRegex(AssertionError,'Unexpected imports'):self.run_image()
 def test_unexpected_weak_import(self):
  self.rows.append('3: 00000000 0 NOTYPE WEAK DEFAULT UND host_private')
  with self.assertRaisesRegex(AssertionError,'Unexpected imports'):self.run_image()
if __name__=='__main__':unittest.main()
