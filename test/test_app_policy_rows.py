"""Packaging cannot silently relabel a sixteen-row Runtime as seventeen-row."""
import configparser
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from paired_candidate import policy_rows_proof


class AppPolicyRows(unittest.TestCase):
    def test_matching_artifacts_and_wrong_or_missing_marker(self):
        for rows in (16, 17):
            marker = f'RISC_APP_POLICY_ROWS:{rows}'.encode() + b'\0'
            blobs = dict.fromkeys(('firmware.bin', 'firmware.elf'), marker)
            proof = policy_rows_proof(blobs, rows)
            self.assertEqual(proof['rows'], rows)
            self.assertEqual(proof['live_app_grants'], 16)
            self.assertEqual(proof['manifest_requirements'], 16)
            with self.assertRaises(ValueError):
                policy_rows_proof(blobs, 33 - rows)
            for name in blobs:
                for bad in (b'', marker[:-1], marker + f'RISC_APP_POLICY_ROWS:{33-rows}'.encode() + b'\0'):
                    with self.assertRaises(ValueError):
                        policy_rows_proof({**blobs, name: bad}, rows)
        with self.assertRaises(ValueError):
            policy_rows_proof({}, 18)

    def test_only_explicit_environments_select_seventeen(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / 'platformio.ini')
        opted = set()
        for name in config.sections():
            flags = config[name].get('build_flags', '')
            if '-DRISC_APP_POLICY_ROWS=' in flags:
                self.assertIn('-DRISC_APP_POLICY_ROWS=17', flags)
                self.assertTrue(name.endswith('-policy17'))
                opted.add(name)
        self.assertEqual(opted, {'env:esp32s3-16mb-appdata' + suffix + '-policy17'
                                for suffix in ('', '-iq', '-perf', '-iq-perf')})


if __name__ == '__main__':
    unittest.main()
