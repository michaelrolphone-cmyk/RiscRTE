import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from verify_resident_worker_target import fixture_sources
class Selection(unittest.TestCase):
 def test_normal(self):self.assertEqual(fixture_sources([],None),set())
 def test_legacy(self):self.assertEqual(fixture_sources([],None,True),set())
 def test_selected(self):self.assertEqual(fixture_sources(['test/probe.cpp'],'PROBE result=passed'),{'test/probe.cpp'})
 def test_unpaired(self):
  for paths,marker in [([], 'marker'),(['test/probe.cpp'],None),(['test/probe.cpp'],'')]:
   with self.subTest(paths=paths,marker=marker),self.assertRaises(AssertionError):fixture_sources(paths,marker)
 def test_ambiguous(self):
  with self.assertRaises(AssertionError):fixture_sources(['test/probe.cpp'],'marker',True)
 def test_invalid_paths(self):
  for path in ['/tmp/probe.cpp','../probe.cpp','test/../probe.cpp','test/probe.h']:
   with self.subTest(path=path),self.assertRaises(AssertionError):fixture_sources([path],'marker')
 def test_duplicate(self):
  with self.assertRaises(AssertionError):fixture_sources(['test/probe.cpp']*2,'marker')
if __name__=='__main__':unittest.main()
