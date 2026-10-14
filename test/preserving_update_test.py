#!/usr/bin/env python3
"""Offline fault/preservation qualification against exact published baselines."""
import argparse
import json
from pathlib import Path
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from prepare_preserving_update import plan, layout, pair, selection, ota, sha, record


def active(image):
    entries = [row[0] for i in range(2) if (row := ota(image[0xff0000+i*4096:0xff0020+i*4096]))]
    return (max(entries)-1) % 2


def reject(snapshot, baseline, candidate, digest):
    try:
        plan(snapshot, baseline, candidate, digest)
    except ValueError:
        return
    raise AssertionError('Invalid update accepted')


def test(row):
    baseline = Path(row['path']).read_bytes()
    assert sha(baseline) == row['sha256']
    geometry = layout(baseline)
    expanded, native, stores, size = geometry
    _, firmware, store = pair(baseline, 0, geometry)
    # A different synthetic candidate proves the copy, not an identity rewrite.
    # It is a storage model only, never an executable delivery.
    changed = bytes([store[0] ^ 1]) + store[1:]
    candidate = bytearray(baseline)
    candidate[stores[0]:stores[0]+size] = changed
    candidate[0xff2000:0xff2060] = record(0, firmware, changed, True, expanded)
    results = []
    for source in (0, 1):
        snapshot = bytearray(baseline)
        snapshot[0x9000:0xf000] = bytes((i*17+3)%256 for i in range(0x6000))
        snapshot[0x270000:0x2f0000] = bytes((i*23+7)%256 for i in range(0x80000))
        if source:
            snapshot[native[1]:native[1]+len(firmware)] = firmware
            snapshot[stores[1]:stores[1]+size] = store
            snapshot[0xff3000:0xff3060] = record(1, firmware, store, True, expanded)
            snapshot[0xff1000:0xff2000] = selection(2, 2)
        writes, receipt = plan(snapshot, baseline, candidate, row['sha256'])
        staged = bytearray(snapshot)
        sectors = 0
        for _, off, data in writes[:-1]:
            end = off+(len(data)+4095)//4096*4096
            for sector in range(off, end, 4096):
                assert all(not (sector < stop and sector+4096 > start) for start, stop in receipt['preserved_ranges'])
                sectors += 1
            staged[off:end] = b'\xff'*(end-off)
            staged[off:off+len(data)] = data
            assert active(staged) == source
        for start, end in receipt['preserved_ranges']:
            assert staged[start:end] == snapshot[start:end]
        _, off, data = writes[-1]
        for cut in range(33):
            interrupted = bytearray(staged)
            interrupted[off:off+4096] = b'\xff'*4096
            interrupted[off:off+cut] = data[:cut]
            assert active(interrupted) == (1-source if cut == 32 else source)
        staged[off:off+4096] = data
        assert active(staged) == 1-source
        assert pair(staged, 1-source, geometry)[1:] == (firmware, changed)
        for start, end in receipt['preserved_ranges']:
            assert staged[start:end] == snapshot[start:end]
        staged[off+24:off+28] = struct.pack('<I', 1)
        assert active(staged) == 1-source
        staged[off+24:off+28] = struct.pack('<I', 4)
        assert active(staged) == source
        for offset in (0, 0x8000, native[source], stores[source], 0xff2000+source*4096):
            bad = bytearray(snapshot); bad[offset] ^= 1
            reject(bad, baseline, candidate, row['sha256'])
        for offset in (0, 0x8000, native[0], stores[0], 0xff2000):
            bad = bytearray(candidate); bad[offset] ^= 1
            reject(snapshot, baseline, bad, row['sha256'])
        for state in (0, 1):
            bad = bytearray(snapshot); bad[0xff0000+source*4096:0xff1000+source*4096] = selection(source+1, state)
            reject(bad, baseline, candidate, row['sha256'])
        reject(snapshot, baseline, candidate, '0'*64)
        reject(snapshot[:-1], baseline, candidate, row['sha256'])
        results.append(dict(source_bank=source, sector_boundaries=sectors, selection_cuts=33, preservation=True,
                            candidate_integrity=True, invalid_inputs_rejected=True, rollback_model=True))
    return dict(baseline_sha256=row['sha256'], store_abi=3 if expanded else 2, cases=results)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    results = [test(row) for row in json.loads(args.catalog.read_text())]
    args.output.write_text(json.dumps(dict(hardware_tested=False, synthetic_candidate=True, results=results), indent=2)+'\n')
    print('Preserving update: %d exact baselines, both active banks, sector bounds, 33 selection cuts, corruption refusal and rollback model PASS' % len(results))
