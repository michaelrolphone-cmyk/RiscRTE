#!/usr/bin/env python3
"""Test-only SDK assembly for the frozen display-startup regression fixtures.

The Runtime under test owns every header it supplies. Historical Reader/X4
fixtures contribute only missing headers, never replace an authoritative Runtime
header. This is not a product SDK builder and does not relax prepare_sdk.py.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile


def prepare(runtime, reader, x4, output):
    output = Path(output)
    if output.exists():
        raise ValueError('SDK output must be new')
    headers = {}
    shadows = []
    inputs = (('runtime/app', Path(runtime) / 'sdk/app', True),
              ('runtime/driver', Path(runtime) / 'sdk/driver', True),
              ('runtime/hardware', Path(runtime) / 'sdk/hardware', True),
              ('reader/driver', Path(reader) / 'sdk/driver', False),
              ('x4/interfaces', Path(x4) / 'minimal/interfaces', False))
    for label, folder, authoritative in inputs:
        if not folder.is_dir():
            raise ValueError('Missing SDK input: ' + label)
        for path in sorted(folder.glob('*.h')):
            if path.is_symlink() or not path.is_file():
                raise ValueError('Expected regular SDK header: ' + str(path))
            data = path.read_bytes()
            existing = headers.get(path.name)
            if existing is not None:
                if existing['data'] == data:
                    existing['origins'].append(label)
                elif existing['authoritative'] and not authoritative:
                    # Frozen fixture copies may predate append-only Runtime
                    # suffixes. Exclude them and record exactly what was ignored.
                    shadows.append({'name': path.name, 'origin': label,
                                    'sha256': hashlib.sha256(data).hexdigest()})
                else:
                    raise ValueError('Conflicting SDK inputs: ' + path.name)
            else:
                headers[path.name] = {'data': data, 'origins': [label],
                                      'authoritative': authoritative}
    output.mkdir(parents=True)
    try:
        records = {}
        for name, item in headers.items():
            (output / name).write_bytes(item['data'])
            records[name] = {'sha256': hashlib.sha256(item['data']).hexdigest(),
                             'origins': item['origins'],
                             'authority': 'runtime' if item['authoritative'] else 'fixture-only'}
        manifest = {'scope': 'display-startup host regression only',
                    'headers': records, 'excluded_frozen_headers': shadows}
        (output / 'source-hashes.json').write_text(json.dumps(manifest, indent=2) + '\n')
    except Exception:
        shutil.rmtree(output)
        raise
    return manifest


def self_test():
    with tempfile.TemporaryDirectory(prefix='display-sdk-test-') as temporary:
        root = Path(temporary)
        runtime, reader, x4 = (root / name for name in ('runtime', 'reader', 'x4'))
        for path in (runtime / 'sdk/app', runtime / 'sdk/driver', runtime / 'sdk/hardware',
                     reader / 'sdk/driver', x4 / 'minimal/interfaces'):
            path.mkdir(parents=True)
        (runtime / 'sdk/app/Shared.h').write_text('current Runtime\n')
        (reader / 'sdk/driver/Shared.h').write_text('frozen Reader\n')
        (reader / 'sdk/driver/Extra.h').write_text('fixture extension\n')
        (x4 / 'minimal/interfaces/Extra.h').write_text('fixture extension\n')
        result = prepare(runtime, reader, x4, root / 'sdk')
        assert (root / 'sdk/Shared.h').read_text() == 'current Runtime\n'
        assert (root / 'sdk/Extra.h').read_text() == 'fixture extension\n'
        assert result['headers']['Shared.h']['authority'] == 'runtime'
        assert result['headers']['Extra.h']['origins'] == ['reader/driver', 'x4/interfaces']
        assert result['excluded_frozen_headers'][0]['name'] == 'Shared.h'
        assert (reader / 'sdk/driver/Shared.h').read_text() == 'frozen Reader\n'
        for fault in ('existing', 'runtime-conflict', 'fixture-conflict', 'symlink'):
            output = root / fault
            if fault == 'existing':
                output = root / 'sdk'
            if fault == 'runtime-conflict':
                (runtime / 'sdk/driver/Shared.h').write_text('different Runtime\n')
            if fault == 'fixture-conflict':
                (runtime / 'sdk/driver/Shared.h').unlink()
                (x4 / 'minimal/interfaces/Extra.h').write_text('different fixture\n')
            if fault == 'symlink':
                (x4 / 'minimal/interfaces/Extra.h').write_text('fixture extension\n')
                (reader / 'sdk/driver/Link.h').symlink_to(runtime / 'sdk/app/Shared.h')
            try:
                prepare(runtime, reader, x4, output)
            except ValueError:
                pass
            else:
                raise AssertionError('SDK validation accepted ' + fault)
            if fault != 'existing':
                assert not output.exists()
        print('Display fixture SDK: Runtime authority, frozen shadows, conflict and symlink controls PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    for name in ('runtime', 'reader', 'x4', 'output'):
        parser.add_argument('--' + name, type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if any(getattr(args, name) is None for name in ('runtime', 'reader', 'x4', 'output')):
            parser.error('--runtime, --reader, --x4 and --output are required')
        result = prepare(args.runtime, args.reader, args.x4, args.output)
        print('Display fixture SDK:', len(result['headers']), 'headers;',
              len(result['excluded_frozen_headers']), 'frozen copies excluded in favor of Runtime')
