#!/usr/bin/env python3
"""Offline source and private-layout pin refusal tests."""
import importlib.util,pathlib,shutil,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
PIN=ROOT/'experimental/native_replica/pinned_littlefs'
class Pins(unittest.TestCase):
 def fixture(self):
  tmp=tempfile.TemporaryDirectory(prefix='idf6-pin-');self.addCleanup(tmp.cleanup)
  pin=pathlib.Path(tmp.name)/'pin';shutil.copytree(PIN,pin,ignore=shutil.ignore_patterns('__pycache__'))
  spec=importlib.util.spec_from_file_location('pin',pin/'prepare_idf6.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  return pin,module
 def test_exact(self):
  pin,module=self.fixture();module.prepare(pin/'generated')
  self.assertEqual((pin/'vendor/littlefs/lfs.c').read_bytes(),(pin/'generated/littlefs/lfs.c').read_bytes())
  self.assertNotEqual((pin/'vendor/esp_littlefs.c').read_bytes(),(pin/'generated/esp_littlefs.c').read_bytes())
  for upstream,notice in [('LICENSE','LICENSE-esp-littlefs.txt'),('littlefs/LICENSE.md','LICENSE-littlefs.txt')]:
   self.assertEqual((pin/'vendor'/upstream).read_bytes(),(pin/'generated'/upstream).read_bytes())
   normalized=lambda text:'\n'.join(line.rstrip() for line in text.splitlines())
   self.assertEqual(normalized((pin/'vendor'/upstream).read_text()),normalized((pin/notice).read_text()))
 def test_modified_source_refuses(self):
  pin,module=self.fixture();f=pin/'vendor/esp_littlefs.c';f.write_bytes(f.read_bytes()+b'\n')
  with self.assertRaisesRegex(SystemExit,'hash mismatch'):module.prepare(pin/'generated')
  self.assertFalse((pin/'generated').exists())
 def test_missing_source_refuses(self):
  pin,module=self.fixture();(pin/'vendor/littlefs/lfs.c').unlink()
  with self.assertRaisesRegex(SystemExit,'inventory mismatch'):module.prepare(pin/'generated')
  self.assertFalse((pin/'generated').exists())
 def test_private_layout_refuses(self):
  pin,module=self.fixture();f=pin/'littlefs_api.h';f.write_bytes(f.read_bytes()+b'\n')
  with self.assertRaisesRegex(SystemExit,'declaration differs'):module.prepare(pin/'generated')
  self.assertFalse((pin/'generated').exists())
 def test_missing_license_refuses(self):
  for name in ('LICENSE','littlefs/LICENSE.md'):
   with self.subTest(name=name):
    pin,module=self.fixture();(pin/'vendor'/name).unlink()
    with self.assertRaisesRegex(SystemExit,'inventory mismatch'):module.prepare(pin/'generated')
    self.assertFalse((pin/'generated').exists())
 @unittest.skipUnless(shutil.which('cmake'),'CMake is needed for the discovery-pass check')
 def test_dependency_discovery_is_read_only(self):
  with tempfile.TemporaryDirectory(prefix='idf6-pin-cmake-') as temp:
   work=pathlib.Path(temp);script=work/'check.cmake'
   script.write_text('''set(CMAKE_BUILD_EARLY_EXPANSION TRUE)
macro(idf_component_register)
 set(registered TRUE)
 return()
endmacro()
include("'''+str(ROOT/'idf/components/risc_littlefs/CMakeLists.txt')+'''")
if(NOT registered)
 message(FATAL_ERROR "Component requirements were not registered")
endif()
''')
   subprocess.run(['cmake','-P',str(script)],cwd=work,check=True,capture_output=True)
   self.assertEqual(list(work.iterdir()),[script])
if __name__=='__main__':unittest.main()
