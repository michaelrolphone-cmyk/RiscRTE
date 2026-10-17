#!/usr/bin/env python3
"""Validate immutable-payload selections and track their guarded execution state."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import zipfile
from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parent
PAYLOAD = ROOT.parents[1] / 'catalog-payload' / '1.0.0'
spec = importlib.util.spec_from_file_location('catalog_payload_v1', PAYLOAD / 'validate.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
Invalid, canonical, sha256, require = base.Invalid, base.canonical, base.sha256, base.require
CLIENT_ROOT = ROOT.parents[2] / 'scripts'
sys.path.insert(0, str(CLIENT_ROOT))
import serial_files


def schema(value, name):
    require(len(canonical(value)) <= base.MAX_MANIFEST_BYTES, 'metadata exceeds bound')
    error = next(Draft202012Validator(json.loads((ROOT / name).read_text())).iter_errors(value), None)
    require(error is None, name + ': ' + (error.message if error else ''))


def key(row):
    return row['volume'], row.get('destination', row.get('path'))


class Payload:
    """Validated immutable input; original pins and operation fields stay unchanged."""
    def __init__(self, container, context, allow_fixture=False):
        base.schema_check(context, 'context.schema.json')
        with zipfile.ZipFile(container) as archive:
            require(archive.getinfo('manifest.json').file_size <= base.MAX_MANIFEST_BYTES, 'manifest too large')
            self.manifest = base.parse(archive.read('manifest.json'))
        base.schema_check(self.manifest, 'manifest.schema.json')
        self.context = copy.deepcopy(context)
        base.validate_container(container, self._structural_context(), allow_fixture)
        self.files = self.manifest['payload']['files']
        self.parents = self.manifest['payload']['directories']
        self.by_key = {key(row): row for row in self.files}
        self.transfer_firmware = (self.manifest['plan']['pre_firmware_id']
                                  if self.manifest['plan']['order'][0] == 'payload'
                                  else self.manifest['firmware']['id'])
        self.target_firmware = self.manifest['firmware']['id']

    def binding(self, firmware, session):
        require(firmware in {b['firmware_id'] for b in self.manifest['payload']['bindings']},
                'firmware has no trusted binding')
        return {'manifest_sha256': self.context['trusted_manifest_sha256'],
                'container_sha256': self.context['trusted_container_sha256'],
                'inventory_sha256': self.manifest['payload']['inventory_sha256'],
                'context_sha256': sha256(canonical(self.context)),
                'firmware_id': firmware, 'session': session}

    def namespace(self, firmware, volume):
        binding = next((b for b in self.manifest['payload']['bindings']
                        if b['firmware_id'] == firmware and b['volume'] == volume), None)
        require(binding is not None, 'no trusted firmware/volume binding')
        return binding['namespace_id']

    def profile(self, firmware, volume):
        ns = self.namespace(firmware, volume)
        return next(n for n in self.context['namespaces']
                    if n['firmware_id'] == firmware and n['namespace_id'] == ns)

    def _structural_context(self):
        # Frozen v1 combines immutable inventory checks with whole-plan execution
        # limits. Selection defers ONLY those limits to the selected rows. The
        # private view is never an admitted capability record or an output.
        view = copy.deepcopy(self.context)
        for ns in view['namespaces']:
            ns.update(writable=True, hierarchical=True,
                      operations=['stat', 'get', 'mkdir', 'create', 'replace'],
                      write_budget_bytes=9007199254740991,
                      max_file_bytes=9007199254740991,
                      max_path_bytes=511, max_component_bytes=511,
                      forbidden_characters='', forbid_trailing_dot_space=False)
        return view

    def check_immutable(self):
        base.schema_check(self.context, 'context.schema.json')
        base.validate_manifest(self.manifest, self._structural_context(), self.manifest['fixture'])


def row_key(payload, row, firmware):
    return {'volume': row['volume'], 'destination': key(row)[1],
            'namespace_id': payload.namespace(firmware, row['volume'])}


def validate_observation(payload, observation):
    payload.check_immutable()
    schema(observation, 'observation.schema.json')
    binding = observation['binding']
    firmware, session = binding['firmware_id'], binding['session']
    require(binding == payload.binding(firmware, session), 'immutable binding changed')
    namespaces = observation['namespaces']
    ids = [n['namespace_id'] for n in namespaces]
    require(len(ids) == len(set(ids)) and all(int(n) <= 2**64 - 1 for n in ids), 'ambiguous namespace map')
    for name, source in [('files', payload.files), ('parents', payload.parents)]:
        rows = observation[name]
        require([key(r) for r in rows] == [key(r) for r in source], 'incomplete or reordered ' + name)
        for row, original in zip(rows, source):
            require({k: row[k] for k in ('volume', 'destination', 'namespace_id')} ==
                    row_key(payload, original, firmware), 'destination or namespace changed')
            current = row['current']
            if current['kind'] in ('file', 'directory'):
                require(current['flags'] <= 0xffffffff, 'invalid STAT flags')
                require(bool(current['flags'] & 1) == (current['kind'] == 'directory'), 'STAT type disagrees')
                if current['kind'] == 'file':
                    require(current['bytes'] <= payload.profile(firmware, row['volume'])['max_file_bytes'],
                            'current file exceeds admitted bound')
    return observation


def make_selection(payload, observation, choices):
    """choices are explicit UI decisions in immutable inventory order, not inferred modes."""
    validate_observation(payload, observation)
    require(len(choices) == len(payload.files), 'one explicit decision per inventory row required')
    result = {'selection_version': '1.0.0', 'mode': 'explicit-current-file-selection',
              'binding': copy.deepcopy(observation['binding']),
              'observation_sha256': sha256(canonical(observation)), 'files': []}
    for row, choice in zip(observation['files'], choices):
        require(set(choice) == {'action', 'consent'}, 'choice requires action and consent')
        result['files'].append(dict(copy.deepcopy(row), **choice))
    validate_selection(payload, observation, result)
    return result


def current_valid(current):
    definition = json.loads((ROOT / 'observation.schema.json').read_text())['properties']['files']['items']['properties']['current']
    return Draft202012Validator(definition).is_valid(current) and current.get('status') == 0 and not current.get('flags', 1) & 1


class HashSink:
    def __init__(self, limit):
        self.limit, self.size, self.digest = limit, 0, hashlib.sha256()

    def write(self, data):
        require(self.size + len(data) <= self.limit, 'read exceeds admitted file bound')
        self.size += len(data)
        self.digest.update(data)
        return len(data)


def observe_current(client, namespace, path, max_file_bytes):
    """Read through the existing client. Only a STAT NOT_FOUND becomes absence.

    Caller has already verified admission. A complete GET/READ/CLOSE is required
    for a regular-file hash, including empty files.
    """
    try:
        info = client.stat(int(namespace), path)
    except serial_files.RemoteError as error:
        if error.command == 'STAT' and error.code == 1:
            return {'kind': 'absent', 'status': 1}
        raise
    if info.directory:
        return {'kind': 'directory', 'status': 0, 'flags': info.flags}
    require(info.size <= max_file_bytes, 'current file exceeds admitted bound')
    sink = HashSink(max_file_bytes)
    size = client.get_stream(int(namespace), path, sink)
    after = client.stat(int(namespace), path)
    require(not after.directory and size == sink.size == info.size == after.size,
            'current file changed during observation')
    return {'kind': 'file', 'status': 0, 'flags': after.flags,
            'bytes': sink.size, 'sha256': sink.digest.hexdigest()}


def observe(payload, client, firmware):
    """Read-only snapshot adapter, for an already-owned client at an existing stage.

    Definitive errors remain errors; uncertain/unread observations cannot authorize
    a transfer. The builder supplies proven firmware identity through its current
    product flow, because RTEF1 has no firmware-identity command.
    """
    require(client.usable, 'session unusable')
    session = client.session
    namespaces = client.namespaces()
    ids = [str(n.id) for n in namespaces]
    require(len(ids) == len(set(ids)), 'duplicate actual namespaces')
    result = {'observation_version': '1.0.0', 'binding': payload.binding(firmware, session),
              'session_usable': True,
              'namespaces': [{'namespace_id': str(n.id), 'writable': n.writable} for n in namespaces],
              'files': [], 'parents': []}
    for group, rows in [('parents', payload.parents), ('files', payload.files)]:
        for entry in rows:
            row = row_key(payload, entry, firmware)
            require(row['namespace_id'] in ids, 'trusted namespace missing from actual admission')
            if not result['session_usable'] or not client.usable:
                current = {'kind': 'uncertain'}
            else:
                try:
                    current = observe_current(client, row['namespace_id'], row['destination'],
                                              payload.profile(firmware, row['volume'])['max_file_bytes'])
                except serial_files.RemoteError as error:
                    # Only the STAT helper above may interpret status 1 as absence.
                    if error.code in (0, 1):
                        current = {'kind': 'uncertain'}
                        result['session_usable'] = False
                    else:
                        current = {'kind': 'error', 'status': error.code}
                except (serial_files.FileTransferError, Invalid, OSError):
                    current = {'kind': 'uncertain'}
                    result['session_usable'] = False
            result[group].append(dict(row, current=current))
    result['session_usable'] = result['session_usable'] and client.usable and client.session == session
    return validate_observation(payload, result)


def matching_bytes(current, entry):
    return current['kind'] == 'file' and all(current[k] == entry[k] for k in ('bytes', 'sha256'))


def legacy_guards(entry, current):
    """Informational only in explicit-current mode; never claim old guards passed."""
    guards = entry.get('previous') or {}
    if not guards:
        return 'not-declared'
    if 'owner' in guards:
        return 'owner-not-established-by-RTEF1'
    return 'match' if all(current.get(k) == v for k, v in guards.items()) else 'different'


def validate_selection(payload, observation, selection):
    validate_observation(payload, observation)
    schema(selection, 'selection.schema.json')
    require(selection['binding'] == observation['binding'], 'selection binding changed')
    require(selection['observation_sha256'] == sha256(canonical(observation)), 'observation changed')
    require(observation['session_usable'], 'session unusable')
    require(len(selection['files']) == len(payload.files), 'incomplete selected inventory')
    firmware = selection['binding']['firmware_id']
    actual = {n['namespace_id']: n for n in observation['namespaces']}
    parents = {key(r): r for r in observation['parents']}
    counts = {action: 0 for action in ('create', 'replace-observed', 'verify-equal', 'skip')}
    reports, selected_bytes = [], {}
    for entry, observed, row in zip(payload.files, observation['files'], selection['files']):
        require({k: row[k] for k in observed} == observed, 'selected current bytes or destination changed')
        action, current = row['action'], row['current']
        require(row['consent'] == (action == 'replace-observed'), 'explicit replacement consent required')
        counts[action] += 1
        reports.append({'volume': entry['volume'], 'destination': entry['destination'],
                        'action': action, 'legacy_operation': entry['operation'],
                        'legacy_previous': copy.deepcopy(entry.get('previous')),
                        'legacy_guard_comparison': legacy_guards(entry, current)})
        if action == 'skip':
            continue
        ns = actual.get(row['namespace_id'])
        require(ns is not None, 'selected namespace not actually admitted')
        profile = payload.profile(firmware, entry['volume'])
        need = {'stat', 'get'}
        selected_paths = [entry['destination']] + base.parent_paths(entry['destination'])
        require(profile['hierarchical'] or len(selected_paths) == 1, 'selected provider is not hierarchical')
        for path in selected_paths:
            components = base.safe_path(path, True)
            require(len(path.encode('utf-8')) <= profile['max_path_bytes'], 'selected path exceeds provider limit')
            require(all(len(c.encode('utf-8')) <= profile['max_component_bytes'] for c in components),
                    'selected component exceeds provider limit')
            require(not any(c in path for c in profile['forbidden_characters']), 'selected path has forbidden character')
            require(not profile['forbid_trailing_dot_space'] or not any(c.endswith((' ', '.')) for c in components),
                    'selected path has forbidden suffix')
        require(entry['bytes'] <= profile['max_file_bytes'], 'selected source exceeds provider file bound')
        if action in ('create', 'replace-observed'):
            require(firmware == payload.transfer_firmware, 'wrong transfer firmware stage')
            require(ns['writable'] and profile['writable'], 'selected namespace is read-only')
            need.add('replace' if action == 'replace-observed' else 'create')
            selected_bytes[entry['volume']] = selected_bytes.get(entry['volume'], 0) + entry['bytes']
        if action == 'create':
            require(current['kind'] == 'absent' and current['status'] == 1, 'create requires authoritative absence')
        elif action == 'replace-observed':
            require(current['kind'] == 'file', 'replacement requires observed current regular bytes')
        elif action == 'verify-equal':
            require(matching_bytes(current, entry), 'unchanged file differs from payload')
        for path in base.parent_paths(entry['destination']):
            parent = parents[(entry['volume'], path)]['current']
            if action in ('create', 'replace-observed') and parent['kind'] == 'absent':
                need.add('mkdir')
            else:
                require(parent['kind'] == 'directory', 'selected parent is not an available directory')
        require(need <= set(profile['operations']), 'selected operation unsupported by trusted provider')
    for volume, size in selected_bytes.items():
        require(size <= payload.profile(firmware, volume)['write_budget_bytes'], 'selected bytes exceed write budget')
    return {'status': 'selection-validated-not-executed', 'counts': counts,
            'selected_files': len(payload.files) - counts['skip'], 'inventory_files': len(payload.files),
            'rows': reports, 'mutation_performed': False}


class SelectionRun:
    """One owner-held in-memory run. Serialized selections are review receipts only.

    Recheck returns one file instruction; the builder owns transport and execution.
    No instruction survives a changed owner object, session, known removal or failure.
    """
    def __init__(self, payload, observation, selection, owner):
        self.summary = validate_selection(payload, observation, selection)
        require(owner.usable and owner.session == selection['binding']['session'], 'live owner/session mismatch')
        self.payload, self.owner = payload, owner
        self.selection = copy.deepcopy(selection)
        self.selection_digest = sha256(canonical(selection))
        self.binding = copy.deepcopy(selection['binding'])
        self.states = ['skipped' if r['action'] == 'skip' else 'pending' for r in selection['files']]
        self.active, self.stop_reason = None, None
        self.readbacks = [None] * len(self.states)
        self.acknowledged = [0] * len(self.states)
        self.final_inventory_verified = False

    def invalidate(self, reason):
        require(bool(reason), 'invalidation reason required')
        self.stop_reason = reason
        if self.active is not None:
            self.states[self.active] = 'uncertain'
            self.active = None
        self.final_inventory_verified = False

    def _live(self, owner):
        require(self.stop_reason is None, 'run stopped: ' + str(self.stop_reason))
        if owner is not self.owner or not owner.usable or owner.session != self.binding['session']:
            self.invalidate('connection or session changed')
            raise Invalid('connection or session changed')
        require(sha256(canonical(self.selection)) == self.selection_digest, 'selection changed after consent')
        self.payload.check_immutable()

    def recheck(self, index, fresh, owner):
        try:
            return self._recheck(index, fresh, owner)
        except Invalid:
            self.invalidate('pre-mutation recheck rejected')
            raise

    def _recheck(self, index, fresh, owner):
        """Full selected-file/parent preflight; call immediately before this file's PUT.

        Fresh observations must be gathered under the same exclusive owner. Full-file
        hashes are necessary. This is not atomic compare-and-swap or a media UUID.
        """
        self._live(owner)
        require(type(index) is int and 0 <= index < len(self.states), 'invalid file index')
        require(self.active is None and self.states[index] == 'pending', 'file already claimed or not pending')
        validate_observation(self.payload, fresh)
        require(fresh['binding'] == self.binding and fresh['session_usable'], 'fresh binding/session changed')
        # Check all not-yet-started selected files first, so a known later conflict
        # never authorizes an earlier mutation. Verified files are checked separately.
        adjusted = copy.deepcopy(self.selection)
        adjusted['observation_sha256'] = sha256(canonical(fresh))
        for i, row in enumerate(adjusted['files']):
            if self.states[i] == 'pending':
                require(row['current'] == fresh['files'][i]['current'], 'current-file guard changed')
            else:
                row.update(action='skip', consent=False, current=copy.deepcopy(fresh['files'][i]['current']))
        validate_selection(self.payload, fresh, adjusted)
        row = self.selection['files'][index]
        if row['action'] == 'verify-equal':
            self.states[index] = 'verified'
            return None
        self.final_inventory_verified = False
        self.states[index], self.active = 'claimed', index
        entry = self.payload.files[index]
        return {'namespace_id': row['namespace_id'], 'destination': row['destination'],
                'operation': 'replace' if row['action'] == 'replace-observed' else 'create',
                'source': entry['source'], 'bytes': entry['bytes'], 'sha256': entry['sha256']}

    def progress(self, index, acknowledged_bytes):
        require(self.active == index and self.states[index] == 'claimed', 'no matching claimed write')
        require(type(acknowledged_bytes) is int and self.acknowledged[index] <= acknowledged_bytes <= self.payload.files[index]['bytes'],
                'invalid acknowledged progress')
        self.acknowledged[index] = acknowledged_bytes

    def finish(self, index, outcome, readback=None, commit_acknowledged=False):
        try:
            return self._finish(index, outcome, readback, commit_acknowledged)
        except Invalid:
            self.invalidate('completion not verified')
            raise

    def _finish(self, index, outcome, readback=None, commit_acknowledged=False):
        require(self.active == index and self.states[index] == 'claimed', 'no matching claimed write')
        require(outcome in ('verified', 'failed-known', 'uncertain'), 'unknown outcome')
        if outcome == 'verified':
            require(self.owner.usable and self.owner.session == self.binding['session'], 'session lost before completion')
            require(commit_acknowledged is True, 'COMMIT acknowledgment required')
            require(readback is not None and current_valid(readback) and matching_bytes(readback, self.payload.files[index]),
                    'full acknowledged readback does not match')
            self.readbacks[index] = copy.deepcopy(readback)
            self.acknowledged[index] = self.payload.files[index]['bytes']
            self.states[index], self.active = 'verified', None
        else:
            self.final_inventory_verified = False
            self.states[index], self.active = outcome, None
            self.stop_reason = outcome

    def verify_final_inventory(self, final_observation):
        self.final_inventory_verified = False
        validate_observation(self.payload, final_observation)
        require(self.stop_reason is None and self.active is None, 'run is stopped or unfinished')
        require(final_observation['session_usable'], 'final session unusable')
        require(final_observation['binding']['firmware_id'] == self.payload.target_firmware,
                'final inventory requires target firmware')
        actual = {n['namespace_id'] for n in final_observation['namespaces']}
        self.final_inventory_verified = all(r['namespace_id'] in actual and matching_bytes(r['current'], f)
                                            for r, f in zip(final_observation['files'], self.payload.files))
        return self.final_inventory_verified

    def receipt(self):
        selected = [s for s in self.states if s != 'skipped']
        rows = []
        for i, (choice, entry) in enumerate(zip(self.selection['files'], self.payload.files)):
            rows.append({'volume': entry['volume'], 'namespace_id': choice['namespace_id'],
                         'destination': entry['destination'], 'action': choice['action'],
                         'observed_before': copy.deepcopy(choice['current']),
                         'expected': {'bytes': entry['bytes'], 'sha256': entry['sha256']},
                         'acknowledged_bytes': self.acknowledged[i],
                         'readback': copy.deepcopy(self.readbacks[i]), 'outcome': self.states[i]})
        return {'files': rows, 'selection_sha256': self.selection_digest, 'binding': copy.deepcopy(self.binding),
                'states': list(self.states), 'stopped': self.stop_reason,
                'selected_files_verified': self.stop_reason is None and bool(selected) and all(s == 'verified' for s in selected),
                'all_inventory_bytes_verified_after_final_firmware': self.final_inventory_verified,
                'target_consumer_usability': 'not-established-by-this-reference'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('container', type=Path)
    parser.add_argument('--context', required=True, type=Path)
    parser.add_argument('--observation', required=True, type=Path)
    parser.add_argument('--selection', required=True, type=Path)
    parser.add_argument('--allow-fixture', action='store_true')
    args = parser.parse_args()
    try:
        payload = Payload(args.container, base.parse(args.context.read_bytes()), args.allow_fixture)
        result = validate_selection(payload, base.parse(args.observation.read_bytes()), base.parse(args.selection.read_bytes()))
        print(canonical(result).decode(), end='')
    except (Invalid, OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        print('REJECT: ' + str(error), file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
