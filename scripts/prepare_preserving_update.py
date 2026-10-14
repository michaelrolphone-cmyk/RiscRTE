#!/usr/bin/env python3
"""Prepare an offline, readback-bound inactive-bank update. Never accesses a device.

The baseline digest must come from the verified installed release. A fresh full
flash readback is required. Source and target must use the same paired layout.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from paired_bank_images import parse_record, record


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def ota(data):
    seq = struct.unpack_from('<I', data)[0]
    state, crc = struct.unpack_from('<II', data, 24)
    if seq == 0xffffffff or state in (3, 4) or crc != zlib.crc32(data[:4], 0xffffffff) & 0xffffffff:
        return None
    require(state in (0, 1, 2), 'Unknown OTA state')
    return seq, state


def selection(seq, state):
    raw = struct.pack('<I', seq)
    return raw + b'\xff' * 20 + struct.pack('<II', state, zlib.crc32(raw, 0xffffffff) & 0xffffffff) + b'\xff' * (4096 - 32)


def layout(image):
    parts = {}
    for off in range(0x8000, 0x9000, 32):
        magic, kind, subtype, start, size, label, flags = struct.unpack_from('<HBBII16sI', image, off)
        if magic in (0xffff, 0xebeb):
            break
        require(magic == 0x50aa and flags == 0, 'Unsupported partition entry')
        name = label.split(b'\0')[0].decode('ascii')
        require(name not in parts, 'Duplicate partition')
        parts[name] = (kind, subtype, start, size)
    expanded = parts.get('bootfs0', (0, 0, 0, 0))[3] == 0x550000
    store_bytes = 0x550000 if expanded else 0x510000
    native = (0x10000, 0x840000 if expanded else 0x800000)
    stores = (0x2f0000, 0xaa0000 if expanded else 0xae0000)
    expected = {'nvs': (1, 2, 0x9000, 0x6000), 'app0': (0, 0x10, native[0], 0x260000),
                'app1': (0, 0x11, native[1], 0x260000), 'appdata': (1, 0x41, 0x270000, 0x80000),
                'bootfs0': (1, 0x82, stores[0], store_bytes), 'bootfs1': (1, 0x82, stores[1], store_bytes),
                'otadata': (1, 0, 0xff0000, 0x2000), 'bank_state': (1, 0x40, 0xff2000, 0x2000)}
    require(parts == expected, 'Unsupported paired partition geometry; no layout migration permitted')
    return expanded, native, stores, store_bytes


def pair(image, bank, geometry):
    expanded, native, stores, store_bytes = geometry
    row = parse_record(image[0xff2000 + bank * 4096:0xff2060 + bank * 4096], True, expanded)
    require(row[2] == bank, 'Bank identity mismatch')
    firmware = image[native[bank]:native[bank] + row[3]]
    store = image[stores[bank]:stores[bank] + store_bytes]
    require(hashlib.sha256(firmware).digest() == row[6] and hashlib.sha256(store).digest() == row[7], 'Pair readback integrity mismatch')
    return row, firmware, store


def plan(snapshot, baseline, candidate, baseline_sha256):
    require(len(snapshot) == len(baseline) == len(candidate) == 0x1000000, 'Full 16 MiB images required')
    require(sha(baseline) == baseline_sha256, 'Installed release baseline digest mismatch')
    geometry = layout(baseline)
    require(snapshot[:0x9000] == baseline[:0x9000] == candidate[:0x9000], 'Bootloader/partition mismatch; no layout migration permitted')
    entries = [(row[0], i, row[1]) for i in range(2) if (row := ota(snapshot[0xff0000+i*4096:0xff0020+i*4096]))]
    require(bool(entries), 'No valid source OTA selection')
    if len(entries) == 2:
        require(entries[0][0] != entries[1][0], 'Ambiguous equal OTA sequences')
    seq, selected, state = max(entries)
    require(state == 2 and seq < 0xfffffffe, 'Source must be confirmed with incrementable sequence')
    active = (seq-1) % 2
    target = 1-active
    old, _, _ = pair(snapshot, active, geometry)
    admitted, _, _ = pair(baseline, 0, geometry)
    require(old[3:8] == admitted[3:8], 'Active pair differs from installed release baseline')
    _, firmware, store = pair(candidate, 0, geometry)
    expanded, native, stores, store_bytes = geometry
    bank = record(target, firmware, store, True, expanded) + b'\xff'*(4096-96)
    rows = [('firmware.bin', native[target], firmware), ('store.bin', stores[target], store),
            ('bank-state.bin', 0xff2000+target*4096, bank),
            ('ota-selection-LAST.bin', 0xff0000+(1-selected)*4096, selection(seq+1, 0))]
    protected = [(0, 0x10000), (native[active], native[active]+0x260000),
                 (stores[active], stores[active]+store_bytes), (0x270000, 0x2f0000),
                 (0xff2000+active*4096, 0xff3000+active*4096),
                 (0xff0000+selected*4096, 0xff1000+selected*4096), (0xff4000, 0x1000000)]
    for _, offset, data in rows:
        end = offset + (len(data)+4095)//4096*4096
        require(not any(offset < stop and end > start for start, stop in protected), 'Write/erase intersects preserved data')
    return rows, dict(active_bank=active, target_bank=target, old_sequence=seq, new_sequence=seq+1,
                      source_sha256=sha(snapshot), baseline_sha256=baseline_sha256, candidate_sha256=sha(candidate),
                      store_abi=3 if expanded else 2, preserved_ranges=protected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('snapshot', 'baseline', 'candidate', 'output'):
        parser.add_argument('--'+key, type=Path, required=True)
    parser.add_argument('--baseline-sha256', required=True)
    parser.add_argument('--software-fixture', action='store_true')
    args = parser.parse_args()
    rows, receipt = plan(args.snapshot.read_bytes(), args.baseline.read_bytes(), args.candidate.read_bytes(), args.baseline_sha256)
    args.output.mkdir(parents=True, exist_ok=False)
    for name, _, data in rows:
        (args.output/name).write_bytes(data)
    receipt.update(software_fixture=args.software_fixture, hardware_tested=False, device_accessed=False,
                   writes=[dict(file=name, offset=offset, bytes=len(data), sha256=sha(data)) for name, offset, data in rows],
                   protocol=['Use a fresh full readback from this device and the verified digest of its installed release.',
                             'Keep ROM download mode. Stage firmware, store and bank-state without resetting.',
                             'Read back all three regions and verify their exact hashes before selection. Stop on any mismatch.',
                             'Write the separate OTA selection sector LAST. Read it back and verify its exact hash before resetting.',
                             'Never erase_flash or write the full factory image. Preserve NVS, app-data, active pair and the SD card.',
                             'NEW requires existing bootloader health confirmation; failed confirmation uses existing rollback.'])
    (args.output/'plan.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
