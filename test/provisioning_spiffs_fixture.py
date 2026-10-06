#!/usr/bin/env python3
"""Create deterministic public SPIFFS capacity fixtures, without product files.

These arbitrary bytes are storage-test inputs, not valid boot graphs or ELFs.
The production Python page calculation chooses the boundaries; the C++ harness
checks the production native fits() function and refuses a one-byte overrun.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from provision_profile import spiffs_charge

PARTITION_BYTES = 0x510000


def exact_boundary(count):
    """Maximize one file, keeping the remaining files nonempty."""
    low, high = 1, 8 * 1024 * 1024
    while low < high:
        middle = (low + high + 1) // 2
        charged, budget = spiffs_charge([middle] + [1] * (count - 1), PARTITION_BYTES)
        if charged <= budget:
            low = middle
        else:
            high = middle - 1
    lengths = [low] + [1] * (count - 1)
    charged, budget = spiffs_charge(lengths, PARTITION_BYTES)
    over, _ = spiffs_charge([low + 1] + lengths[1:], PARTITION_BYTES)
    assert charged == budget and over > budget
    return lengths


def make(output):
    # Equal-sized high-fill objects exercise a different fragmentation pattern
    # from the exact-boundary fixtures' one large object plus tiny objects.
    total, count = 4_470_591, 83
    base, extra = divmod(total, count)
    fixtures = {
        'high-fill-83': [base + (i < extra) for i in range(count)],
        'boundary-3': exact_boundary(3),
        'boundary-128': exact_boundary(128),
    }
    if output.exists() or output.is_symlink():
        raise ValueError('output already exists')
    output.mkdir()
    report = {}
    try:
        for name, lengths in fixtures.items():
            charged, budget = spiffs_charge(lengths, PARTITION_BYTES)
            assert charged <= budget and 3 <= len(lengths) <= 128
            folder = output / name
            folder.mkdir()
            for index, size in enumerate(lengths):
                pattern = hashlib.sha256(f'riscrte-spiffs-fixture-v1:{name}:{index}'.encode()).digest()
                data = (pattern * ((size + len(pattern) - 1) // len(pattern)))[:size]
                (folder / f'{index:03d}.bin').write_bytes(data)
            report[name] = {
                'files': len(lengths), 'payload_bytes': sum(lengths),
                'charged_pages_including_digest': charged, 'available_pages': budget,
                'largest_file_bytes': max(lengths),
            }
    except BaseException:
        shutil.rmtree(output)
        raise
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='new directory for all three fixtures')
    args = parser.parse_args()
    print(json.dumps(make(args.output), sort_keys=True, indent=2))


if __name__ == '__main__':
    main()
