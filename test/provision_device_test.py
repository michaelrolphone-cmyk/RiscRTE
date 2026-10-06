#!/usr/bin/env python3
"""Offline blank-device package checks; real official NVS roundtrip when supplied."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import provision_device as d
import provision_profile as p
import provision_seed as s

ROOT = Path(__file__).resolve().parents[1]
BASE = 'https://packages.example.invalid/owner-pinned/'
GENERATOR = os.environ.get('NVS_GENERATOR')


class Device(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tools = tempfile.TemporaryDirectory()
        cls.validator = Path(cls.tools.name) / 'provision-input'
        subprocess.run(['bash', str(ROOT / 'scripts/build_provision_input_tool.sh'), str(cls.validator)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tools.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def inputs(self, app_data=False, count=3):
        self.layout = 'riscrte-paired-appdata-v2' if app_data else 'riscrte-paired-16m-v1'
        store = self.root / 'store'; store.mkdir()
        names = ['boot.json', 'board.json', 'default.elf'] + [f'app{n:03}.elf' for n in range(count - 3)]
        for name in names:
            (store / name).write_bytes(b'not-native-ELF-test-only')
        inventory = self.root / 'inventory.json'
        inventory.write_bytes(p.encode(p.pin_store(store, self.layout, BASE, target='esp32s3-16mb-appdata' if app_data else s.TARGET)))
        wifi = self.root / 'wifi.json'; wifi.write_bytes(p.encode({'ssid': 'dummy-only', 'password': 'dummy-password'}))
        p.build_profile(inventory, store, wifi, self.validator, 'time.example.invalid', self.root / 'owner', BASE)
        payloads = {name: b'fixture-' + name.encode() for name in s.PAYLOADS}
        payloads['firmware.bin'] = b'fixture-firmware' * 32
        payloads['bootfs0.bin'] = b'\xff' * p.LAYOUTS[self.layout]
        payloads['otadata.bin'] = s.initial_otadata()
        payloads['bank_state.bin'] = s.initial_bank_state(payloads['firmware.bin'], payloads['bootfs0.bin'], app_data)
        if app_data:
            payloads.update({'appdata.bin': b'\xff' * 0x80000, 'appdata-image.json': b'{}'})
        candidate = {'layout': self.layout, 'store_abi': 2 if app_data else 1,
                     'target': 'esp32s3-16mb-appdata' if app_data else s.TARGET,
                     'firmware_version': '0.1.31', 'source_sha': 'a' * 40}
        payloads['candidate.json'] = p.encode(candidate)
        for name in s.SEED_FILES:
            payloads[name] = (store / name).read_bytes()
        record = {'schema': 'riscrte.provisioning-seed', 'schema_version': 1, **candidate,
                  'segments': s.segments(app_data), 'assets': {name: {'bytes': len(blob), 'sha256': p.sha(blob)} for name, blob in payloads.items()}}
        self.seed = self.root / 'seed'; self.seed.mkdir()
        for name, blob in payloads.items():
            (self.seed / name).write_bytes(blob)
        (self.seed / 'seed.json').write_bytes(p.encode(record))
        self.seed_checks()
        self.candidate = candidate
        return record, payloads

    def seed_checks(self):
        (self.seed / 'SHA256SUMS').write_bytes(''.join(f'{p.sha(path.read_bytes())}  {path.name}\n'
            for path in sorted(self.seed.iterdir()) if path.name != 'SHA256SUMS').encode())

    def compose(self, name='out', new_device=True):
        return d.compose(self.seed, self.root / 'owner', self.validator,
                         Path(GENERATOR or '/not-used'), self.root / name, new_device)

    def test_abi1_abi2_segments_blank_inactive_and_private_modes(self):
        for app_data in (False, True):
            with self.subTest(app_data=app_data):
                if app_data:
                    self.temp.cleanup(); self.setUp()
                record, blobs = self.inputs(app_data)
                with patch.object(s, 'candidate', return_value=(self.candidate, {})), \
                     patch.object(d, 'make_nvs', return_value=b'\xa5' * d.NVS_BYTES):
                    result = self.compose()
                    self.compose('again')
                out = self.root / 'out'
                image = (out / 'first-install.bin').read_bytes()
                self.assertEqual(len(image), 0x1000000)
                self.assertEqual(image[0x9000:0xf000], b'\xa5' * d.NVS_BYTES)
                self.assertEqual(image[0x800000:0xff0000], b'\xff' * (0xff0000 - 0x800000))
                self.assertEqual(result['store_abi'], 2 if app_data else 1)
                self.assertEqual(result['segments']['bootfs0.bin'], 0x2f0000 if app_data else 0x310000)
                self.assertEqual('appdata.bin' in result['segments'], app_data)
                for name, offset in result['segments'].items():
                    self.assertEqual(image[offset:offset + len((out / name).read_bytes())], (out / name).read_bytes())
                self.assertEqual(out.stat().st_mode & 0o777, 0o700)
                for path in out.iterdir():
                    self.assertEqual(path.stat().st_mode & 0o777, 0o600)
                    self.assertEqual(path.read_bytes(), (self.root / 'again' / path.name).read_bytes())
                self.assertEqual((out / 'COMPLETE').read_bytes(), b'riscrte.first-install.v1\n')

    def test_new_device_no_overwrite_and_layout_guard(self):
        self.inputs()
        self.assertRaisesRegex(ValueError, 'new-device', self.compose, 'bad', False)
        self.assertFalse((self.root / 'bad').exists())
        (self.root / 'out').mkdir(); (self.root / 'out/keep').write_bytes(b'untouched')
        self.assertRaisesRegex(ValueError, 'already exists', self.compose)
        self.assertEqual((self.root / 'out/keep').read_bytes(), b'untouched')
        owner = p.decode((self.root / 'owner/owner.json').read_bytes())
        owner['layout'] = 'riscrte-paired-appdata-v2'
        (self.root / 'owner/owner.json').write_bytes(p.encode(owner))
        with patch.object(s, 'candidate', return_value=(self.candidate, {})):
            self.assertRaisesRegex(ValueError, 'layout mismatch', self.compose, 'mismatch')
        self.assertFalse((self.root / 'mismatch').exists())
        owner['layout'] = self.layout; owner['runtime_target'] = 'esp32s3-16mb-appdata-iq'
        (self.root / 'owner/owner.json').write_bytes(p.encode(owner))
        with patch.object(s, 'candidate', return_value=(self.candidate, {})):
            self.assertRaisesRegex(ValueError, 'Runtime target mismatch', self.compose, 'target-mismatch')
        self.assertFalse((self.root / 'target-mismatch').exists())

    def test_missing_time_refuses_before_image_generation(self):
        self.inputs()
        owner_path = self.root / 'owner/owner.json'
        owner = p.decode(owner_path.read_bytes())
        with patch.object(s, 'candidate', return_value=(self.candidate, {})), patch.object(d, 'make_nvs') as generator:
            owner.pop('time_server'); owner_path.write_bytes(p.encode(owner))
            self.assertRaises(KeyError, self.compose)
            owner['time_server'] = ''; owner_path.write_bytes(p.encode(owner))
            self.assertRaises(subprocess.CalledProcessError, self.compose)
            generator.assert_not_called()
        self.assertFalse((self.root / 'out').exists())

    def test_seed_hash_journal_completion_and_extra_refusal(self):
        self.inputs()
        with tempfile.TemporaryDirectory() as temp:
            (self.seed / 'firmware.bin').write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError, 'digest mismatch'):
                d.verify_seed(self.seed, Path(temp))
        # Rebuild a fresh modeled seed and then provide a hash-correct wrong journal.
        self.temp.cleanup(); self.setUp(); self.inputs()
        (self.seed / 'bank_state.bin').write_bytes(b'\xff' * 8192)
        record = p.decode((self.seed / 'seed.json').read_bytes())
        record['assets']['bank_state.bin']['sha256'] = p.sha(b'\xff' * 8192)
        (self.seed / 'seed.json').write_bytes(p.encode(record)); self.seed_checks()
        with tempfile.TemporaryDirectory() as temp, patch.object(s, 'candidate', return_value=(self.candidate, {})):
            self.assertRaisesRegex(ValueError, 'journal binding', d.verify_seed, self.seed, Path(temp))
        (self.seed / 'unexpected').write_bytes(b'extra')
        with tempfile.TemporaryDirectory() as temp:
            self.assertRaisesRegex(ValueError, 'extra seed files', d.verify_seed, self.seed, Path(temp))
        (self.seed / 'unexpected').unlink(); (self.seed / 'SHA256SUMS').unlink()
        with tempfile.TemporaryDirectory() as temp:
            self.assertRaisesRegex(ValueError, 'incomplete', d.verify_seed, self.seed, Path(temp))

    def test_interrupted_write_cleanup_and_generator_failure(self):
        self.inputs()
        original_write = d.write
        def interrupted(path, data):
            if path.name == 'first-install.bin':
                raise KeyboardInterrupt()
            return original_write(path, data)
        with patch.object(s, 'candidate', return_value=(self.candidate, {})), \
             patch.object(d, 'make_nvs', return_value=b'\xa5' * d.NVS_BYTES), \
             patch.object(d, 'write', side_effect=interrupted):
            self.assertRaises(KeyboardInterrupt, self.compose)
        self.assertFalse((self.root / 'out').exists())
        with patch.object(s, 'candidate', return_value=(self.candidate, {})), \
             patch.object(d, 'make_nvs', side_effect=OSError('fixture generator failure')):
            self.assertRaises(OSError, self.compose)
        self.assertFalse((self.root / 'out').exists())
        self.assertTrue((self.root / 'owner/COMPLETE').is_file())
        generator = self.root / 'unverified-generator'; generator.write_bytes(b'not official')
        self.assertRaisesRegex(ValueError, 'unverified official', d.make_nvs,
                               generator, self.root / 'owner/inputs', self.root)

    def test_corrupt_output_write_never_receives_completion_marker(self):
        self.inputs()
        original = d.write
        def corrupt(path, data):
            if path.name == 'nvs.bin':
                data = bytes([data[0] ^ 1]) + data[1:]
            return original(path, data)
        with patch.object(s, 'candidate', return_value=(self.candidate, {})), \
             patch.object(d, 'make_nvs', return_value=b'\xa5' * d.NVS_BYTES), \
             patch.object(d, 'write', side_effect=corrupt):
            self.assertRaisesRegex(ValueError, 'output readback', self.compose)
        self.assertFalse((self.root / 'out').exists())

    @unittest.skipUnless(GENERATOR, 'Set NVS_GENERATOR to official pinned IDF4.4.7 generator')
    def test_real_official_nvs_83_file_profile_readback_and_corruption(self):
        self.inputs(True, 83)
        with patch.object(s, 'candidate', return_value=(self.candidate, {})):
            self.compose()
        data = (self.root / 'out/nvs.bin').read_bytes()
        expected = {key: (self.root / 'owner/inputs' / (key + '.bin')).read_bytes()
                    for key in ('profile', 'descriptor', 'time')}
        d.verify_nvs(data, expected)
        self.assertGreater(len(expected['profile']), 4096)
        for offset in (8, 28, 64, 100, 4096 + 150):
            bad = bytearray(data); bad[offset] ^= 1
            self.assertRaises(ValueError, d.verify_nvs, bad, expected)
        expected['profile'] += b' '
        self.assertRaisesRegex(ValueError, 'readback', d.verify_nvs, data, expected)


if __name__ == '__main__':
    unittest.main()
