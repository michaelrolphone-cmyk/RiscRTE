#!/usr/bin/env python3
"""Offline policy generator trust/path checks; optional exact pinned ELF check."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import generate_usb_controller_policy as policy


class ControllerPolicyTests(unittest.TestCase):
    def test_selected_path(self):
        for path in ('driver.elf', 'drivers/usb-controller-esp32s3/driver.elf'):
            self.assertEqual(policy.relative_path(path), path)
        for path in ('', '/driver.elf', '../driver.elf', 'a/../driver.elf', './driver.elf',
                     'a//driver.elf', 'x\\driver.elf', 'x:driver.elf', 'a.json', '.x.elf',
                     'x..elf', 'x' * 128 + '.elf', 'x".elf', 'x\n.elf'):
            with self.assertRaises(ValueError, msg=path):
                policy.relative_path(path)

    def test_unpinned_input_never_generates(self):
        for content in (b'', b'{}', b'\x7fELF', b'{"privileged-imports.v1":["printf"]}'):
            with self.assertRaises(ValueError):
                policy.render(content, 'driver.elf')

    @unittest.skipUnless(os.environ.get('CONTROLLER_ELF'), 'exact controller input not supplied')
    def test_exact_frozen_image_and_check_mode(self):
        image = Path(os.environ['CONTROLLER_ELF'])
        data = image.read_bytes()
        generated = policy.render(data, 'driver.elf')
        self.assertEqual(generated, policy.render(data, 'driver.elf'))
        self.assertEqual(len(policy.inspect_controller(data)), 47)
        self.assertIn(policy.PINNED_SHA256, generated)
        for field in ('"board.power.vbus"', '"platform.usb.phy.resource"', 'imports,47,requirements,2,1'):
            self.assertIn(field, generated)
        for offset in (0, len(data) // 2, len(data) - 1):
            changed = bytearray(data)
            changed[offset] ^= 1
            with self.assertRaises(ValueError):
                policy.render(bytes(changed), 'driver.elf')
        with tempfile.TemporaryDirectory() as temporary:
            header = Path(temporary) / 'policy.h'
            command = [sys.executable, policy.__file__, '--controller', str(image), '--relative-elf-path', 'driver.elf']
            subprocess.run(command + ['--output', str(header)], check=True, capture_output=True)
            subprocess.run(command + ['--check', str(header)], check=True, capture_output=True)
            header.write_text(generated + '\n// changed\n')
            self.assertNotEqual(subprocess.run(command + ['--check', str(header)], capture_output=True).returncode, 0)


if __name__ == '__main__':
    unittest.main()
