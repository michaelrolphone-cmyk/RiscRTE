"""Coverage/routing and fail-closed input checks for the native CI replacement."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('prepare_spiffs', ROOT / 'test/prepare_native_spiffs.py')
spiffs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(spiffs)


class NativeCITests(unittest.TestCase):
    def test_integration_has_no_obsolete_target_build(self):
        workflow = (ROOT / '.github/workflows/integration.yml').read_text()
        target = workflow.split('  target:\n', 1)[1]
        for retired in ('pio run', 'platformio', 'framework-arduino', 'candidate.py', 'release_assets.py', 'v4.4.7'):
            self.assertNotIn(retired, target)
        self.assertIn('espressif/idf:v6.1@sha256:81893c71', target)
        self.assertIn('fff9895c82d744c7237be8847347bdd1b07c6643', target)
        self.assertIn('github.event.pull_request.head.sha || github.sha', target)
        self.assertIn('bash test/run_native_target_fixtures.sh elf', target)
        self.assertIn('bash test/run_native_target_fixtures.sh spiffs', target)
        # Independent fixtures no longer wait for the host shards.
        self.assertNotIn('needs: host', target)
        self.assertNotIn('continue-on-error', target)
        self.assertNotIn('--detach FETCH_HEAD --', target)
        for artifact in ('elf', 'native-spiffs-config'):
            self.assertIn('${{ env.FIXTURE_ROOT }}/build/' + artifact + '/', target)
        self.assertNotIn('${{ runner.temp }}/native-target-fixtures/build/', target)

    def test_shared_target_cases_are_retained(self):
        script = (ROOT / 'test/run_native_target_fixtures.sh').read_text()
        self.assertIn('xtensa-esp32s3-elf-gcc', script)
        self.assertIn('esp-15.2.0_20251204', script)
        self.assertIn('verify_legacy_runtime_header.py', script)
        self.assertIn('scripts/build_apps.py --cc "$CC"', script)
        for name in ('performance_loader', 'image_byte_cache'):
            self.assertIn(f'  bash test/run_{name}_test.sh\n', script)
            self.assertIn(f'  SANITIZE=1 bash test/run_{name}_test.sh\n', script)
        for check in ('run_elf_test.sh', 'high-fill-83 boundary-3 boundary-128',
                      'SANITIZE=1 bash test/run_provisioning_spiffs_test.sh',
                      'SANITIZE=1 bash test/run_provisioning_spiffs_image_test.sh',
                      'run_store_image_capacity_test.sh', 'cmp build/native-seed.bin'):
            self.assertIn(check, script)
        native = (ROOT / '.github/workflows/native-idf6.yml').read_text()
        self.assertIn('BOOT_PATH_FIRMWARE="$NATIVE_ROOT/build/riscrte_native.bin"', native)
        self.assertIn('--sdkconfig "$NATIVE_ROOT/build/config/sdkconfig.h"', native)
        self.assertIn('SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 bash test/run_native_bank_test.sh', native)
        self.assertIn("expected ELF32 little-endian ESP32-S3 fixture", script)

    def fixture(self, root):
        sdk = root / 'sdk'
        component = sdk / 'components/spiffs'
        sources, inputs = {}, {}
        for name in spiffs.SOURCE_BLOBS:
            data = ('source-' + name).encode()
            path = component / 'spiffs/src' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            sources[name] = hashlib.sha1(f'blob {len(data)}\0'.encode() + data).hexdigest()
        for name in spiffs.SDK_FILES:
            data = ('input-' + name).encode()
            path = component / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            inputs[name] = hashlib.sha256(data).hexdigest()
        return sdk, component, sources, inputs

    def test_sources_and_producer_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk, component, sources, inputs = self.fixture(root)
            with patch.object(spiffs, 'SOURCE_BLOBS', sources), patch.object(spiffs, 'SDK_FILES', inputs):
                spiffs.prepare(sdk, root / 'out')
                self.assertEqual((root / 'out/sdkconfig.h').read_bytes(),
                                 (ROOT / 'test/native_spiffs_sdkconfig.h').read_bytes())
                for path in [component / 'spiffs/src' / n for n in sources] + [component / n for n in inputs]:
                    original = path.read_bytes()
                    path.write_bytes(original + b'changed')
                    with self.assertRaisesRegex(ValueError, 'Wrong native SPIFFS'):
                        spiffs.prepare(sdk, root / 'rejected')
                    self.assertFalse((root / 'rejected').exists())
                    path.write_bytes(original)

    def test_generated_target_configuration_must_match(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk, _, sources, inputs = self.fixture(root)
            config = root / 'sdkconfig.h'
            fixture = (ROOT / 'test/native_spiffs_sdkconfig.h').read_text()
            config.write_text(fixture + '#define OTHER_SETTING 1\n')
            with patch.object(spiffs, 'SOURCE_BLOBS', sources), patch.object(spiffs, 'SDK_FILES', inputs):
                spiffs.prepare(sdk, root / 'out', sdkconfig=config)
                config.write_text(fixture.replace('CONFIG_SPIFFS_META_LENGTH 4', 'CONFIG_SPIFFS_META_LENGTH 8'))
                with self.assertRaisesRegex(ValueError, 'configuration differs'):
                    spiffs.prepare(sdk, root / 'rejected', sdkconfig=config)
                self.assertFalse((root / 'rejected').exists())


if __name__ == '__main__':
    unittest.main()
