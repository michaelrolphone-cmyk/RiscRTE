#!/usr/bin/env python3
"""Host regression coverage for controlled native CI input verification."""
import importlib.util
import json
import re
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


class NativeCiWorkflow(unittest.TestCase):
    def workflow(self):
        return (Path(__file__).resolve().parents[1] / '.github/workflows/native-idf6.yml').read_text()

    def test_publication_only_ignores_are_exact_and_bounded(self):
        workflow = self.workflow()
        ignored = workflow.split('    paths-ignore:\n', 1)[1].split('  workflow_dispatch:', 1)[0]
        paths = re.findall(r'^      - (.+)$', ignored, re.M)
        self.assertEqual(set(paths), {
            '.github/workflows/native-idf61-release.yml',
            'scripts/publish_native_idf61_release.py',
            'test/test_publish_native_idf61_release.py',
            'docs/native-idf61/runtime-0.4.0-release.json',
            'docs/native-idf61/RELEASE_TRANSPORT.md',
        })
        self.assertEqual(len(paths), 5)
        self.assertNotIn('.github/workflows/native-idf6.yml', paths)

    def test_generated_input_custody_and_release_routing_are_retained(self):
        workflow = self.workflow()
        self.assertIn('          python test/test_release_routing.py\n', workflow)
        self.assertIn('          python tests/test_native_idf61_candidate.py "${args[@]}"\n', workflow)
        self.assertIn('          python tests/test_native_idf61_provenance.py\n', workflow)
        self.assertIn('            python tests/test_native_display_archive.py\n', workflow)
        paths = workflow.split('          path: |\n', 1)[1]
        for name in ('project_elf_src_esp32s3.c', 'bootloader/project_elf_src_esp32s3.c',
                     'x509_crt_bundle.S', 'esp-idf/mbedtls/x509_crt_bundle',
                     'esp-idf/runtime/RiscBuildIdentity.h'):
            self.assertIn('            ${{ env.NATIVE_ROOT }}/build/' + name + '\n', paths)
        self.assertIn('            ${{ env.NATIVE_ROOT }}/build/x4-native-idf61/\n', paths)

    def test_current_fixture_source_set_and_historical_baselines(self):
        workflow = self.workflow()
        for revision in ('364bdd9ba58edd3f6fe1dc16d361db53d6de81af',
                         '86876d56d5b60cce494c922916e8f22fbc553f2a',
                         '41b8efb25a4cb4d3ab9cfac50ac625c11fab43a1'):
            self.assertIn('source: ' + revision, workflow)
        self.assertIn('IMAGE_HELPER_SOURCE: 364bdd9ba58edd3f6fe1dc16d361db53d6de81af', workflow)
        self.assertIn('python tests/test_current_native_fixture.py', workflow)
        for digest in ('0194fe7095edd50ee4e82b8077a9d154fddf5323143f3219f070bde952424370',
                       '868933ecf2cb468b8d487333fbc6150641035015b58b62c884024fc8d8fc06da',
                       '48145adc023a334885207100ce29596edef3f02c07a6d830ca53e3ab1fbf5993',
                       '2b6dce3efb468f3cd728b50707e0c9552d84e225b65e2b27e683d3d726bc6cef'):
            self.assertIn(digest, workflow)

    def test_product_contract_invocation_supplies_exact_inputs(self):
        workflow = self.workflow()
        step = workflow.split('      - name: Controlled product source and provenance contracts\n', 1)[1].split('      - name:', 1)[0]
        self.assertIn('args=(--runtime "$NATIVE_ROOT/sources/runtime" --image-helper "$IMAGE_HELPER_ROOT"\n', step)
        self.assertIn('            --sdk "$IDF_PATH" --baseline "$NATIVE_ROOT/inputs/released-factory.bin")\n', step)
        self.assertIn('          if [[ "$PRODUCT" == x4 ]]; then\n            args+=(--sd-archive "$NATIVE_ROOT/inputs/released-complete-sd.zip")\n          fi\n', step)
        self.assertIn('          python tests/test_native_idf61_candidate.py "${args[@]}"\n', step)


if __name__ == '__main__':
    unittest.main()
