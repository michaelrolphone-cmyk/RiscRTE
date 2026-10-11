#!/usr/bin/env python3
"""Offline image/inventory custody; fixture bytes are not an executable product."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import provision_profile as p


def fixture(files, size=0x510000):
    raw = bytearray(b'\xff' * size)
    blocks = size // 4096
    for block in range(blocks):
        struct.pack_into('<H', raw, block * 4096 + 252, (0x20140529 ^ 256 ^ (blocks - block)) & 0xffff)
    next_page = 1

    def allocate(obj):
        nonlocal next_page
        if next_page % 16 == 0:
            next_page += 1
        page = next_page;next_page += 1
        struct.pack_into('<H', raw, page // 16 * 4096 + (page % 16 - 1) * 2, obj)
        return page

    for obj, (name, data) in enumerate(sorted(files.items()), 1):
        assert len(data) <= 251 * 103
        header = allocate(obj | 0x8000) * 256
        struct.pack_into('<HHB', raw, header, obj | 0x8000, 0, 0xf8)
        struct.pack_into('<IB', raw, header + 8, len(data), 1)
        name = ('/' + name).encode() + b'\0'
        raw[header + 13:header + 13 + len(name)] = name
        for span, offset in enumerate(range(0, len(data), 251)):
            page = allocate(obj);struct.pack_into('<HHB', raw, page * 256, obj, span, 0xfc)
            chunk = data[offset:offset + 251];raw[page * 256 + 5:page * 256 + 5 + len(chunk)] = chunk
            struct.pack_into('<H', raw, header + 49 + span * 2, page)
    return bytes(raw)


class ImageInventory(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name);self.store = self.root / 'store';self.store.mkdir()
        self.files = {'board.json': b'{"fixture":"board"}', 'boot.json': b'{"fixture":"boot"}',
                      'default.elf': b'fixture-only-not-target-code' * 128}
        for name, data in self.files.items():
            (self.store / name).write_bytes(data)
        self.image = self.root / 'store.bin';self.image.write_bytes(fixture(self.files))

    def build(self, name='out'):
        return p.build_image_inventory(self.store, self.image, 'riscrte-paired-appdata-v2',
                                       'esp32s3-16mb-appdata-iq', 'https://example.test/fixture/image.bin', self.root / name)

    def test_frozen_image_exact_inventory_and_repeat(self):
        record = self.build();self.build('again')
        self.assertEqual(record['schema_version'], 2)
        self.assertEqual(record['image']['sha256'], p.sha(self.image.read_bytes()))
        self.assertEqual({v['path'] for v in record['files']}, set(self.files))
        self.assertTrue(all(set(v) == {'path', 'bytes', 'sha256'} for v in record['files']))
        for path in (self.root / 'out').iterdir():
            self.assertEqual(path.read_bytes(), (self.root / 'again' / path.name).read_bytes())
        self.assertRaisesRegex(ValueError, 'already exists', self.build)

    def test_store_mismatch_missing_extra_and_image_corruption(self):
        for name, data in [('board.json', b'changed'), ('unexpected', b'extra')]:
            (self.store / name).write_bytes(data)
            self.assertRaisesRegex(ValueError, 'image and complete', self.build)
            (self.store / name).unlink()
            if name in self.files:(self.store / name).write_bytes(self.files[name])
        raw = bytearray(self.image.read_bytes());raw[252] ^= 1;self.image.write_bytes(raw)
        self.assertRaisesRegex(ValueError, 'magic', self.build)
        self.assertFalse((self.root / 'out').exists())

    def test_symlink_and_wrong_partition(self):
        self.image.unlink();self.image.symlink_to(self.store / 'board.json')
        self.assertRaisesRegex(ValueError, 'symlink', self.build)
        self.image.unlink();self.image.write_bytes(fixture(self.files, 0x4f0000))
        self.assertRaisesRegex(ValueError, 'length mismatch', self.build)

    def test_interruption_cleanup_and_retry(self):
        original = p.write
        def interrupted(path, data):
            if path.name == 'inventory.json':raise KeyboardInterrupt()
            original(path, data)
        with patch.object(p, 'write', side_effect=interrupted):
            self.assertRaises(KeyboardInterrupt, self.build)
        self.assertFalse((self.root / 'out').exists())
        self.build()


if __name__ == '__main__':
    unittest.main()
