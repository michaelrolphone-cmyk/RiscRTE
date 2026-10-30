import os,subprocess,tempfile,unittest
from unittest.mock import patch
from pathlib import Path
from run_resident_worker import compiler_dependencies,compiler_system_roots,dependency_key
class DependencyScan(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
  self.top=Path(self.tmp.name);self.root=self.top/'candidate';self.root.mkdir();self.base=self.top/'base';self.base.mkdir()
  self.child=self.root/'descendant.h';self.child.write_text('typedef int masked_type;\n')
  self.header=self.root/'masked.h';self.header.write_text('#pragma GCC system_header\n#include "descendant.h"\n')
  self.source=self.root/'source.cpp';self.source.write_text('#include "masked.h"\nmasked_type value;\n')
  self.prefix=['c++','-std=c++17','-I'+str(self.root)];self.roots,self.aliases=compiler_system_roots(self.prefix)
 def key(self,path,owned=None):return dependency_key(path.resolve(),self.root,self.base,owned or {'source.cpp','masked.h','descendant.h'},self.roots,self.aliases)
 def test_repository_pragma_is_still_pinned(self):
  before=subprocess.check_output([*self.prefix,'-MM',str(self.source)],text=True)
  self.assertNotIn(str(self.child),before)
  after=compiler_dependencies(self.prefix,self.source);self.assertIn(self.child,after)
  self.assertEqual(self.key(self.child),'candidate/descendant.h')
 def test_external_pragma_include_refused(self):
  external=self.top/'untrusted.h';external.write_text('#pragma GCC system_header\ntypedef int external_type;\n')
  self.header.write_text('#pragma GCC system_header\n#include "'+str(external)+'"\n')
  self.assertIn(external,compiler_dependencies(self.prefix,self.source))
  with self.assertRaisesRegex(ValueError,'Undeclared external dependency'):self.key(external)
 def test_repository_path_precedes_standard_classification(self):
  h=self.base/'pinned.h';h.write_text('#pragma GCC system_header\n')
  self.assertEqual(dependency_key(h,self.root,self.base,set(),[self.base]),'runtime/pinned.h')
 def test_external_symlink_refused(self):
  external=self.top/'untrusted.h';external.write_text('typedef int masked_type;\n')
  self.header.unlink();self.header.symlink_to(external)
  self.assertIn(external,compiler_dependencies(self.prefix,self.source))
  with self.assertRaisesRegex(ValueError,'Undeclared external dependency'):self.key(self.header)
 def test_environment_include_and_dependency_overrides_refused(self):
  for name in ('CPATH','CPLUS_INCLUDE_PATH','C_INCLUDE_PATH','OBJC_INCLUDE_PATH','GCC_EXEC_PREFIX','COMPILER_PATH','DEPENDENCIES_OUTPUT','SUNPRO_DEPENDENCIES'):
   for value in ('','/tmp/unqualified'):
    with self.subTest(name=name,value=value),patch.dict(os.environ,{name:value}):
     with self.assertRaisesRegex(ValueError,'Unqualified compiler environment override: '+name):compiler_system_roots(self.prefix)
     with self.assertRaisesRegex(ValueError,'Unqualified compiler environment override: '+name):compiler_dependencies(self.prefix,self.source)
 def test_standard_header_recorded_separately(self):
  self.source.write_text('#include <stdio.h>\n')
  dependencies=compiler_dependencies(self.prefix,self.source)
  standard=[p for p in dependencies if p!=self.source]
  self.assertTrue(standard)
  for path in standard:self.assertTrue(self.key(path).startswith('system/'))
if __name__=='__main__':unittest.main()
