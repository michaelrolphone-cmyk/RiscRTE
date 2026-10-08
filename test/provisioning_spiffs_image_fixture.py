#!/usr/bin/env python3
"""Dense public byte fixture for image storage tests; never a bootable product."""
import argparse
import hashlib
from pathlib import Path
import shutil
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from provision_profile import spiffs_charge


def make(output):
    if output.exists() or output.is_symlink():raise ValueError('output already exists')
    names = ['board.json', 'boot.json', 'default.elf'] + [f'{n:03}.bin' for n in range(88)]
    # Close to the accepted dense Watch footprint, above the streaming bound.
    total = 4_740_000
    sizes = [total // len(names) + (i < total % len(names)) for i in range(len(names))]
    charge, budget = spiffs_charge(sizes, 0x510000)
    assert charge > budget
    live = sum((n + 250) // 251 + 1 + (max(0, (n + 250) // 251 - 103) + 123) // 124 for n in sizes)
    assert live <= budget - 15
    output.mkdir()
    try:
        for name, size in zip(names, sizes):
            pattern = hashlib.sha256(('compact-image-fixture:' + name).encode()).digest()
            (output / name).write_bytes((pattern * ((size + 31) // 32))[:size])
    except BaseException:
        shutil.rmtree(output);raise
    print(f'Dense image fixture: {len(names)} files, {total} bytes, {live} live pages; streaming charge {charge}>{budget}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    make(parser.parse_args().output)
