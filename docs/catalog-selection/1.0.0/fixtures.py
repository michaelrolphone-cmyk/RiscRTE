"""Source-authored deterministic fixture inputs, never installable product data."""
import copy
import importlib.util
from pathlib import Path
import sys
from selection import PAYLOAD, Payload, base, canonical, sha256, row_key
sys.path.insert(0, str(PAYLOAD))
spec = importlib.util.spec_from_file_location('payload_fixture_source', PAYLOAD / 'make_examples.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


def source(kind='bootstrap', count=3):
    m, c, contents = generator.fixture(kind)
    if count != 3:
        assert count == 45
        contents = {k: v for k, v in contents.items() if k.startswith('firmware/')}
        files = []
        for i in range(45):
            name = f'app-{i:02}.elf'
            value = f'SYNTHETIC APP {i}\n'.encode()
            contents['payload/' + name] = value
            files.append({'source': 'payload/' + name, 'bytes': len(value), 'sha256': sha256(value),
                          'volume': 'content', 'destination': '/example/' + name, 'owner': 'fixture-app',
                          'operation': 'replace' if i < 3 else 'create',
                          'previous': {'bytes': 3, 'sha256': sha256(b'old')} if i < 3 else None})
        m['payload']['files'] = files
        m['payload']['totals'] = {'file_count': 45, 'bytes': sum(f['bytes'] for f in files)}
        m['payload']['volumes'][0]['requirements']['operations'] = ['stat', 'get', 'mkdir', 'create', 'replace']
        m['payload']['inventory_sha256'] = base.inventory_hash(m['payload'])
        c['trusted_manifest_sha256'] = sha256(canonical(m))
        c['trusted_container_sha256'] = sha256(base.pack(m, contents))
    return m, c, contents


def payload_at(root, kind='bootstrap', count=3):
    m, c, contents = source(kind, count)
    path = Path(root) / 'container.zip'
    path.write_bytes(base.pack(m, contents))
    return Payload(path, c, allow_fixture=True), contents


def file_state(data, flags=0):
    return {'kind': 'file', 'status': 0, 'flags': flags, 'bytes': len(data), 'sha256': sha256(data)}


def observation(payload, contents, scenario='mixed', session=0x12345678, firmware=None):
    firmware = firmware or payload.transfer_firmware
    rows = []
    for i, entry in enumerate(payload.files):
        if scenario == 'clean' or (scenario == 'mixed' and i >= 2):
            current = {'kind': 'absent', 'status': 1}
        elif scenario == 'all-equal' or (scenario == 'mixed' and i == 1):
            current = file_state(contents[entry['source']])
        else:
            current = file_state(b'customized by owner\n')
        rows.append(dict(row_key(payload, entry, firmware), current=current))
    return {'observation_version': '1.0.0', 'binding': payload.binding(firmware, session),
            'session_usable': True,
            'namespaces': [{'namespace_id': n['namespace_id'], 'writable': n['writable']}
                           for n in payload.context['namespaces'] if n['firmware_id'] == firmware],
            'files': rows,
            'parents': [dict(row_key(payload, p, firmware), current={'kind': 'directory', 'status': 0, 'flags': 1})
                        for p in payload.parents]}


def choices_for(payload, obs, skipped=()):
    choices = []
    for i, (row, entry) in enumerate(zip(obs['files'], payload.files)):
        current = row['current']
        action = ('skip' if i in skipped else 'create' if current['kind'] == 'absent'
                  else 'verify-equal' if current['kind'] == 'file' and current['sha256'] == entry['sha256']
                  else 'replace-observed')
        # Synthetic test inputs stand for discrete UI choices, not automatic live defaults.
        choices.append({'action': action, 'consent': action == 'replace-observed'})
    return choices
