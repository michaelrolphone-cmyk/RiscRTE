#!/usr/bin/env python3
"""Build a PRIVATE first-install image for an explicitly NEW paired device.

Offline only: never opens a device, serial port, network, or flash interface.
The full image overwrites NVS and (ABI2) app-data. NEVER use for existing devices.
"""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

import provision_seed as seed
from provision_profile import (decode, destination, encode, read, require, run_validator,
                               safe_path, sha, write, MAX_PROFILE)

NVS_BYTES = 0x6000
FLASH_BYTES = 0x1000000
# Exact official Espressif IDF v4.4.7 source, retrieved without modification:
# https://github.com/espressif/esp-idf/blob/v4.4.7/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py
NVS_GENERATOR_SHA256 = 'c6979797dcf373b6e0f2700c6d7b54675f3f197619e61325acd8865ca72182b3'


def verify_seed(directory, work):
    directory = safe_path(directory)
    record = decode(read(directory / 'seed.json', 128 * 1024))
    require(record['schema'] == 'riscrte.provisioning-seed' and
            type(record['schema_version']) is int and record['schema_version'] == 1, 'seed schema')
    expected, app_data = seed.layout(record)
    names = set(seed.PAYLOADS) | set(seed.SEED_FILES) | {'candidate.json', 'bootfs0.bin', 'otadata.bin', 'bank_state.bin'}
    if app_data:
        names |= {'appdata.bin', 'appdata-image.json'}
    if record['target'] == 'esp32s3-16mb-appdata-iq':
        names.add('radio-iq-proof.json')
    require(set(record['assets']) == names and record['segments'] == seed.segments(app_data), 'seed asset/offset inventory')
    require({p.name for p in directory.iterdir()} == names | {'seed.json', 'SHA256SUMS'}, 'incomplete or extra seed files')
    blobs = {name: read(directory / name, 32 * 1024 * 1024) for name in sorted(names)}
    for name, data in blobs.items():
        require(record['assets'][name] == {'bytes': len(data), 'sha256': sha(data)}, 'seed digest mismatch')
    checks = {**blobs, 'seed.json': read(directory / 'seed.json', 128 * 1024)}
    require(read(directory / 'SHA256SUMS', 8192) == ''.join(
            f'{sha(data)}  {name}\n' for name, data in sorted(checks.items())).encode(), 'seed completion checksum mismatch')
    # Revalidate the frozen bytes, not paths an external process can change after
    # verification. The candidate checks partition geometry, native identity,
    # pinned rollback bootloader/TLS proof and ABI2 initial LittleFS custody.
    frozen = work / 'seed'; frozen.mkdir()
    for name, data in blobs.items():
        (frozen / name).write_bytes(data)
    candidate, _ = seed.candidate(frozen, record['source_sha'])
    require(all(record[key] == candidate[key] for key in
                ('source_sha', 'target', 'layout', 'store_abi', 'firmware_version')), 'seed candidate identity mismatch')
    require(len(blobs['bootfs0.bin']) == expected['bootfs0'][3], 'seed store size mismatch')
    require(blobs['bank_state.bin'] == seed.initial_bank_state(
            blobs['firmware.bin'], blobs['bootfs0.bin'], app_data), 'seed journal binding mismatch')
    require(blobs['otadata.bin'] == seed.initial_otadata(), 'seed OTA selector mismatch')
    return record, blobs


def crc(data):
    return zlib.crc32(data, 0xffffffff) & 0xffffffff


def verify_nvs(data, expected):
    """Read back only the fresh generator's namespace and v2 blob entries.

    This deliberately rejects general live NVS state, encryption, deletions and
    unexpected keys. It is not a migration/recovery tool or hardware NVS model.
    """
    require(len(data) == NVS_BYTES and data[-4096:] == b'\xff' * 4096, 'fresh NVS geometry')
    chunks, indexes = {}, {}
    namespace = False
    sequences = []
    for offset in range(0, len(data) - 4096, 4096):
        page = data[offset:offset + 4096]
        if page == b'\xff' * 4096:
            continue
        state, sequence = struct.unpack_from('<II', page)
        require(state in (0xfffffffe, 0xfffffffc) and page[8] == 0xfe and
                struct.unpack_from('<I', page, 28)[0] == crc(page[4:28]), 'NVS page integrity')
        sequences.append(sequence)
        bitmap = int.from_bytes(page[32:64], 'little')
        index = 0
        while index < 126:
            bits = (bitmap >> (index * 2)) & 3
            if bits == 3:
                index += 1
                continue
            require(bits == 2, 'NVS entry state')
            entry = page[64 + index * 32:96 + index * 32]
            ns, kind, span, chunk = entry[:4]
            require(span > 0 and index + span <= 126 and
                    all((bitmap >> (i * 2)) & 3 == 2 for i in range(index, index + span)) and
                    struct.unpack_from('<I', entry, 4)[0] == crc(entry[:4] + entry[8:]), 'NVS entry integrity')
            key = entry[8:24].split(b'\0', 1)[0].decode('ascii')
            if ns == 0 and kind == 1:
                require(not namespace and key == 'rte_bootstrap' and span == 1 and entry[24:32] == b'\x01' + b'\xff' * 7,
                        'NVS namespace mismatch')
                namespace = True
            elif ns == 1 and kind == 0x42:
                size = struct.unpack_from('<H', entry, 24)[0]
                require(key in expected and (key, chunk) not in chunks and 1 <= size <= (span - 1) * 32,
                        'NVS blob chunk bounds')
                payload = page[96 + index * 32:96 + index * 32 + size]
                require(struct.unpack_from('<I', entry, 28)[0] == crc(payload), 'NVS blob CRC')
                chunks[(key, chunk)] = payload
            elif ns == 1 and kind == 0x48:
                require(key in expected and key not in indexes and span == 1, 'NVS blob index')
                indexes[key] = (struct.unpack_from('<I', entry, 24)[0], entry[28], entry[29])
            else:
                raise ValueError('unexpected fresh NVS entry')
            index += span
    require(namespace and sequences == list(range(len(sequences))) and set(indexes) == set(expected), 'NVS inventory mismatch')
    consumed = set()
    for key, (size, count, start) in indexes.items():
        require(count > 0 and start + count <= 256, 'NVS chunk count')
        keys = [(key, n) for n in range(start, start + count)]
        require(all(k in chunks for k in keys), 'missing NVS chunk')
        payload = b''.join(chunks[k] for k in keys)
        require(len(payload) == size and payload == expected[key], 'NVS blob readback mismatch')
        consumed.update(keys)
    require(consumed == set(chunks), 'unindexed NVS chunk')


def make_nvs(generator, inputs, work):
    generator = safe_path(generator)
    require(sha(read(generator, 128 * 1024)) == NVS_GENERATOR_SHA256, 'unverified official NVS generator')
    image = work / 'nvs.bin'
    subprocess.run([sys.executable, str(generator), 'generate', str(inputs / 'nvs.csv'),
                    str(image), hex(NVS_BYTES), '--version', '2'],
                   check=True, capture_output=True, timeout=120, cwd=work)
    data = read(image, NVS_BYTES)
    verify_nvs(data, {key: read(inputs / (key + '.bin')) for key in ('profile', 'descriptor', 'time')})
    return data


def compose(seed_directory, owner_directory, validator, generator, output, new_device=False):
    require(new_device is True, 'explicit new-device acknowledgement required; existing-device NVS must never be replaced')
    output = destination(output, private=True)
    owner_directory = safe_path(owner_directory)
    require(read(owner_directory / 'COMPLETE', 64) == b'riscrte.owner-provisioning.v1\n', 'incomplete owner profile')
    owner = decode(read(owner_directory / 'owner.json', 4096))
    require(owner['schema'] == 'riscrte.owner-provisioning' and type(owner['schema_version']) is int and owner['schema_version'] == 1, 'owner profile identity')
    profile = read(owner_directory / 'profile.json', MAX_PROFILE)
    require(owner['profile_sha256'] == sha(profile) and owner['profile_bytes'] == len(profile), 'owner profile custody')
    with tempfile.TemporaryDirectory(prefix='riscrte-first-install-') as temp:
        work = Path(temp)
        record, blobs = verify_seed(seed_directory, work)
        require(owner['layout'] == record['layout'], 'owner inventory and seed layout mismatch')
        require(owner['runtime_target'] == record['target'], 'owner inventory and Runtime target mismatch')
        private_profile = work / 'profile.json'
        write(private_profile, profile)
        run_validator(validator, private_profile, work / 'inputs', owner['time_server'])
        nvs = make_nvs(generator, work / 'inputs', work)
        offsets = {**record['segments'], 'nvs.bin': 0x9000}
        payloads = {name: blobs[name] for name in record['segments']}
        payloads['nvs.bin'] = nvs
        image = bytearray(b'\xff' * FLASH_BYTES)
        end = 0
        for name, offset in sorted(offsets.items(), key=lambda item: item[1]):
            data = payloads[name]
            require(type(offset) is int and offset >= end and offset + len(data) <= FLASH_BYTES, 'first-install segment overlap/bounds')
            image[offset:offset + len(data)] = data
            end = offset + len(data)
        # Keep exact owner profile available for review alongside its private
        # encoded NVS. Everything in this output is private, including hashes.
        payloads['profile.json'] = profile
        payloads['first-install.bin'] = bytes(image)
        manifest = {'schema': 'riscrte.first-install', 'schema_version': 1,
                    'layout': record['layout'], 'store_abi': record['store_abi'], 'target': record['target'],
                    'source_sha': record['source_sha'], 'firmware_version': record['firmware_version'],
                    'flash_bytes': FLASH_BYTES, 'segments': offsets,
                    'profile_sha256': sha(profile), 'nvs_generator_sha256': NVS_GENERATOR_SHA256,
                    'assets': {name: {'bytes': len(data), 'sha256': sha(data)} for name, data in sorted(payloads.items())},
                    'prerequisites': ['Explicit NEW 16 MiB device installation only.',
                                      'Full image overwrites NVS and ABI2 app-data; never use on an existing device.',
                                      'Owner must separately verify hardware/layout and authorize device installation.'],
                    'scope': 'PRIVATE credentials; keep local. Offline first-install image only. No device accessed; hardware UNRUN.'}
        output.mkdir(mode=0o700)
        try:
            for name, data in payloads.items():
                write(output / name, data)
            for name, data in payloads.items():
                require(read(output / name, 32 * 1024 * 1024) == data, 'first-install output readback mismatch')
            write(output / 'first-install.json', encode(manifest))
            write(output / 'SHA256SUMS', ''.join(f'{sha(read(p, 32 * 1024 * 1024))}  {p.name}\n'
                                               for p in sorted(output.iterdir())).encode())
            write(output / 'COMPLETE', b'riscrte.first-install.v1\n')
        except BaseException:
            shutil.rmtree(output)
            raise
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('seed', 'owner', 'validator', 'nvs-generator', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--new-device', action='store_true', required=True,
                        help='Acknowledge this whole-flash image is ONLY for a NEW device, never an existing installation')
    args = parser.parse_args()
    try:
        compose(args.seed, args.owner, args.validator, args.nvs_generator, args.output, args.new_device)
    except (Exception, KeyboardInterrupt):
        print('First-install preparation failed; no valid image produced. No device accessed.', file=sys.stderr)
        return 1
    print('Private NEW-device image complete. Contains credentials; keep local. No device accessed; hardware UNRUN.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
