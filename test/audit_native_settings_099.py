#!/usr/bin/env python3
"""Read-only authority inventory for the frozen X4 .99 persistence transition."""
import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bundle', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
bundle = a.bundle.resolve()
receipt_path = bundle / 'receipts/memory-composition.json'
receipt = json.loads(receipt_path.read_bytes())
assert receipt['product_version'] == '0.1.99'
assert receipt['product_source'] == '199e231d962c401c5d20617e2f5e4da07bffd780'
assert receipt['runtime_commit'] == '1c1ed8661ea30d47e7e9707c41d492ce3a2b4d70'
assert receipt['component_sources']['system']['commit'] == '49752a02746b38afbcc403bb92189be5a8356c9d'
store = bundle / 'store'
sha = lambda data: hashlib.sha256(data).hexdigest()
for name, expected in receipt['store'].items():
    path = (store / name).resolve()
    assert path.is_relative_to(store.resolve())
    assert sha(path.read_bytes()) == expected, name
boot = json.loads((store / 'boot.json').read_bytes())
assert len(boot['app_capabilities']) == 27 and len(boot['drivers']) == 33
assert boot['installation']['settings_sd'] is True
assert 'shared_settings' not in boot['installation']
apps = []
for policy in boot['app_capabilities']:
    manifest = json.loads((store / policy['manifest']).read_bytes())
    selected = [g for g in policy['grants'] if g['capability'] == 'storage.key-value'
                and g['instance_id'] in (1, 6)]
    assert not any(g['capability'] == 'storage.key-value.atomic' for g in policy['grants'])
    if selected:
        apps.append({'id': manifest['id'], 'manifest': policy['manifest'],
                     'version': manifest['version'], 'grants': selected,
                     'manifest_sha256': receipt['store'][policy['manifest']],
                     'authority': 'KV1/KV2 grant permits reads and writes; this is not proof every app writes'})
providers = []
for policy in boot['drivers']:
    rows = [row for row in policy.get('key_value', []) if row['namespace'] in (1, 6)]
    if rows:
        manifest = json.loads((store / policy['manifest']).read_bytes())
        providers.append({'id': manifest['id'], 'manifest': policy['manifest'],
                          'bindings': rows, 'manifest_sha256': receipt['store'][policy['manifest']]})
counts = {str(ns): sum(any(g['instance_id'] == ns for g in app['grants']) for app in apps)
          for ns in (1, 6)}
assert counts == {'1': 20, '6': 5}
assert len(providers) == 2 and all(row['access'] == 'read' for provider in providers for row in provider['bindings'])
result = {
    'status': 'Frozen .99 authority inventory only; native persistence remains unselected',
    'product_source': receipt['product_source'], 'runtime_source': receipt['runtime_commit'],
    'system_source': receipt['component_sources']['system']['commit'],
    'receipt_sha256': sha(receipt_path.read_bytes()), 'boot_sha256': receipt['store']['boot.json'],
    'verified_store_files': len(receipt['store']), 'app_count': 27, 'provider_count': 33,
    'installed_settings': {'settings_sd': True, 'namespace_range': boot['installation']['namespace_range']},
    'shared_settings_selected': False, 'atomic_capability_selected': False,
    'app_authority_counts': counts, 'apps': apps, 'bound_provider_readers': providers,
    'required_integration': [
        'Exact namespace-6 Core read-through for Files, Wi-Fi, OTA, App Store and Daily Digest',
        'Atomic Wi-Fi controller publication plus same-Core reads, lifecycle and namespace serialization',
        'Namespace-1 app and bound-provider access must share the same admitted native Core when selected',
        'Legacy puts cannot silently bypass a selected atomic namespace or invent user intents',
        'Read/write grant inventory is an authority upper bound, not a semantic writer audit',
        'Retain old native and SD datasets and report unresolved conflicting source selection',
        'Do not apply the X4 SD policy to Watch'
    ]
}
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps(result, indent=2) + '\n')
print('X4 .99 inventory PASS: 106 verified store files; 27 apps, 33 providers; namespace 1: 20 app grants / 2 read-only providers; namespace 6: 5 app grants')
