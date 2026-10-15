#!/usr/bin/env python3
"""Check native-bank preflight with one or more real ESP32-S3 bootloader files.

Example: python3 test/native_bank_bootloader_fixture_test.py path/to/bootloader.bin
Only host compiler sentinels run here; no target tools or device access.
"""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from release_assets import esp_image, file_bytes


def main(paths):
    if not paths:
        raise SystemExit('Pass at least one real ESP32-S3 bootloader fixture.')
    cases = 0
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        sentinel = root / 'compiler-called'
        compiler = root / 'cc'
        compiler.write_text('''#!/usr/bin/env bash
set -eu
if [[ -n "${BOOTLOADER_FILE:-}" ]];then
 [[ "$BOOTLOADER_FILE" != "$NATIVE_BANK_EXPECTED_FIXTURE" ]]
 cmp "$BOOTLOADER_FILE" "$NATIVE_BANK_EXPECTED_FIXTURE"
fi
printf called > "$NATIVE_BANK_COMPILER_SENTINEL"
exit 97
''')
        compiler.chmod(0o755)
        env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                   NATIVE_BANK_COMPILER_SENTINEL=str(sentinel))
        for name in ('BOOTLOADER_FILE', 'BOOT_BASELINE_REF', 'BOOT_PATH_ONLY', 'PRODUCT_STORE'):
            env.pop(name, None)

        def run(path, valid, options=None):
            nonlocal cases
            sentinel.unlink(missing_ok=True)
            settings = dict(env, NATIVE_BANK_EXPECTED_FIXTURE=str(path))
            if path is not None:
                settings['BOOTLOADER_FILE'] = str(path)
            settings.update(options or {})
            result = subprocess.run(['bash', str(ROOT / 'test/run_native_bank_test.sh')],
                                    cwd=ROOT, env=settings, text=True, capture_output=True)
            if valid:
                assert result.returncode == 97 and sentinel.exists(), result.stdout + result.stderr
                if path is not None:
                    data = path.read_bytes()
                    assert f'bytes={len(data)} sha256={hashlib.sha256(data).hexdigest()}' in result.stdout
            else:
                assert result.returncode != 0 and not sentinel.exists(), result.stdout + result.stderr
            cases += 1

        # Non-bootloader modes still work without an image or esptool invocation.
        run(None, True)
        for option in ({'BOOT_PATH_ONLY': '1'}, {'BOOT_BASELINE_REF': 'missing'}, {'PRODUCT_STORE': 'missing'}):
            run(None, False, option)
        for source in paths:
            data = file_bytes(Path(source))
            esp_image(data)
            assert len(data) <= 0x8000
            fixture = root / 'valid.bin'
            fixture.write_bytes(data)
            run(fixture, True)
            cases_by_name = {
                'empty': b'', 'wrong-magic': b'\x00' + data[1:],
                'truncated-header': data[:23], 'truncated-segment': data[:40],
                'truncated-digest': data[:-1], 'trailing-byte': data + b'\x00',
                'oversized': data + bytes(0x8001 - len(data)),
            }
            for name, offset in (('wrong-chip', 12), ('checksum', len(data) - 33),
                                 ('digest', len(data) - 1), ('segment-payload', 32)):
                changed = bytearray(data)
                changed[offset] ^= 1
                # Payload/checksum defects remain invalid even with a matching SHA.
                if name in ('checksum', 'segment-payload'):
                    changed[-32:] = hashlib.sha256(changed[:-32]).digest()
                cases_by_name[name] = changed
            changed = bytearray(data)
            struct.pack_into('<I', changed, 28, 0xfffffff0)
            changed[-32:] = hashlib.sha256(changed[:-32]).digest()
            cases_by_name['segment-length'] = changed
            for name, invalid in cases_by_name.items():
                fixture = root / (name + '.bin')
                fixture.write_bytes(invalid)
                run(fixture, False)
            run(root / 'missing.bin', False)
            link = root / 'symlink.bin'
            link.unlink(missing_ok=True)
            link.symlink_to(root / 'valid.bin')
            run(link, False)
            print(f'Native-bank preflight: {source} ({len(data)} bytes), valid snapshot and rejection cases PASS')
    print(f'Native-bank bootloader fixture validation: {cases} checks PASS')


if __name__ == '__main__':
    main(sys.argv[1:])
