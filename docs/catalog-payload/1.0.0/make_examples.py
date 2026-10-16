#!/usr/bin/env python3
"""Build deterministic, synthetic fixtures. These are not X4/Wave/T-Watch releases."""
import argparse
import copy
from pathlib import Path
from validate import canonical, inventory_hash, pack, sha256


def fixture(kind='bootstrap'):
    firmware = b'NOT FIRMWARE: synthetic catalog schema fixture 1.0.0\n'
    contents = {'firmware/fixture.bin': firmware, 'payload/data.bin': bytes(range(256)),
                'payload/empty.txt': b'', 'payload/settings.json': b'{"theme":"blue"}\n'}
    evidence = {'url': 'https://example.invalid/fixture/evidence/' + '1' * 40,
                'sha256': sha256(b'synthetic evidence, no hardware qualification')}
    mode = 'replace' if kind == 'update-after' else 'create'
    files = [{'source': source, 'bytes': len(contents[source]), 'sha256': sha256(contents[source]),
              'volume': 'content', 'destination': destination, 'operation': mode, 'owner': 'fixture-app',
              'previous': ({'owner': 'fixture-app', 'bytes': 3, 'sha256': sha256(b'old')}
                           if mode == 'replace' else None)}
             for source, destination in [('payload/data.bin', '/example/data.bin'),
                                         ('payload/empty.txt', '/example/empty.txt'),
                                         ('payload/settings.json', '/example/settings.json')]]
    consumer = {'id': 'fixture-consumer', 'firmware_id': 'fixture-v2', 'evidence': copy.deepcopy(evidence)}
    manifest = {
        'schema_version': '1.0.0', 'container_format': 'risc-catalog-payload-zip-store-v1', 'fixture': True,
        'description': 'SYNTHETIC SCHEMA FIXTURE ONLY. Namespace 7 is invented for this test, not a product mapping.',
        'catalog_release': {'id': 'fixture:release-2', 'revision': 1, 'repository': 'fixture/RiscRTE-Example',
                            'release_id': '2', 'tag': 'fixture-v2', 'source_commit': '2' * 40},
        'product': {'id': 'fixture-board', 'hardware_profiles': ['fixture-profile'], 'hardware_revisions': ['rev-a'],
                    'storage_abi': 'RiscStorageFilesV1', 'wire_protocol': 'RTEF1', 'serial_contract_version': '1.0.0'},
        'firmware': {'id': 'fixture-v2', 'version': '2.0.0-fixture', 'source_commit': '2' * 40,
                     'artifacts': [{'id': 'fixture-image', 'source': 'firmware/fixture.bin',
                                    'bytes': len(firmware), 'sha256': sha256(firmware)}],
                     'install': {'method_id': 'fixture-flash-method', 'kind': 'factory' if kind == 'bootstrap' else 'update',
                                 'artifact_ids': ['fixture-image'],
                                 'volume_effects': [{'volume': 'content', 'effect': 'erased' if kind == 'bootstrap' else 'preserved',
                                                     'evidence': copy.deepcopy(evidence)}],
                                 'settings_effect': 'erased' if kind == 'bootstrap' else 'unknown',
                                 'erase_disclosure': ('Synthetic factory example erases existing device data and settings.'
                                                      if kind == 'bootstrap' else 'Synthetic update example preserves the content volume; settings survival is unknown.'),
                                 'evidence': copy.deepcopy(evidence)}},
        'payload': {'id': 'fixture-content-v2', 'owner': 'fixture-app', 'inventory_sha256': '0' * 64,
                    'volumes': [{'id': 'content', 'requirements': {'operations': ['stat', 'get', 'mkdir', mode],
                                                               'hierarchical': True, 'path_encoding': 'utf-8-exact', 'case_semantics': 'sensitive'},
                                 'consumer': consumer}],
                    'bindings': [{'volume': 'content', 'firmware_id': 'fixture-v2', 'namespace_id': '7', 'evidence': copy.deepcopy(evidence)}],
                    'directories': [{'volume': 'content', 'path': '/example', 'operation': 'ensure-directory'}],
                    'files': files, 'totals': {'file_count': 3, 'bytes': sum(f['bytes'] for f in files)}},
        'plan': {'purpose': 'bootstrap' if kind == 'bootstrap' else 'combined-update',
                 'order': ['payload', 'firmware', 'verify'] if kind.startswith('update-before') else ['firmware', 'payload', 'verify'],
                 'pre_firmware_id': None if kind == 'bootstrap' else 'fixture-v1',
                 'policy': {'verification': 'commit-crc32-then-full-sha256-readback',
                            'completion': 'all-required-files-readable-by-target-consumer',
                            'on_uncertain_result': 'stop-inspect-no-automatic-retry',
                            'on_partial_file': 'retain-and-reconcile-selected-file', 'unrelated_files': 'preserve',
                            'replacement': 'explicit-selected-file-in-place-nonatomic', 'rollback': 'none'}}}
    if kind.startswith('update-before'):
        manifest['payload']['bindings'].insert(0, {'volume': 'content', 'firmware_id': 'fixture-v1',
                                                  'namespace_id': '7', 'evidence': copy.deepcopy(evidence)})
    manifest['payload']['inventory_sha256'] = inventory_hash(manifest['payload'])
    context = {'context_version': '1.0.0', 'fixture': True, 'product_id': 'fixture-board',
               'hardware_profile': 'fixture-profile', 'hardware_revision': 'rev-a',
               'installed_firmware_id': manifest['plan']['pre_firmware_id'],
               'trusted_manifest_sha256': sha256(canonical(manifest)),
               'trusted_binding_sha256': [sha256(canonical(x)) for x in manifest['payload']['bindings']],
               'trusted_install_sha256': sha256(canonical(manifest['firmware']['install'])),
               'trusted_consumer_sha256': [sha256(canonical(consumer))], 'supported_methods': ['fixture-flash-method'],
               'namespaces': [{'firmware_id': stage, 'namespace_id': '7', 'writable': True, 'hierarchical': True,
                                'case_semantics': 'sensitive', 'operations': ['stat', 'get', 'mkdir', 'create', 'replace'],
                                'forbidden_characters': '', 'forbid_trailing_dot_space': False,
                                'max_path_bytes': 490, 'max_component_bytes': 255,
                                'max_file_bytes': 1048576, 'write_budget_bytes': 1048576}
                               for stage in (['fixture-v1', 'fixture-v2'] if kind.startswith('update-before') else ['fixture-v2'])],
               'limits': {'container_bytes': 1048576, 'inventory_files': 100}}
    if kind == 'update-before-readonly':
        context['namespaces'][-1].update(writable=False, operations=['stat', 'get'], write_budget_bytes=0)
    if kind == 'flat-root':
        manifest['plan']['purpose'] = 'bootstrap'
        manifest['plan']['pre_firmware_id'] = None
        context['installed_firmware_id'] = None
        manifest['payload']['directories'] = []
        manifest['payload']['volumes'][0]['requirements'].update(hierarchical=False, operations=['stat', 'get', 'create'])
        for file in manifest['payload']['files']:
            file['destination'] = '/' + file['destination'].split('/')[-1]
        context['namespaces'][0].update(hierarchical=False, operations=['stat', 'get', 'create'])
        manifest['payload']['inventory_sha256'] = inventory_hash(manifest['payload'])
        context['trusted_manifest_sha256'] = sha256(canonical(manifest))
    context['trusted_container_sha256'] = sha256(pack(manifest, contents))
    return manifest, context, contents


def write_examples(output):
    for kind in ('bootstrap', 'update-after', 'update-before', 'update-before-readonly', 'flat-root'):
        manifest, context, contents = fixture(kind)
        dest = output / kind
        dest.mkdir(parents=True, exist_ok=True)
        data = canonical(manifest)
        archive = pack(manifest, contents)
        (dest / 'manifest.json').write_bytes(data)
        (dest / 'context.json').write_bytes(canonical(context))
        (dest / 'container.zip').write_bytes(archive)
        descriptor = {'schema_version': '1.0.0',
                      'manifest': {'asset_name': kind + '-manifest.json', 'bytes': len(data), 'sha256': sha256(data)},
                      'container': {'asset_name': kind + '-container.zip', 'bytes': len(archive), 'sha256': sha256(archive)}}
        (dest / 'descriptor.json').write_bytes(canonical(descriptor))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    write_examples(parser.parse_args().output)
