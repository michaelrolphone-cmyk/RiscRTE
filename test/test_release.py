import importlib.util, json, pathlib, struct, sys, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from check_versions import version
from release_assets import esp_image, file_bytes, partition_table, sha
from publish_firmware import decision
class ReleaseTests(unittest.TestCase):
    def test_versions(self):
        self.assertGreater(version('0.2.0'),version('0.1.9'))
        for bad in ('1.2','1.2.3-rc1','01.2.3','1.2.65536','../v'):
            with self.assertRaises(ValueError):version(bad)
    def test_immutable_versions(self):
        r={'version':'1.2.3','tag':'firmware-v1.2.3'}
        self.assertIsNone(decision(r,[]))
        existing={'tag_name':r['tag'],'draft':False}
        self.assertEqual(decision(r,[existing]),existing)
        with self.assertRaises(ValueError):decision(r,[{'tag_name':'firmware-v1.2.4'}])
        with self.assertRaises(ValueError):decision(r,[existing,existing])
    def test_asset_presence(self):
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'test.bin'
            with self.assertRaises(ValueError):file_bytes(p)
            p.write_bytes(b'')
            with self.assertRaises(ValueError):file_bytes(p)
            p.write_bytes(b'test');q=p.with_suffix('.link');q.symlink_to(p)
            with self.assertRaises(ValueError):file_bytes(q)
    def test_image_rejection(self):
        for bad in (b'',b'\xe9'+bytes(100),bytes(100)):
            with self.assertRaises(ValueError):esp_image(bad)
        with self.assertRaises(ValueError):partition_table(bytes(3072))
if __name__=='__main__':unittest.main()
