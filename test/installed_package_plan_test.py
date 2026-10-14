import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from installed_package_plan import plan


def cap(name): return {'capability': name, 'api': 1}
def package(ident, needs=(), offers=(), kind='driver'):
    return {'id': ident, 'kind': kind, 'requires': [cap(n) for n in needs],
            'provides': [cap(n) for n in offers], 'files': [{
            'internal_path': ident + '.elf', 'sd_path': 'Packages/' + ident + '.elf',
            'bytes': 4096, 'sha256': 'a' * 64, 'url': 'https://example.org/pinned/' + ident + '.elf'}]}


class PlanTests(unittest.TestCase):
    def setUp(self):
        self.p = [package('transport', offers=['bus']), package('sd', ['bus'], ['storage.volume']),
                  package('scene', offers=['ui.scene']), package('new-app', ['ui.scene'], kind='application')]
    def run_plan(self, **kw):
        return plan(self.p, ['new-app'], ['sd'], sd_bytes=1024*1024, **kw)
    def test_sd_bootstrap_closure(self):
        result = self.run_plan(sd_ready=True)
        self.assertEqual(result['bootstrap_internal'], ['sd', 'transport'])
        self.assertEqual([f['medium'] for f in result['files']], ['internal', 'internal', 'sd', 'sd'])
        self.assertEqual(result['activation_order'], ['transport', 'sd', 'scene', 'new-app'])
    def test_no_sd_internal_fallback(self):
        self.assertEqual({f['medium'] for f in self.run_plan()['files']}, {'internal'})
    def test_missing(self):
        self.p.pop(2)
        with self.assertRaises(ValueError): self.run_plan()
    def test_cycle(self):
        self.p[0]['requires'] = [cap('storage.volume')]
        with self.assertRaises(ValueError): self.run_plan()
    def test_ambiguous(self):
        self.p.append(package('scene2', offers=['ui.scene']))
        with self.assertRaises(ValueError): self.run_plan()
        r = self.run_plan(bindings={('new-app', 'ui.scene', 1): 'scene2'})
        self.assertIn('scene2', r['activation_order']); self.assertNotIn('scene', r['activation_order'])
    def test_native_dependency(self):
        self.p[0]['requires'] = [cap('platform.spi')]
        with self.assertRaises(ValueError): self.run_plan()
        self.run_plan(native=[cap('platform.spi')])
    def test_wrong_api(self):
        self.p[-1]['requires'][0]['api'] = 2
        with self.assertRaises(ValueError): self.run_plan()
    def test_capacity(self):
        with self.assertRaises(ValueError): self.run_plan(internal_bytes=5*4096)
        with self.assertRaises(ValueError):
            plan(self.p, ['new-app'], ['sd'], sd_ready=True, sd_bytes=1,
                 internal_bytes=5*4096)
    def test_full_sd_internal_fallback(self):
        r = plan(self.p, ['new-app'], ['sd'], sd_ready=True, sd_bytes=1)
        self.assertEqual({f['medium'] for f in r['files']}, {'internal'})
        self.assertEqual(r['sd_payload_bytes'], 0)
    def test_partial_sd_capacity(self):
        r = plan(self.p, ['new-app'], ['sd'], sd_ready=True, sd_bytes=4096)
        self.assertEqual([f['medium'] for f in r['files']],
                         ['internal', 'internal', 'sd', 'internal'])
        self.assertEqual(r['sd_payload_bytes'], 4096)
    def test_package_not_split_between_media(self):
        self.p[-1]['files'].append(dict(self.p[-1]['files'][0],
            internal_path='metadata.json', sd_path='Packages/metadata.json', bytes=32))
        r = plan(self.p, ['new-app'], ['sd'], sd_ready=True, sd_bytes=8192)
        self.assertEqual([f['medium'] for f in r['files'] if f['package']=='new-app'],
                         ['internal', 'internal'])
        self.assertEqual(r['sd_payload_bytes'], 4096)
    def test_duplicate_path(self):
        self.p[-1]['files'][0]['internal_path'] = 'sd.elf'
        with self.assertRaises(ValueError): self.run_plan()
    def test_invalid_file(self):
        self.p[-1]['files'][0]['sd_path'] = '../app.elf'
        with self.assertRaises(ValueError): self.run_plan(sd_ready=True)
    def test_input_unchanged(self):
        before = copy.deepcopy(self.p); self.run_plan(); self.assertEqual(before, self.p)
    def test_unused_binding(self):
        with self.assertRaises(ValueError): self.run_plan(bindings={('foreign', 'ui.scene', 1): 'scene'})


if __name__ == '__main__': unittest.main()
