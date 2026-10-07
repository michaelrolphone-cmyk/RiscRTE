#!/usr/bin/env python3
"""Offline full-inventory/private-profile checks with the production C++ parser."""
import json
import os
import signal
import time
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import provision_profile as p

ROOT = Path(__file__).resolve().parents[1]
BASE = 'https://packages.example.invalid/explicit-owner-published-release/'
LAYOUT = 'riscrte-paired-appdata-v2'
TARGET = 'esp32s3-16mb-appdata-iq'


class Profiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tools = tempfile.TemporaryDirectory()
        cls.validator = Path(cls.tools.name) / 'provision-input'
        subprocess.run(['bash', str(ROOT / 'scripts/build_provision_input_tool.sh'), str(cls.validator)], check=True)
        probe = Path(cls.tools.name) / 'capacity.cpp'
        probe.write_text('#include "runtime/provisioning/SpiffsCapacity.h"\n#include <cstdio>\n#include <cstdlib>\nint main(int argc,char** argv){for(int i=1;i<argc;++i)std::printf("%llu\\n",static_cast<unsigned long long>(RiscProvision::SpiffsCapacity::filePages(std::strtoul(argv[i],nullptr,10))));}\n')
        cls.capacity = Path(cls.tools.name) / 'capacity'
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'src'),
                        str(probe), '-o', str(cls.capacity)], check=True)


    @classmethod
    def tearDownClass(cls):
        cls.tools.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.store = self.root / 'store'; self.store.mkdir()
        for name in ('boot.json', 'board.json', 'default.elf'):
            (self.store / name).write_bytes(b'fixture-only-not-native-admission')
        self.wifi = self.root / 'wifi.json'
        self.wifi.write_text(json.dumps({'ssid': 'dummy-network', 'password': 'dummy-secret-only'}))
        self.inventory = self.root / 'inventory.json'

    def pin(self, count=3, base=BASE):
        for n in range(count - 3):
            (self.store / f'app{n:03}.elf').write_bytes(b'explicit-test-file-' + str(n).encode())
        result = p.pin_store(self.store, LAYOUT, base, target=TARGET)
        self.inventory.write_bytes(p.encode(result))
        return result

    def build(self, name='owner', base=BASE):
        return p.build_profile(self.inventory, self.store, self.wifi, self.validator,
                               'time.example.invalid', self.root / name, base)

    def test_full_83_file_store_v2_production_parser_determinism(self):
        record = self.pin(83)
        result = self.build()
        self.assertEqual(result['file_count'], 83)
        profile = p.decode((self.root / 'owner/profile.json').read_bytes())
        self.assertEqual(profile['schema_version'], 2)
        self.assertEqual(len(profile['files']), 83)
        self.assertNotIn('url', profile['files'][0])
        board = next(x for x in profile['files'] if x['path'] == 'board.json')
        self.assertEqual(board['sha256'], p.sha((self.store / 'board.json').read_bytes()))
        self.assertLess(result['profile_bytes'], 16384)
        self.assertEqual((self.root / 'owner').stat().st_mode & 0o777, 0o700)
        self.assertEqual((self.root / 'owner/profile.json').stat().st_mode & 0o777, 0o600)
        self.build('again')
        for path in (self.root / 'owner').rglob('*'):
            if path.is_file():
                self.assertEqual(path.read_bytes(), (self.root / 'again' / path.relative_to(self.root / 'owner')).read_bytes())
        self.assertEqual({e['path'] for e in record['files']}, {e['path'] for e in profile['files']})

    def test_legacy_schema_and_distinct_sources(self):
        record = self.pin()
        record['files'][0]['url'] = 'https://other.example.invalid/pinned/' + record['files'][0]['path']
        self.inventory.write_bytes(p.encode(record))
        self.build(base=None)
        self.assertEqual(p.decode((self.root / 'owner/profile.json').read_bytes())['schema_version'], 1)
        with self.assertRaisesRegex(ValueError, 'base URL differs'):
            self.build('bad')
        self.assertFalse((self.root / 'bad').exists())

    def test_mismatch_missing_unexpected_and_symlink(self):
        self.pin()
        (self.store / 'default.elf').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'pinned store bytes'):
            self.build()
        self.pin()
        (self.store / 'extra.elf').write_bytes(b'extra')
        with self.assertRaisesRegex(ValueError, 'full store inventory'):
            self.build()
        (self.store / 'extra.elf').unlink(); (self.store / 'default.elf').unlink()
        with self.assertRaisesRegex(ValueError, 'complete boot.json'):
            self.build()
        (self.store / 'default.elf').symlink_to(self.wifi)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            self.build()
        self.assertFalse((self.root / 'owner').exists())

    def test_limits_urls_unknown_keys(self):
        record = self.pin()
        for change in ({'bytes': True}, {'bytes': 0}, {'sha256': 'A' * 64},
                       {'path': '../escape'}, {'path': 'x' * 31},
                       {'url': 'http://example.invalid/default.elf'},
                       {'url': BASE + 'default.elf?token=private'}, {'unexpected': 1}):
            copy = json.loads(json.dumps(record)); copy['files'][0].update(change)
            with self.assertRaises(ValueError):
                p.validate_inventory(copy)
        self.assertRaises(ValueError, p.decode, b'{"a":1,"a":2}')
        record = self.pin(128)
        self.assertRaisesRegex(ValueError, '16 KiB', p.profile_bytes, record,
                               {'ssid': 'dummy', 'password': ''}, None)
        record['files'].append(record['files'][0])
        self.assertRaisesRegex(ValueError, 'count', p.validate_inventory, record)

    def test_no_overwrite_private_location_and_interruption_cleanup(self):
        self.pin(); self.build()
        before = (self.root / 'owner/profile.json').read_bytes()
        self.assertRaisesRegex(ValueError, 'already exists', self.build)
        (self.root / 'alias').symlink_to(self.root / 'owner', target_is_directory=True)
        self.assertRaisesRegex(ValueError, 'symlink', self.build, 'alias')
        self.assertRaisesRegex(ValueError, 'outside Git', p.destination, ROOT / 'private-forbidden', True)
        self.assertEqual((self.root / 'owner/profile.json').read_bytes(), before)
        with patch.object(p, 'run_validator', side_effect=KeyboardInterrupt):
            self.assertRaises(KeyboardInterrupt, self.build, 'interrupted')
        self.assertFalse((self.root / 'interrupted').exists())
        with patch.object(p, 'run_validator', side_effect=OSError('dummy-secret-only')):
            self.assertRaises(OSError, self.build, 'failed')
        self.assertFalse((self.root / 'failed').exists())

    def test_spiffs_page_accounting_matches_pinned_geometry(self):
        # Each expectation includes the three-page charge for the digest file.
        for size, pages in ((1, 3), (251, 3), (252, 4), (8192, 35),
                            (8193, 36), (251 * 103, 108), (251 * 104, 111)):
            self.assertEqual(p.spiffs_charge([size], 0x510000), (pages + 3, 19380))
        for invalid in (4096, 16384, 0x510001, True):
            self.assertRaises(ValueError, p.spiffs_charge, [1], invalid)
        self.assertRaises(ValueError, p.spiffs_charge, [0], 0x510000)
        record = self.pin()
        lo, hi = 1, 8 * 1024 * 1024
        while lo < hi:
            middle = (lo + hi + 1) // 2
            charge, available = p.spiffs_charge([middle, 1, 1], 0x510000)
            if charge <= available:
                lo = middle
            else:
                hi = middle - 1
        for item in record['files']:
            item['bytes'] = 1
        record['files'][0]['bytes'] = lo
        p.validate_inventory(record)
        record['files'][0]['bytes'] += 1
        self.assertRaisesRegex(ValueError, 'capacity', p.validate_inventory, record)


    @unittest.skipUnless(os.environ.get('PRODUCT_STORE'), 'Set PRODUCT_STORE to an independently admitted complete product store')
    def test_real_full_product_store_inventory_and_profile(self):
        source = Path(os.environ['PRODUCT_STORE'])
        record = p.build_inventory(source, LAYOUT, self.root / 'distribution', BASE, target=TARGET)
        expected = p.store_files(source)
        self.assertEqual({item['path'] for item in record['files']}, set(expected))
        self.assertGreaterEqual(len(record['files']), 80)
        result = p.build_profile(self.root / 'distribution/inventory.json', self.root / 'distribution/files',
                                 self.wifi, self.validator, 'time.example.invalid', self.root / 'real-owner', BASE)
        self.assertEqual(result['file_count'], len(expected))
        self.assertLessEqual(result['profile_bytes'], 16384)
        charged, available = p.spiffs_charge([len(data) for data in expected.values()], p.LAYOUTS[LAYOUT])
        self.assertLessEqual(charged, available)
        self.assertEqual(result['runtime_target'], TARGET)

    def test_capacity_formula_matches_production_header(self):
        sizes = [1, 250, 251, 252, 8191, 8192, 8193, 251 * 103,
                 251 * 104, 251 * 227, 251 * 228, 8 * 1024 * 1024]
        sizes += [n * 7919 for n in range(1, 129)]
        native = subprocess.check_output([str(self.capacity), *map(str, sizes)], text=True).splitlines()
        digest_charge = p.spiffs_charge([], 0x510000)[0]
        self.assertEqual([int(value) for value in native],
                         [p.spiffs_charge([size], 0x510000)[0] - digest_charge for size in sizes])

    def test_time_server_is_mandatory_and_validated(self):
        self.pin()
        for server in (None, '', 'https://invalid', 'a' * 64):
            with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                p.build_profile(self.inventory, self.store, self.wifi, self.validator,
                                server, self.root / 'bad-time', BASE)
            self.assertFalse((self.root / 'bad-time').exists())

    def test_source_map_inventory_and_capacity(self):
        record = self.pin()
        sources = {entry['path']: entry['url'] for entry in record['files']}
        self.assertEqual(p.pin_store(self.store, LAYOUT, sources=sources, target=TARGET), record)
        sources.pop('default.elf')
        self.assertRaisesRegex(ValueError, 'full source map', p.pin_store, self.store, LAYOUT, None, sources, TARGET)
        record['files'][0]['bytes'] = 8 * 1024 * 1024
        self.assertRaisesRegex(ValueError, 'capacity', p.validate_inventory, record)

    def test_distributable_inventory_snapshot_and_cleanup(self):
        record = self.pin(83)
        result = p.build_inventory(self.store, LAYOUT, self.root / 'public', BASE, target=TARGET)
        self.assertEqual(result, record)
        self.assertTrue((self.root / 'public/COMPLETE').is_file())
        self.assertNotIn('wifi', p.decode((self.root / 'public/inventory.json').read_bytes()))
        for item in record['files']:
            self.assertEqual((self.root / 'public/files' / item['path']).read_bytes(),
                             (self.store / item['path']).read_bytes())
        with patch.object(p, 'write', side_effect=KeyboardInterrupt):
            self.assertRaises(KeyboardInterrupt, p.build_inventory, self.store, LAYOUT, self.root / 'incomplete', BASE, None, TARGET)
        self.assertFalse((self.root / 'incomplete').exists())
        self.assertRaisesRegex(ValueError, 'already exists', p.build_inventory,
                               self.store, LAYOUT, self.root / 'public', BASE, None, TARGET)

    def test_real_process_interrupt_removes_partial_private_output(self):
        self.pin()
        slow = self.root / 'slow-validator'
        slow.write_text('#!/usr/bin/env python3\nimport pathlib,sys,time\npathlib.Path(sys.argv[2]).mkdir()\ntime.sleep(20)\n')
        slow.chmod(0o700)
        args = [sys.executable, str(ROOT / 'scripts/provision_profile.py'), 'profile',
                '--inventory', str(self.inventory), '--store', str(self.store),
                '--wifi-file', str(self.wifi), '--validator', str(slow),
                '--output', str(self.root / 'interrupted'), '--time-server', 'time.example.invalid']
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 5
            while not (self.root / 'interrupted/inputs').exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertTrue((self.root / 'interrupted/inputs').is_dir())
            process.send_signal(signal.SIGINT)
            stdout, stderr = process.communicate(timeout=5)
            self.assertNotEqual(process.returncode, 0)
            self.assertFalse((self.root / 'interrupted').exists())
            self.assertNotIn(b'dummy-secret-only', stdout + stderr)
        finally:
            if process.poll() is None:
                process.kill(); process.communicate()

    def test_published_inventory_needs_no_local_payload_download(self):
        self.pin()
        local = self.build()
        remote = p.build_profile(self.inventory, None, self.wifi, self.validator,
                                 'time.example.invalid', self.root / 'inventory-only', BASE)
        self.assertEqual(local['profile_sha256'], remote['profile_sha256'])
        self.assertEqual((self.root / 'owner/profile.json').read_bytes(),
                         (self.root / 'inventory-only/profile.json').read_bytes())

    def test_private_location_refuses_ambiguous_git_detection(self):
        output = self.root / 'ambiguous-private'
        with patch.dict(os.environ, {'GIT_DIR': str(self.root / 'missing.git')}):
            self.assertRaisesRegex(ValueError, 'ambiguous Git', p.destination, output, True)
        for message in (b'fatal: detected dubious ownership', b'fatal: permission denied', b''):
            result = subprocess.CompletedProcess([], 128, b'', message)
            with patch.object(p.subprocess, 'run', return_value=result):
                self.assertRaisesRegex(ValueError, 'cannot verify', p.destination, output, True)
        broken = self.root / 'broken-repository'; broken.mkdir(); (broken / '.git').write_text('broken')
        with patch.dict(os.environ, {'GIT_DIR': str(self.root / 'missing.git')}):
            self.assertRaisesRegex(ValueError, 'outside Git', p.destination, broken / 'private', True)
        self.assertFalse(output.exists())

    def test_cli_fixed_failure_diagnostic(self):
        self.pin(); self.wifi.write_text('{"ssid":"dummy-secret-only","password":"short"}')
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/provision_profile.py'), 'profile',
                                 '--inventory', str(self.inventory), '--store', str(self.store),
                                 '--wifi-file', str(self.wifi), '--validator', str(self.validator),
                                 '--output', str(self.root / 'failed'), '--time-server', 'time.example.invalid'], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn(b'dummy-secret-only', result.stdout + result.stderr)
        self.assertFalse((self.root / 'failed').exists())


if __name__ == '__main__':
    unittest.main()
