#!/usr/bin/env python3
"""Host regression coverage for controlled native CI input verification."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('native_ci', Path(__file__).resolve().parents[1] / 'scripts/native_idf61_ci.py')
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)


class NativeCiInputs(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def repository(self, path):
        path.mkdir()
        def run(*args):
            return subprocess.check_output(['git', '-C', str(path), *args], stderr=subprocess.DEVNULL, text=True).strip()
        run('init', '-q')
        (path / 'tracked').write_text('pinned\n')
        run('add', 'tracked')
        run('-c', 'user.name=CI Test', '-c', 'user.email=ci@example.invalid', 'commit', '-qm', 'fixture')
        return run('rev-parse', 'HEAD')

    def test_exact_clean_checkout_and_alias(self):
        path = self.root / 'product'
        revision = self.repository(path)
        report = ci.record_sources([f'product={path}@{revision}', f'image_helper={path}@{revision}'])
        self.assertEqual(report['sources']['product']['revision'], revision)
        self.assertTrue(report['sources']['product']['clean'])

    def test_dirty_untracked_wrong_pin_and_duplicate_rejected(self):
        path = self.root / 'runtime'
        revision = self.repository(path)
        with self.assertRaisesRegex(ValueError, 'HEAD differs'):
            ci.source_record(path, '0' * 40)
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            ci.record_sources([f'runtime={path}@{revision}'] * 2)
        (path / 'tracked').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'dirty'):
            ci.source_record(path, revision)
        (path / 'tracked').write_text('pinned\n')
        (path / 'untracked').write_text('unexpected')
        with self.assertRaisesRegex(ValueError, 'dirty'):
            ci.source_record(path, revision)

    def test_different_source_owner_rejected(self):
        path = self.root / 'runtime'
        revision = self.repository(path)
        with mock.patch.object(ci.os, 'getuid', return_value=path.stat().st_uid + 1):
            with self.assertRaisesRegex(ValueError, 'owned by this shell user'):
                ci.source_record(path, revision)

    def test_nested_repositories_rejected(self):
        path = self.root / 'runtime'
        revision = self.repository(path)
        # Git ignore must not make a nested repo acceptable as a source root.
        (path / '.git/info/exclude').write_text('nested/\n')
        nested = path / 'nested'
        nested_revision = self.repository(nested)
        with self.assertRaisesRegex(ValueError, 'nested'):
            ci.record_sources([f'runtime={path}@{revision}', f'product={nested}@{nested_revision}'])

    def test_uninitialized_or_moved_submodule_rejected(self):
        path = self.root / 'runtime'
        revision = self.repository(path)
        actual = subprocess.check_output
        for marker in ('-', '+', 'U'):
            def output(command, *args, **kwargs):
                if command[-3:] == ['submodule', 'status', '--recursive']:
                    return marker + '1' * 40 + ' vendor/module\n'
                return actual(command, *args, **kwargs)
            with mock.patch.object(ci.subprocess, 'check_output', side_effect=output):
                with self.assertRaisesRegex(ValueError, 'Submodule'):
                    ci.source_record(path, revision)

    def fixture(self, product):
        factory = self.root / 'factory.bin'
        factory.write_bytes(b'exact released factory')
        sd = self.root / 'complete-sd.zip'
        sd.write_bytes(b'exact complete SD archive')
        contract = self.root / 'contract.json'
        contract.write_text(json.dumps({'product': product, 'factory_bytes': factory.stat().st_size,
                                       'factory_sha256': ci.digest(factory)['sha256'], 'sd': ci.digest(sd)}))
        args = [product, contract, factory, 'https://github.com/owner/repo/releases/download/tag/factory.bin',
                ci.digest(factory)['sha256']]
        return args, sd

    def test_exact_factory_and_failure_modes(self):
        args, _ = self.fixture('twatch')
        self.assertEqual(ci.verify_baselines(*args)['factory']['sha256'], args[-1])
        with self.assertRaisesRegex(ValueError, 'another product'):
            ci.verify_baselines('waveshare', *args[1:])
        with self.assertRaisesRegex(ValueError, 'Workflow factory digest'):
            ci.verify_baselines(*args[:-1], '0' * 64)
        args[2].write_bytes(b'tampered factory')
        with self.assertRaisesRegex(ValueError, 'factory bytes'):
            ci.verify_baselines(*args)

    def test_complete_x4_sd_required_and_hashed(self):
        args, sd = self.fixture('x4')
        with self.assertRaisesRegex(ValueError, 'Complete X4'):
            ci.verify_baselines(*args)
        extra = [sd, 'https://github.com/owner/repo/releases/download/tag/sd.zip', ci.digest(sd)['sha256']]
        self.assertIn('sd_archive', ci.verify_baselines(*args, *extra))
        with self.assertRaisesRegex(ValueError, 'Workflow SD digest'):
            ci.verify_baselines(*args, *extra[:-1], '0' * 64)
        sd.write_bytes(b'partial SD archive')
        with self.assertRaisesRegex(ValueError, 'archive bytes'):
            ci.verify_baselines(*args, *extra)


if __name__ == '__main__':
    unittest.main()
