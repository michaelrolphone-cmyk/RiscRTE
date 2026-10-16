#!/usr/bin/env python3
"""Verify frozen source, asset and conformance hashes supplied as local directories.
"""
import argparse
import hashlib
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for product in ('waveshare', 'twatch'):
        parser.add_argument('--' + product + '-runtime', type=Path)
        parser.add_argument('--' + product + '-assets', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    pins = json.loads((root / 'pins.json').read_text())
    evidence = json.loads((root / 'conformance.json').read_text())
    assert pins['contract_version'] == evidence['contract_version'] == '1.0.0'
    assert pins['wire_protocol'] == 'RTEF1'
    assert evidence['hardware'] == {
        'status': 'not_tested',
        'no_claims': ['USB open/close modem-line behavior', 'live device timing',
                      'physical media durability', 'X4 product admission']}
    runs = evidence['tests']['local_command_runs']
    assert len(runs) == evidence['tests']['local_command_run_count'] == 20
    for run in runs:
        assert run['exit_code'] == 0
        assert digest(root / run['log']) == run['log_sha256'], run['log']
        if run['sanitize'] == '1':
            assert run['leak_detection'] is False
    identities = []
    for product in pins['products']:
        name = product['product']
        assert product['hardware_tested'] is False
        assert product['admission']['hardware_tested'] is False
        assert product['admission']['active_boot_bank_writable'] is False
        assert product['admission']['application_grants_reused'] is False
        assert product['admission']['boot_export_enabled'] is False
        assert product['ci']['conclusion'] == 'success'
        assert product['ci']['head_sha'] == product['product_revision']
        assert len(product['assets']) == 16
        assert {n['id'] for n in product['admission']['namespaces']} == (
            {2, 12} if name == 'waveshare' else {2})
        selected = getattr(args, name + '_runtime')
        assets = getattr(args, name + '_assets')
        common = {}
        for source in product['sources']:
            if source['identical_across_delivered_revisions']:
                common[source['path']] = source['sha256']
            if selected:
                assert digest(selected / source['path']) == source['sha256'], source['path']
        identities.append(common)
        for asset in product['assets']:
            if assets:
                path = assets / asset['name']
                assert path.stat().st_size == asset['bytes'], asset['name']
                assert digest(path) == asset['sha256'], asset['name']
    assert identities[0] == identities[1]
    assert identities[0]['scripts/serial_files.py'] == pins['common_client']['sha256']
    print('PASS: contract 1.0.0 evidence, 20 conformance runs, 2 exact releases; hardware not tested')
    if any(vars(args).values()):
        print('PASS: all supplied source and release-asset directories match frozen SHA-256 pins')


if __name__ == '__main__':
    main()
