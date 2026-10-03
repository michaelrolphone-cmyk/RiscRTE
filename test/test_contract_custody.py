import hashlib,json,pathlib,sys,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from hardware_gate import result,CONTEXT
class ContractCustody(unittest.TestCase):
    def test_pinned_watch_sources(self):
        folder=ROOT/'test/fixtures/watch';record=json.loads((folder/'PROVENANCE.json').read_text())
        self.assertEqual(record['commit'],'e48a540cdc2c4c1e642fbf20807e2743e2a679a9')
        for name,digest in record['files'].items():
            self.assertEqual(hashlib.sha256((folder/name).read_bytes()).hexdigest(),digest,name)
    def test_pinned_garden_sources(self):
        folder=ROOT/'test/fixtures/garden';record=json.loads((folder/'PROVENANCE.json').read_text())
        for name,meta in record['files'].items():
            self.assertEqual(hashlib.sha256((folder/name).read_bytes()).hexdigest(),meta['sha256'],name)
    def test_missing_and_newest_hardware_result(self):
        self.assertIsNone(result([], 'exactsha'))
        self.assertIsNone(result([{'context':CONTEXT,'state':'pending'}], 'exactsha'))
        self.assertFalse(result([{'context':CONTEXT,'state':'failure'},{'context':CONTEXT,'state':'success'}], 'exactsha'))
        self.assertTrue(result([{'context':CONTEXT,'state':'success'}], 'exactsha'))
