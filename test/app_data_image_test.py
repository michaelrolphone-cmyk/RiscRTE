import hashlib,importlib.util,json,pathlib,tempfile,unittest,os,shutil
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('app_data_image',ROOT/'scripts/app_data_image.py');image=importlib.util.module_from_spec(spec);spec.loader.exec_module(image)
# Synthetic reviewed-tool fixture tests packaging guards, not real LittleFS.
TOOL='''#!/usr/bin/env python3
import pathlib,sys
args=sys.argv[1:]
if '-c' in args:
 payload=bytearray(b'\\xff'*524288);payload[32:40]=b'littlefs';pathlib.Path(args[-1]).write_bytes(payload)
'''
class Images(unittest.TestCase):
 def test_custody_no_overwrite_and_manifest(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);tool=root/'fake-reviewed-tool';tool.write_text(TOOL);tool.chmod(0o700);sha=hashlib.sha256(tool.read_bytes()).hexdigest()
   with self.assertRaises(ValueError):image.build(tool,'0'*64,root/'bad')
   self.assertFalse((root/'bad').exists())
   record=image.build(tool,sha,root/'out')
   self.assertEqual(record['kind'],'littlefs-interoperability-fixture');self.assertFalse(record['initial_provisioning_image']);self.assertNotIn('layout',record);self.assertFalse(record['native_mount_verified'])
   self.assertEqual(record['size_bytes'],524288);self.assertEqual(record['tool_sha256'],sha)
   self.assertEqual(json.loads((root/'out/appdata-image.json').read_text()),record)
   with self.assertRaises(ValueError):image.build(tool,sha,root/'out')
   (root/'alias').symlink_to(root/'out',target_is_directory=True)
   with self.assertRaises(ValueError):image.build(tool,sha,root/'alias')
 def test_verified_initial_guards(self):
  supplied=os.environ.get('APP_DATA_INITIAL_IMAGE')
  if not supplied:self.skipTest('Pass real initial image for independent package custody tests')
  source=pathlib.Path(supplied);original=image.verify_initial(source)
  with tempfile.TemporaryDirectory() as d:
   target=pathlib.Path(d)/'copy';shutil.copytree(source,target)
   for field,value in [('disk_version','2.0'),('initial_provisioning_image',False),('kind','littlefs-interoperability-fixture'),('core_mount_verified',False),('littlefs_commit','0'*40),('offset',0x310000),('native_mount_verified',True),('scope','Hardware qualified'),('physical_verification','passed')]:
    record=dict(original);record[field]=value;(target/'appdata-image.json').write_text(json.dumps(record))
    with self.assertRaises(ValueError):image.verify_initial(target)
   shutil.copyfile(source/'appdata-image.json',target/'appdata-image.json')
   bad=bytearray((target/'appdata.bin').read_bytes());bad[300]^=1;(target/'appdata.bin').write_bytes(bad)
   with self.assertRaises(ValueError):image.verify_initial(target)
if __name__=='__main__':unittest.main()
