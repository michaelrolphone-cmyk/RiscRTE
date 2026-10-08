#!/usr/bin/env python3
"""Compare native/Python admission on the same deterministic raw images."""

import importlib.util
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

spec = importlib.util.spec_from_file_location(
    'store_image_capacity', Path(__file__).resolve().parents[1] / 'scripts/store_image_capacity.py')
capacity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capacity)


def empty(blocks=8):
    image = bytearray(b'\xff' * (blocks * 4096))
    for block in range(blocks):
        struct.pack_into('<H', image, block * 4096 + 252,
                         (0x20140529 ^ 256 ^ (blocks - block)) & 0xffff)
    return image


def occupy(image, block, entry, object_id=1):
    struct.pack_into('<H', image, block * 4096 + entry * 2, object_id)
    image[block * 4096 + (entry + 1) * 256] = 0x69


def run(native, external):
    fixtures = [empty(), bytearray(), empty()[:-1], empty(4), bytearray(b'\xff' * 32768)]
    exact = empty()
    for block in range(4):
        for entry in range(15):
            occupy(exact, block, entry, 0 if entry % 2 else 0x8001)
    assert capacity.inspect(exact) == dict(occupied_pages=60, deleted_pages=28,
                                           free_pages=60, free_blocks=4)
    fixtures.append(exact)
    over = exact.copy(); occupy(over, 4, 0, 0); fixtures.append(over)
    scattered = empty()
    for block in range(5):
        occupy(scattered, block, 0)
    fixtures.append(scattered)
    for offset in (252, 7 * 4096 + 252, 30, 4 * 4096 + 256, 32767):
        corrupt = exact.copy(); corrupt[offset] ^= 1; fixtures.append(corrupt)
    hole = empty(); occupy(hole, 0, 1); fixtures.append(hole)
    counters = exact.copy()
    for block in range(8):
        struct.pack_into('<H', counters, block * 4096 + 254, block * 123)
    fixtures.append(counters)
    randomizer = random.Random(0x5f1ff5)
    for _ in range(100):
        blocks = randomizer.randrange(5, 30)
        image = empty(blocks)
        # Some cases have exactly four spare blocks, others distribute pages
        # throughout the image. The page budget alone must not admit the latter.
        used_blocks = blocks - 4 if randomizer.randrange(2) else blocks
        for block in range(used_blocks):
            for entry in range(randomizer.randrange(16)):
                occupy(image, block, entry, randomizer.choice([0, 1, 0x8001, 0x7fff]))
        fixtures.append(image)
    if external:
        raw = Path(external[0]).read_bytes()
        offset = int(external[1], 0) if len(external) == 3 else 0
        length = int(external[2], 0) if len(external) == 3 else len(raw)
        real = bytearray(raw[offset:offset + length])
        assert len(real) == length
        assert capacity.inspect_image(real, length)
        fixtures.append(real)
        corrupt = real.copy(); corrupt[-4096 + 252] ^= 1; fixtures.append(corrupt)
        fixtures.append(real[:-1])
    with tempfile.TemporaryDirectory() as directory:
        for index, image in enumerate(fixtures):
            path = Path(directory) / 'image.bin'
            path.write_bytes(image)
            try:
                expected = capacity.inspect(image)
            except ValueError:
                expected = None
            result = subprocess.run([native, '--inspect', str(path)], capture_output=True, text=True)
            assert result.returncode in (0, 1), (index, result.returncode, result.stderr)
            assert (result.returncode == 0) == (expected is not None), (index, expected, result.stdout)
            if expected is not None:
                assert json.loads(result.stdout) == expected, index
    calls = []
    def reader(offset, size):
        assert offset == len(calls) * 4096 and size == 4096
        calls.append(offset)
        return exact[offset:offset + size]
    assert capacity.inspect_reader(len(exact), reader) == capacity.inspect(exact)
    assert len(calls) == 8
    for image in (bytes(exact), exact, memoryview(exact)):
        assert capacity.inspect_image(image, len(exact)) == capacity.inspect(exact)
    for image, size in ((True, len(exact)), (123, len(exact)), ('text', len(exact)),
                        ([255] * len(exact), len(exact)), (exact, True),
                        (exact, 32768.0), (exact[:-1], len(exact)),
                        (exact + b'\xff', len(exact))):
        try:
            capacity.inspect_image(image, size)
        except ValueError:
            pass
        else:
            raise AssertionError(('invalid image wrapper input accepted', type(image), size))
    for size in (0, 4096, 4 * 4096, 32769, 0x1000000, 0xffffffff, True, 32768.0):
        try:
            capacity.inspect_reader(size, lambda *_: (_ for _ in ()).throw(AssertionError('must not read')))
        except ValueError:
            pass
        else:
            raise AssertionError(('geometry accepted', size))
    for short in (b'', bytes(4095), bytes(4097), None):
        try:
            capacity.inspect_reader(len(exact), lambda *_: short)
        except ValueError:
            pass
        else:
            raise AssertionError('short or malformed read accepted')
    def broken(*_):
        raise OSError('injected read failure')
    try:
        capacity.inspect_reader(len(exact), broken)
    except ValueError:
        pass
    else:
        raise AssertionError('read failure accepted')
    print(f'PASS: Python/native parity on {len(fixtures)} shared images; Python geometry, callback, and short-read failures')


if __name__ == '__main__':
    assert len(sys.argv) in (2, 3, 5)
    run(sys.argv[1], sys.argv[2:])
