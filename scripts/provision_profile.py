#!/usr/bin/env python3
"""Pin a complete local product store and build PRIVATE owner provisioning input.

Offline only. URLs are explicit owner input, never discovered or invented.
The product pipeline must already have admitted its board/driver/application
store; these byte-custody checks do not replace native graph/ELF admission.
"""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from release_assets import require

LAYOUTS = {'riscrte-paired-16m-v1': 0x4f0000,
           'riscrte-paired-appdata-v2': 0x510000}
TARGET_LAYOUTS = {'esp32s3-16mb-paired': 'riscrte-paired-16m-v1',
                  'esp32s3-16mb-appdata': 'riscrte-paired-appdata-v2',
                  'esp32s3-16mb-appdata-iq': 'riscrte-paired-appdata-v2'}
MAX_FILES = 128
MAX_PROFILE = 16384
REQUIRED = {'boot.json', 'board.json', 'default.elf'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON field')
        result[key] = value
    return result


def decode(data):
    return json.loads(data.decode('utf-8'), object_pairs_hook=unique)


def encode(value):
    return (json.dumps(value, ensure_ascii=False, separators=(',', ':'),
                       sort_keys=True) + '\n').encode('utf-8')


def safe_path(path):
    path = Path(path).absolute()
    require(not any(p.is_symlink() for p in (path, *path.parents)), 'symlink path refused')
    return path


def read(path, maximum=16 * 1024 * 1024):
    path = safe_path(path)
    require(path.is_file() and 0 < path.stat().st_size <= maximum, 'input file bounds')
    with path.open('rb') as stream:
        result = stream.read(maximum + 1)
    require(0 < len(result) <= maximum, 'input file bounds')
    return result


def relative(path, maximum=30):
    require(isinstance(path, str) and 0 < len(path.encode('utf-8')) <= maximum,
            'native SPIFFS relative path bound')
    require(all(part not in ('', '.', '..') and re.fullmatch(r'[A-Za-z0-9_.-]+', part)
                for part in path.split('/')), 'unsafe store path')
    require(path != '.provision-sha256', 'reserved store path')
    return path


def source(url, directory=False):
    require(isinstance(url, str) and len(url.encode('utf-8')) <= 384, 'HTTPS source bounds')
    match = re.fullmatch(r'https://([A-Za-z0-9.-]+)/(.*)', url)
    require(match is not None, 'explicit canonical HTTPS source required')
    path = match.group(2)
    if directory and not path:
        return url
    if directory:
        require(path.endswith('/'), 'base URL must end in slash')
        path = path[:-1]
    relative(path, 192)
    return url


def destination(path, private=False):
    path = safe_path(path)
    require(not path.exists(), 'output already exists')
    require(path.parent.is_dir(), 'output parent must exist')
    if private:
        # Do not rely on .gitignore: a future artifact upload can include ignored
        # dist/build files. Credentials must live outside every Git working tree.
        # A corrupt/dubiously-owned repository or inherited Git override must
        # never turn a failed discovery command into permission to save secrets.
        for parent in (path.parent, *path.parent.parents):
            try:
                marker = parent / '.git'
                marker.lstat()
            except FileNotFoundError:
                continue
            # Some isolated executors install empty, read-only .git guard
            # directories. They are not repositories; nonempty/corrupt markers
            # still refuse output rather than trusting a discovery failure.
            if marker.is_dir() and not marker.is_symlink() and not any(marker.iterdir()):
                continue
            raise ValueError('private output must be outside Git working trees and publication directories')
        require(not any(os.environ.get(key) for key in
                        ('GIT_DIR', 'GIT_WORK_TREE', 'GIT_COMMON_DIR')),
                'ambiguous Git location; clear repository overrides for private output')
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith('GIT_')}
        environment['LC_ALL'] = 'C'
        result = subprocess.run(['git', '-C', str(path.parent), 'rev-parse',
                                 '--is-inside-work-tree'], capture_output=True, env=environment)
        if result.returncode == 0:
            require(result.stdout.strip() == b'false',
                    'private output must be outside Git working trees and publication directories')
        else:
            first = result.stderr.splitlines()[0] if result.stderr else b''
            require(result.returncode == 128 and
                    first.startswith(b'fatal: not a git repository (or any '),
                    'cannot verify private output is outside Git; refusing output')
    return path


def write(path, data):
    with Path(path).open('xb') as stream:
        os.chmod(path, 0o600)
        stream.write(data)


def store_files(directory):
    directory = safe_path(directory)
    require(directory.is_dir(), 'product store directory required')
    files = {}
    total = 0
    for root, directories, names in os.walk(directory, followlinks=False):
        for name in directories:
            require(not (Path(root) / name).is_symlink(), 'symlink store directory')
        for name in sorted(names):
            path = Path(root) / name
            key = relative(path.relative_to(directory).as_posix())
            require(len(files) < MAX_FILES, 'too many product store files')
            data = read(path, 8 * 1024 * 1024)
            total += len(data)
            require(total <= 16 * 1024 * 1024, 'product store size bound')
            files[key] = data
    require(REQUIRED <= files.keys(), 'complete boot.json, board.json and default.elf required')
    return files


def spiffs_charge(byte_lengths, partition_bytes):
    """Mirror native StoreFiles' pinned geometry and 8192-byte append batching.

    Includes the profile digest file, lookup-page loss, index update garbage,
    and four fully reserved blocks. This bounds admission, not physical wear
    or power-loss durability; real write/readback failures still fail closed.
    """
    require(type(partition_bytes) is int and partition_bytes % 4096 == 0 and
            partition_bytes > 4 * 4096, 'invalid native store geometry')
    charged = 0
    for size in [*byte_lengths, 32]:
        require(type(size) is int and size > 0, 'file byte bounds')
        data_pages = (size + 250) // 251
        index_pages = 1 + (max(0, data_pages - 103) + 123) // 124
        writes = (size + 8191) // 8192
        charged += data_pages + 2 * index_pages + writes - 1
    return charged, (partition_bytes // 4096 - 4) * 15


def validate_inventory(record):
    image = isinstance(record, dict) and record.get('schema_version') == 2
    fields = {'schema', 'schema_version', 'layout', 'runtime_target', 'files'} | ({'image'} if image else set())
    require(isinstance(record, dict) and set(record) == fields,
            'inventory fields')
    require(record['schema'] == 'riscrte.provisioning-inventory' and
            type(record['schema_version']) is int and record['schema_version'] in (1, 2) and
            record['layout'] in LAYOUTS and
            TARGET_LAYOUTS.get(record['runtime_target']) == record['layout'], 'inventory identity')
    if image:
        item = record['image']
        require(isinstance(item, dict) and set(item) == {'url', 'bytes', 'sha256'}, 'inventory image fields')
        source(item['url'])
        require(type(item['bytes']) is int and item['bytes'] == LAYOUTS[record['layout']], 'image partition bounds')
        require(isinstance(item['sha256'], str) and re.fullmatch('[0-9a-f]{64}', item['sha256']), 'image digest')
    entries = record['files']
    require(isinstance(entries, list) and 3 <= len(entries) <= MAX_FILES, 'inventory count')
    names = set()
    total = 32  # Committed exact-profile SHA stored by native backend.
    for item in entries:
        require(isinstance(item, dict) and set(item) == ({'path', 'bytes', 'sha256'} if image else {'path', 'url', 'bytes', 'sha256'}), 'inventory file fields')
        name = relative(item['path'])
        require(name not in names and not any(name.startswith(old + '/') or old.startswith(name + '/')
                                             for old in names), 'duplicate or overlapping store path')
        if not image:
            source(item['url'])
        require(type(item['bytes']) is int and 1 <= item['bytes'] <= 8 * 1024 * 1024, 'file byte bounds')
        require(isinstance(item['sha256'], str) and re.fullmatch('[0-9a-f]{64}', item['sha256']), 'file digest')
        total += item['bytes']
        names.add(name)
    require(REQUIRED <= names, 'complete boot.json, board.json and default.elf required')
    if image:
        # Necessary metadata bound only. The actual image's occupied/deleted
        # pages and completely erased blocks must also pass image admission.
        pages = sum((item['bytes'] + 250) // 251 + 1 +
                    (max(0, (item['bytes'] + 250) // 251 - 103) + 123) // 124 for item in entries)
        require(total <= LAYOUTS[record['layout']] and
                pages <= (LAYOUTS[record['layout']] // 4096 - 4) * 15, 'native image capacity exceeded')
    else:
        charged, available = spiffs_charge([item['bytes'] for item in entries], LAYOUTS[record['layout']])
        require(total <= 16 * 1024 * 1024 and charged <= available, 'native store capacity exceeded')
    return record


def verify_inventory(record, files):
    validate_inventory(record)
    require({entry['path'] for entry in record['files']} == set(files), 'full store inventory mismatch')
    for item in record['files']:
        data = files[item['path']]
        require(len(data) == item['bytes'] and sha(data) == item['sha256'], 'pinned store bytes mismatch')


def pin_store(store, layout, base_url=None, sources=None, target=None):
    require((base_url is None) != (sources is None), 'supply exactly one explicit base URL or source map')
    files = store_files(store)
    if base_url is not None:
        source(base_url, True)
        urls = {name: base_url + name for name in files}
    else:
        require(isinstance(sources, dict) and set(sources) == set(files), 'full source map required')
        urls = sources
    record = {'schema': 'riscrte.provisioning-inventory', 'schema_version': 1,
              'layout': layout, 'runtime_target': target, 'files': [{'path': name, 'url': urls[name],
              'bytes': len(data), 'sha256': sha(data)} for name, data in sorted(files.items())]}
    return validate_inventory(record)


def build_inventory(store, layout, output, base_url=None, sources=None, target=None):
    output = destination(output)
    files = store_files(store)
    # Construct the pin and recheck it against the frozen distribution bytes.
    # Any concurrent source change refuses the artifact instead of silently
    # pairing a manifest with a different payload snapshot.
    record = pin_store(store, layout, base_url, sources, target)
    verify_inventory(record, files)
    output.mkdir()
    try:
        for name, data in files.items():
            target = output / 'files' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            write(target, data)
        verify_inventory(record, store_files(output / 'files'))
        write(output / 'inventory.json', encode(record))
        write(output / 'SHA256SUMS', ''.join(
            f'{sha(read(path))}  {path.relative_to(output).as_posix()}\n'
            for path in sorted(output.rglob('*')) if path.is_file()).encode())
        write(output / 'COMPLETE', b'riscrte.provisioning-inventory.v1\n')
    except BaseException:
        shutil.rmtree(output)
        raise
    return record


def build_image_inventory(store, image, layout, target, image_url, output):
    """Freeze one exact prepacked image and its full readback inventory."""
    from spiffs_image import read_image
    from store_image_capacity import inspect_image
    output = destination(output)
    files = store_files(store)
    raw = read(image, 8 * 1024 * 1024)
    require(layout in LAYOUTS, 'image layout')
    inspect_image(raw, LAYOUTS[layout])
    decoded = read_image(raw, LAYOUTS[layout])
    require(decoded == files, 'image and complete product store differ')
    record = {'schema': 'riscrte.provisioning-inventory', 'schema_version': 2,
              'layout': layout, 'runtime_target': target,
              'image': {'url': source(image_url), 'bytes': len(raw), 'sha256': sha(raw)},
              'files': [{'path': name, 'bytes': len(data), 'sha256': sha(data)}
                        for name, data in sorted(files.items())]}
    verify_inventory(record, files)
    output.mkdir()
    try:
        write(output / 'image.bin', raw)
        write(output / 'inventory.json', encode(record))
        require(read(output / 'image.bin') == raw, 'image output readback mismatch')
        write(output / 'SHA256SUMS', ''.join(
            f'{sha(read(path))}  {path.name}\n' for path in sorted(output.iterdir())).encode())
        write(output / 'COMPLETE', b'riscrte.provisioning-inventory.v2\n')
    except BaseException:
        shutil.rmtree(output)
        raise
    return record


def profile_bytes(record, wifi, base_url=None):
    validate_inventory(record)
    require(isinstance(wifi, dict) and set(wifi) == {'ssid', 'password'}, 'wifi file fields')
    for key in ('ssid', 'password'):
        require(isinstance(wifi[key], str) and '\0' not in wifi[key], 'wifi string required')
    require(1 <= len(wifi['ssid'].encode('utf-8')) <= 32 and
            (wifi['password'] == '' or 8 <= len(wifi['password'].encode('utf-8')) <= 63), 'wifi length bounds')
    profile = {'schema': 'riscrte.provisioning', 'schema_version': 1,
               'wifi': wifi, 'files': sorted(record['files'], key=lambda entry: entry['path'])}
    if record['schema_version'] == 2:
        require(base_url is None, 'image inventory has one pinned URL')
        profile.update(schema_version=3, image=record['image'])
    elif base_url is not None:
        source(base_url, True)
        require(all(entry['url'] == base_url + entry['path'] for entry in profile['files']),
                'base URL differs from pinned inventory sources')
        profile.update(schema_version=2, base_url=base_url)
        profile['files'] = [{key: entry[key] for key in ('path', 'bytes', 'sha256')}
                            for entry in profile['files']]
    data = encode(profile)
    require(len(data) <= MAX_PROFILE, 'profile exceeds 16 KiB; use compact schema 2 with one matching base URL')
    return data


def validate_inputs(directory, profile, server):
    expected = {'profile.bin', 'descriptor.bin', 'time.bin', 'nvs.csv', 'COMPLETE'}
    require({p.name for p in directory.iterdir()} == expected, 'incomplete owner input bundle')
    require(read(directory / 'COMPLETE', 64) == b'riscrte.bootstrap-input.v1\n', 'owner input completion marker')
    require(read(directory / 'profile.bin', MAX_PROFILE) == profile, 'owner profile changed')
    require(decode(read(directory / 'descriptor.bin', 384)) ==
            {'schema': 'riscrte.bootstrap', 'schema_version': 1, 'profile_key': 'profile'}, 'owner descriptor mismatch')
    require(decode(read(directory / 'time.bin', 384)) ==
            {'schema': 'riscrte.sntp', 'schema_version': 1, 'servers': [server]}, 'owner time mismatch')
    rows = list(csv.DictReader(io.StringIO(read(directory / 'nvs.csv', 40000).decode())))
    require(rows and rows.pop(0) == {'key': 'rte_bootstrap', 'type': 'namespace', 'encoding': '', 'value': ''}, 'owner NVS namespace')
    require(len(rows) == 3 and {row['key'] for row in rows} == {'profile', 'descriptor', 'time'}, 'owner NVS keys')
    for row in rows:
        require(row['type'] == 'data' and row['encoding'] == 'hex2bin' and
                bytes.fromhex(row['value']) == read(directory / (row['key'] + '.bin')), 'owner NVS bytes mismatch')


def run_validator(validator, profile_path, output, server):
    subprocess.run([str(safe_path(validator)), str(profile_path), str(output), server],
                   check=True, capture_output=True, timeout=60)
    validate_inputs(output, read(profile_path, MAX_PROFILE), server)


def build_profile(inventory, store, wifi_file, validator, server, output, base_url=None):
    output = destination(output, private=True)
    record = decode(read(inventory, 128 * 1024))
    validate_inventory(record)
    if store is not None:
        verify_inventory(record, store_files(store))
    # Consumers may use the publisher's immutable verified inventory directly;
    # the device downloads and revalidates every pinned byte before activation.
    data = profile_bytes(record, decode(read(wifi_file, 1024)), base_url)
    # Mandatory fresh time input: a syntactically valid profile alone cannot
    # bootstrap HTTPS on a blank device. Production parser validates the server.
    require(isinstance(server, str) and server, 'explicit time server required')
    output.mkdir(mode=0o700)
    try:
        write(output / 'profile.json', data)
        run_validator(validator, output / 'profile.json', output / 'inputs', server)
        require(read(output / 'profile.json', MAX_PROFILE) == data, 'owner profile output readback mismatch')
        for item in (output / 'inputs').iterdir():
            os.chmod(item, 0o600)
        receipt = {'schema': 'riscrte.owner-provisioning', 'schema_version': 1,
                   'layout': record['layout'], 'runtime_target': record['runtime_target'], 'profile_sha256': sha(data),
                   'profile_bytes': len(data), 'file_count': len(record['files']),
                   'inventory_sha256': sha(encode(record)), 'time_server': server,
                   'scope': 'PRIVATE owner credentials. Offline byte custody only; device and product hardware qualification remain separate.'}
        write(output / 'owner.json', encode(receipt))
        write(output / 'COMPLETE', b'riscrte.owner-provisioning.v1\n')
    except BaseException:
        shutil.rmtree(output)
        raise
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    pin = commands.add_parser('inventory', help='Pin all files from an already admitted product store')
    pin.add_argument('--store', type=Path, required=True)
    pin.add_argument('--layout', choices=LAYOUTS, required=True)
    pin.add_argument('--target', choices=TARGET_LAYOUTS, required=True)
    origin = pin.add_mutually_exclusive_group(required=True)
    origin.add_argument('--base-url')
    origin.add_argument('--sources', type=Path, help='JSON map of each complete store path to its explicit HTTPS URL')
    pin.add_argument('--output', type=Path, required=True, help='New distribution directory: files/, inventory.json, checksums and completion marker')
    image = commands.add_parser('image-inventory', help='Pin an admitted compact image and its exact complete store')
    for key in ('store', 'image', 'output'):
        image.add_argument('--' + key, type=Path, required=True)
    image.add_argument('--layout', choices=LAYOUTS, required=True)
    image.add_argument('--target', choices=TARGET_LAYOUTS, required=True)
    image.add_argument('--image-url', required=True)
    owner = commands.add_parser('profile', help='Create a private exact-profile and NVS-input bundle')
    for key in ('inventory', 'wifi-file', 'validator', 'output'):
        owner.add_argument('--' + key, type=Path, required=True)
    owner.add_argument('--store', type=Path, help='Optional local full-store byte check; not required to prepare a private profile from a verified published inventory')
    owner.add_argument('--base-url', help='Compact schema 2; must match every pinned source')
    owner.add_argument('--time-server', required=True)
    args = parser.parse_args()
    try:
        if args.command == 'inventory':
            build_inventory(args.store, args.layout, args.output, args.base_url,
                            decode(read(args.sources, 128 * 1024)) if args.sources else None, args.target)
            print('Pinned complete local store; URLs supplied by owner; no downloads or device access.')
        elif args.command == 'image-inventory':
            build_image_inventory(args.store, args.image, args.layout, args.target, args.image_url, args.output)
            print('Pinned complete compact image and exact store; no downloads or device access.')
        else:
            build_profile(args.inventory, args.store, args.wifi_file, args.validator,
                          args.time_server, args.output, args.base_url)
            print('Private provisioning profile and NVS inputs complete; keep outside published artifacts.')
    except (Exception, KeyboardInterrupt) as error:
        # Only these fixed validation labels may be reported. JSON, filesystem
        # and subprocess diagnostics can contain owner data and stay private.
        safe = {'profile exceeds 16 KiB; use compact schema 2 with one matching base URL',
                'native store capacity exceeded', 'base URL differs from pinned inventory sources',
                'private output must be outside Git working trees and publication directories',
                'output already exists', 'native SPIFFS relative path bound'}
        if type(error) is ValueError and str(error) in safe:
            print('Provisioning preparation failed: ' + str(error) + '.', file=sys.stderr)
        else:
            print('Provisioning preparation failed; inspect private inputs and required tools. No device accessed.', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
