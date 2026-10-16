#!/usr/bin/env python3
"""Validate catalog payload containers and their execution plans."""
import argparse
import hashlib
import io
import json
import re
import stat
import struct
import sys
import zipfile
from pathlib import Path

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parent
MAX_MANIFEST_BYTES = 4 * 1024 * 1024
MAX_CONTAINER_BYTES = 2 * 1024 * 1024 * 1024
MAX_FILES = 4096
ZIP_TIME = (1980, 1, 1, 0, 0, 0)


class Invalid(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise Invalid(message)


def canonical(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2,
                       allow_nan=False) + '\n').encode('utf-8')


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON member: ' + key)
        result[key] = value
    return result


def parse(data):
    require(len(data) <= MAX_MANIFEST_BYTES, 'JSON exceeds bounded metadata size')
    try:
        return json.loads(data.decode('utf-8'), object_pairs_hook=unique_object,
                          parse_constant=lambda x: (_ for _ in ()).throw(Invalid(x)))
    except (UnicodeError, json.JSONDecodeError, RecursionError) as exc:
        raise Invalid('invalid UTF-8 JSON') from exc


def schema_check(value, name):
    schema = json.loads((ROOT / name).read_text())
    error = next(Draft202012Validator(schema).iter_errors(value), None)
    require(error is None, name + ': ' + (error.message if error else ''))


def safe_path(value, absolute):
    require(value.startswith('/') == absolute, 'wrong path root: ' + value)
    require(value != '/' and not value.endswith('/'), 'root or trailing slash')
    require(not any(ord(c) < 32 or ord(c) == 127 for c in value), 'control in path')
    require('\\' not in value, 'backslash in path')
    components = value[1:].split('/') if absolute else value.split('/')
    require(all(c and c not in ('.', '..') for c in components), 'unsafe path component')
    try:
        encoded = value.encode('utf-8', errors='strict')
    except UnicodeError as exc:
        raise Invalid('path is not strict UTF-8') from exc
    require(len(encoded) <= 511, 'path exceeds wire bound')
    if not absolute:
        require(re.fullmatch(r'[A-Za-z0-9._/-]+', value) is not None,
                'container source must use portable ASCII')
        require(value != 'manifest.json', 'reserved manifest source')
    return components


def inventory_hash(payload):
    return sha256(canonical({k: v for k, v in payload.items() if k != 'inventory_sha256'}))


def ordered_unique(items, key, message):
    keys = [key(x) for x in items]
    require(len(keys) == len(set(keys)), 'duplicate ' + message)
    require(keys == sorted(keys), 'noncanonical order: ' + message)


def parent_paths(path):
    parts = path.split('/')[1:-1]
    return ['/' + '/'.join(parts[:i]) for i in range(1, len(parts) + 1)]


def pack(manifest, contents):
    """Deterministic fixture/container writer. Does not extract or contact devices."""
    members = dict(contents)
    require('manifest.json' not in members, 'reserved manifest source')
    members['manifest.json'] = canonical(manifest)
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_STORED) as archive:
        for name in sorted(members, key=lambda x: x.encode('utf-8')):
            entry = zipfile.ZipInfo(name, ZIP_TIME)
            entry.create_system = 3
            entry.external_attr = (stat.S_IFREG | 0o644) << 16
            archive.writestr(entry, members[name])
    return out.getvalue()


def validate_manifest(manifest, context, allow_fixture=False):
    schema_check(manifest, 'manifest.schema.json')
    schema_check(context, 'context.schema.json')
    require(allow_fixture or not manifest['fixture'], 'fixture is not installable')
    require(manifest['fixture'] == context['fixture'], 'fixture context mismatch')
    require(sha256(canonical(manifest)) == context['trusted_manifest_sha256'],
            'manifest does not match independently trusted catalog digest')
    product, fw, payload, plan = (manifest[k] for k in ('product', 'firmware', 'payload', 'plan'))
    require(product['id'] == context['product_id'], 'incompatible product')
    require(context['hardware_profile'] in product['hardware_profiles'], 'incompatible profile')
    require(context['hardware_revision'] in product['hardware_revisions'], 'incompatible hardware')
    require(plan['pre_firmware_id'] == context['installed_firmware_id'], 'installed firmware mismatch')
    install = fw['install']
    require(install['method_id'] in context['supported_methods'], 'unsupported flash method')
    require(sha256(canonical(install)) == context['trusted_install_sha256'], 'untrusted install effects')
    transfer_first = plan['order'][0] == 'payload'
    require(not (plan['purpose'] == 'bootstrap' and transfer_first), 'bootstrap requires firmware first')
    require(not transfer_first or plan['pre_firmware_id'] is not None, 'transfer requires admitted firmware')
    ordered_unique(fw['artifacts'], lambda x: x['source'].encode(), 'firmware sources')
    artifact_ids = [a['id'] for a in fw['artifacts']]
    require(len(artifact_ids) == len(set(artifact_ids)), 'duplicate firmware artifact identity')
    require(set(install['artifact_ids']) == set(artifact_ids), 'flash artifact inventory mismatch')
    require(inventory_hash(payload) == payload['inventory_sha256'], 'payload inventory digest mismatch')
    volumes = {v['id']: v for v in payload['volumes']}
    ordered_unique(payload['volumes'], lambda v: v['id'], 'volumes')
    require(set(volumes) == {x['volume'] for x in payload['files']}, 'unused or missing logical volume')
    ordered_unique(install['volume_effects'], lambda e: e['volume'], 'volume effects')
    require(set(volumes) == {e['volume'] for e in install['volume_effects']}, 'missing volume flash effect')
    if transfer_first:
        require(all(e['effect'] == 'preserved' for e in install['volume_effects']),
                'required pre-flash payload would be erased or has unknown survival')
    bindings = {}
    namespace_targets = set()
    ordered_unique(payload['bindings'], lambda b: (b['firmware_id'], b['volume']), 'bindings')
    for binding in payload['bindings']:
        require(binding['volume'] in volumes, 'unknown binding volume')
        require(int(binding['namespace_id']) <= 2**64 - 1, 'namespace exceeds uint64')
        require(sha256(canonical(binding)) in context['trusted_binding_sha256'], 'untrusted namespace mapping')
        key = (binding['firmware_id'], binding['volume'])
        nskey = (binding['firmware_id'], binding['namespace_id'])
        require(nskey not in namespace_targets, 'ambiguous volume alias to namespace')
        namespace_targets.add(nskey)
        bindings[key] = binding
    stages = {fw['id']}
    if transfer_first:
        stages.add(plan['pre_firmware_id'])
    require(set(bindings) == {(stage, volume) for stage in stages for volume in volumes},
            'missing or unexpected firmware namespace mapping')
    namespaces = {}
    for ns in context['namespaces']:
        key = (ns['firmware_id'], ns['namespace_id'])
        require(key not in namespaces, 'ambiguous trusted namespace')
        require(int(ns['namespace_id']) <= 2**64 - 1, 'context namespace exceeds uint64')
        namespaces[key] = ns
    for stage in {key[0] for key in namespaces}:
        require(sum(key[0] == stage for key in namespaces) <= 16, 'too many admitted namespaces for firmware')
    for volume in volumes.values():
        consumer = volume['consumer']
        require(consumer['firmware_id'] == fw['id'], 'consumer firmware mismatch')
        require(sha256(canonical(consumer)) in context['trusted_consumer_sha256'],
                'target consumer access not independently established')
    files = payload['files']
    require(len(files) <= min(MAX_FILES, context['limits']['inventory_files']), 'too many files')
    ordered_unique(files, lambda f: (f['volume'], f['destination'].encode()), 'destinations')
    directories = payload['directories']
    ordered_unique(directories, lambda d: (d['volume'], d['path'].count('/'), d['path'].encode()), 'directories')
    needed_dirs = {(f['volume'], p) for f in files for p in parent_paths(f['destination'])}
    require({(d['volume'], d['path']) for d in directories} == needed_dirs, 'incomplete or extra parent directories')
    require(payload['totals'] == {'file_count': len(files), 'bytes': sum(f['bytes'] for f in files)},
            'payload totals mismatch')
    sources = []
    for entry in fw['artifacts'] + files:
        safe_path(entry['source'], False)
        sources.append(entry['source'])
    require(len(sources) == len(set(sources)), 'duplicate container source')
    for file in files:
        require(file['owner'] == payload['owner'], 'foreign release owner')
        if file['operation'] == 'create':
            require(file.get('previous') is None, 'create cannot claim replacement')
    transfer_stage = plan['pre_firmware_id'] if transfer_first else fw['id']
    for stage in stages:
        writing = stage == transfer_stage
        for vid, volume in volumes.items():
            binding = bindings[(stage, vid)]
            ns = namespaces.get((stage, binding['namespace_id']))
            require(ns is not None, 'namespace not admitted by trusted firmware profile')
            requirements = volume['requirements']
            require(ns['case_semantics'] == requirements['case_semantics'], 'provider naming mismatch')
            vf = [f for f in files if f['volume'] == vid]
            vd = [d for d in directories if d['volume'] == vid]
            transfer_ops = {'stat', 'get'} | {f['operation'] for f in vf} | ({'mkdir'} if vd else set())
            require(set(requirements['operations']) == transfer_ops, 'logical operation requirements mismatch')
            require(requirements['hierarchical'] == bool(vd), 'hierarchy requirement must match parent inventory')
            needed_ops = transfer_ops if writing else {'stat', 'get'}
            require(not vd or ns['hierarchical'], 'provider is not hierarchical')
            require(not writing or ns['writable'], 'provider is read-only')
            require(needed_ops <= set(ns['operations']), 'unsupported provider operation')
            if writing:
                require(sum(f['bytes'] for f in vf) <= ns['write_budget_bytes'], 'insufficient admitted write budget')
            paths = set()
            for item in vd + vf:
                path = item.get('path', item.get('destination'))
                components = safe_path(path, True)
                require(not any(c in path for c in ns['forbidden_characters']), 'provider rejects path character')
                require(not ns['forbid_trailing_dot_space'] or not any(c.endswith((' ', '.')) for c in components),
                        'provider rejects trailing dot or space')
                require(len(path.encode()) <= ns['max_path_bytes'], 'provider path too long')
                require(all(len(c.encode()) <= ns['max_component_bytes'] for c in components),
                        'provider component too long')
                if ns['case_semantics'] == 'ascii-insensitive':
                    require(path.isascii(), 'non-ASCII case-insensitive path unsupported')
                    path = path.lower()
                require(path not in paths, 'file/directory or case alias collision')
                paths.add(path)
                if 'bytes' in item:
                    require(item['bytes'] <= ns['max_file_bytes'], 'provider file too large')
    return {'status': 'static-preflight-passed', 'fixture': manifest['fixture'],
            'catalog_release': manifest['catalog_release']['id'], 'firmware_id': fw['id'],
            'payload_id': payload['id'], 'file_count': len(files), 'bytes': payload['totals']['bytes'],
            'order': plan['order'], 'mutation_performed': False,
            'live_gates_required': ['confirm-selected-device-and-firmware', 'verify-actual-admission-and-provider',
                                    'stat-all-destinations-and-parents', 'confirm-selected-file-replacement-and-check-declared-guards',
                                    'recheck-after-flash-before-file-mutation', 'readback-all-files-after-final-flash',
                                    'establish-target-consumer-usability']}


def validate_zip_representation(path, infos):
    """Check every non-content byte against the deterministic ZIP_STORED encoding."""
    with open(path, 'rb') as source:
        offset, centrals = 0, []
        for info in infos:
            name = info.filename.encode('ascii')
            require(info.header_offset == offset, 'noncanonical ZIP prefix or gap')
            local = struct.pack('<4s5H3I2H', b'PK\x03\x04', 20, 0, 0, 0, 33,
                                info.CRC, info.file_size, info.file_size, len(name), 0) + name
            source.seek(offset)
            require(source.read(len(local)) == local, 'noncanonical local ZIP header')
            central = struct.pack('<4s6H3I5H2I', b'PK\x01\x02', 788, 20, 0, 0, 0, 33,
                                  info.CRC, info.file_size, info.file_size, len(name), 0, 0, 0, 0,
                                  (stat.S_IFREG | 0o644) << 16, offset) + name
            centrals.append(central)
            offset += len(local) + info.file_size
        central_bytes = b''.join(centrals)
        source.seek(offset)
        require(source.read(len(central_bytes)) == central_bytes, 'noncanonical central ZIP directory')
        end = struct.pack('<4s4H2IH', b'PK\x05\x06', 0, 0, len(infos), len(infos),
                          len(central_bytes), offset, 0)
        require(source.read(len(end)) == end and source.read(1) == b'', 'noncanonical ZIP trailer')


def validate_container(path, context, allow_fixture=False):
    schema_check(context, 'context.schema.json')
    size = Path(path).stat().st_size
    require(size <= min(MAX_CONTAINER_BYTES, context['limits']['container_bytes']), 'container too large')
    digest = hashlib.sha256()
    with open(path, 'rb') as source:
        for chunk in iter(lambda: source.read(65536), b''):
            digest.update(chunk)
    require(digest.hexdigest() == context['trusted_container_sha256'], 'container digest mismatch')
    with zipfile.ZipFile(path) as archive:
        infos = archive.infolist()
        require(len(infos) <= MAX_FILES + 257, 'too many archive members')
        require(not archive.comment, 'archive comment unsupported')
        names = [i.filename for i in infos]
        require(names == sorted(set(names), key=lambda x: x.encode()), 'duplicate or unordered archive members')
        require('manifest.json' in names, 'missing manifest.json')
        for info in infos:
            if info.filename != 'manifest.json':
                safe_path(info.filename, False)
            require(info.compress_type == zipfile.ZIP_STORED and info.compress_size == info.file_size,
                    'compressed archives unsupported')
            require(info.flag_bits in (0, 0x800), 'encrypted/streaming archive unsupported')
            require(info.create_system == 3 and info.external_attr == (stat.S_IFREG | 0o644) << 16,
                    'archive member is not canonical regular file')
            require(info.date_time == ZIP_TIME and not info.extra and not info.comment,
                    'noncanonical ZIP metadata')
        validate_zip_representation(path, infos)
        require(archive.getinfo('manifest.json').file_size <= MAX_MANIFEST_BYTES, 'manifest too large')
        data = archive.read('manifest.json')
        manifest = parse(data)
        require(data == canonical(manifest), 'manifest serialization is not canonical')
        result = validate_manifest(manifest, context, allow_fixture)
        entries = manifest['firmware']['artifacts'] + manifest['payload']['files']
        require(set(names) == {'manifest.json'} | {x['source'] for x in entries}, 'missing or unlisted container file')
        require(sum(i.file_size for i in infos) <= min(MAX_CONTAINER_BYTES, context['limits']['container_bytes']),
                'uncompressed inventory too large')
        for entry in entries:
            require(archive.getinfo(entry['source']).file_size == entry['bytes'], 'file length mismatch: ' + entry['source'])
            digest, length = hashlib.sha256(), 0
            with archive.open(entry['source']) as source:
                while True:
                    chunk = source.read(65536)
                    if not chunk:
                        break
                    digest.update(chunk)
                    length += len(chunk)
            require(length == entry['bytes'] and digest.hexdigest() == entry['sha256'],
                    'file SHA-256 mismatch: ' + entry['source'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('container', type=Path)
    parser.add_argument('--context', type=Path, required=True)
    parser.add_argument('--allow-fixture', action='store_true')
    args = parser.parse_args()
    try:
        context = parse(args.context.read_bytes())
        schema_check(context, 'context.schema.json')
        result = validate_container(args.container, context, args.allow_fixture)
        print(json.dumps(result, sort_keys=True))
    except (Invalid, OSError, zipfile.BadZipFile, UnicodeError, KeyError, ValueError) as exc:
        print('REJECT: ' + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
