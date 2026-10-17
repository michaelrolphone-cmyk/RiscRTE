#!/usr/bin/env python3
"""Deterministic selection, lifecycle and actual Python-client adapter conformance."""
import copy
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
import zlib
from jsonschema import Draft202012Validator
from fixtures import payload_at, observation, choices_for, file_state, source
from make_examples import write_examples
from make_schemas import schemas
from selection import (ROOT, Payload, SelectionRun, Invalid, base, canonical, make_selection,
                       validate_observation, validate_selection, observe_current, observe, serial_files)


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.payload, self.contents = payload_at(self.temp.name)
        self.obs = observation(self.payload, self.contents)
        self.owner = SimpleNamespace(session=0x12345678, usable=True)

    def plan(self, obs=None, choices=None):
        obs = obs or self.obs
        return make_selection(self.payload, obs, choices or choices_for(self.payload, obs))

    def run_for(self, obs=None, choices=None):
        obs = obs or self.obs
        return SelectionRun(self.payload, obs, self.plan(obs, choices), self.owner)

    def test_schema_meta_and_source_generated_bytes(self):
        for name, value in schemas():
            Draft202012Validator.check_schema(value)
            self.assertEqual(json.loads((ROOT / name).read_text()), value)
        for name in ('a', 'b'):
            write_examples(Path(self.temp.name) / name)
        for a in (Path(self.temp.name) / 'a').glob('*.json'):
            self.assertEqual(a.read_bytes(), (Path(self.temp.name) / 'b' / a.name).read_bytes())
            self.assertEqual(a.read_bytes(), (ROOT / 'examples' / a.name).read_bytes())

    def test_all_positive_vectors(self):
        for name in ('clean', 'mixed', 'all-existing', 'all-equal'):
            with self.subTest(name=name):
                obs = observation(self.payload, self.contents, name)
                result = validate_selection(self.payload, obs, self.plan(obs))
                self.assertEqual(result['selected_files'], 3)
                self.assertFalse(result['mutation_performed'])

    def test_exact_45_inventory_three_guarded_plus_42_create_shape(self):
        p, contents = payload_at(self.temp.name, count=45)
        obs = observation(p, contents, 'clean')
        for i in range(3):
            obs['files'][i]['current'] = file_state(b'old')
        selected = make_selection(p, obs, choices_for(p, obs))
        result = validate_selection(p, obs, selected)
        self.assertEqual(result['counts'], {'create': 42, 'replace-observed': 3, 'verify-equal': 0, 'skip': 0})
        self.assertEqual(len(selected['files']), 45)

    def test_customized_file_can_explicitly_replace_legacy_predecessor(self):
        p, contents = payload_at(self.temp.name, 'update-after')
        before = canonical(p.manifest)
        obs = observation(p, contents, 'all-existing')
        selected = make_selection(p, obs, choices_for(p, obs))
        result = validate_selection(p, obs, selected)
        self.assertTrue(all(r['legacy_guard_comparison'] == 'owner-not-established-by-RTEF1' for r in result['rows']))
        self.assertEqual(before, canonical(p.manifest))
        self.assertEqual(p.files[0]['previous']['sha256'], base.sha256(b'old'))

    def test_legacy_guards_not_reported_as_passed_for_customized_content(self):
        p, contents = payload_at(self.temp.name, count=45)
        obs = observation(p, contents, 'all-existing')
        result = validate_selection(p, obs, make_selection(p, obs, choices_for(p, obs)))
        self.assertEqual(result['rows'][0]['legacy_guard_comparison'], 'different')

    def test_consent_cannot_be_omitted_or_applied_to_other_action(self):
        for value in (False, None, 'yes', 1):
            plan = self.plan()
            plan['files'][0]['consent'] = value
            with self.subTest(value=value), self.assertRaises(Invalid):
                validate_selection(self.payload, self.obs, plan)
        choices = choices_for(self.payload, self.obs)
        choices[2]['consent'] = True
        with self.assertRaises(Invalid):
            self.plan(choices=choices)

    def test_selection_and_observation_have_complete_exact_inventory(self):
        for target in ('files', 'parents'):
            obs = copy.deepcopy(self.obs)
            obs[target] = []
            with self.subTest(target=target), self.assertRaises(Invalid):
                self.plan(obs)
        for mutate in (lambda p: p['files'].pop(), lambda p: p['files'].reverse(),
                       lambda p: p['files'].append(copy.deepcopy(p['files'][0]))):
            p = self.plan(); mutate(p)
            with self.assertRaises(Invalid):
                validate_selection(self.payload, self.obs, p)

    def test_every_immutable_binding_dimension_is_checked(self):
        for field in ('manifest_sha256', 'container_sha256', 'inventory_sha256', 'context_sha256', 'firmware_id', 'session'):
            p = self.plan()
            p['binding'][field] = 99 if field == 'session' else '0' * 64
            with self.subTest(field=field), self.assertRaises(Invalid):
                validate_selection(self.payload, self.obs, p)

    def test_unknown_fields_versions_modes_and_actions_fail_closed(self):
        for field, value in [('selection_version', '2.0.0'), ('mode', 'upsert'), ('extra', True)]:
            p = self.plan(); p[field] = value
            with self.subTest(field=field), self.assertRaises(Invalid):
                validate_selection(self.payload, self.obs, p)
        p = self.plan(); p['files'][0]['action'] = 'delete'
        with self.assertRaises(Invalid):
            validate_selection(self.payload, self.obs, p)

    def test_paths_and_namespace_cannot_be_redirected(self):
        for field, value in [('namespace_id', '12'), ('destination', '/other'), ('volume', 'other')]:
            p = self.plan(); p['files'][0][field] = value
            with self.subTest(field=field), self.assertRaises(Invalid):
                validate_selection(self.payload, self.obs, p)

    def test_only_not_found_can_authorize_create(self):
        for current in [{'kind': 'error', 'status': code} for code in [-11, -10, -9, -7, -6, -5, -4, -3, -2, -1, 2, 3, 99]] + [{'kind': 'uncertain'}, file_state(b'')]:
            obs = copy.deepcopy(self.obs); obs['files'][2]['current'] = current
            choices = choices_for(self.payload, self.obs)
            with self.subTest(current=current), self.assertRaises(Invalid):
                self.plan(obs, choices)

    def test_not_found_with_error_kind_is_rejected(self):
        obs = copy.deepcopy(self.obs); obs['files'][2]['current'] = {'kind': 'error', 'status': 1}
        with self.assertRaises(Invalid):
            self.plan(obs)

    def test_directory_is_not_replacement_file(self):
        obs = copy.deepcopy(self.obs); obs['files'][0]['current'] = {'kind': 'directory', 'flags': 1, 'status': 0}
        with self.assertRaises(Invalid):
            self.plan(obs)

    def test_same_size_different_hash_and_zero_length_are_distinct(self):
        run = self.run_for()
        changed = copy.deepcopy(self.obs)
        changed['files'][0]['current']['sha256'] = 'f' * 64
        with self.assertRaisesRegex(Invalid, 'guard changed'):
            run.recheck(0, changed, self.owner)
        self.assertIsNotNone(run.stop_reason)
        with self.assertRaises(Invalid):
            run.recheck(0, self.obs, self.owner)
        self.assertEqual(self.obs['files'][1]['current']['bytes'], 0)
        self.assertIsNone(self.run_for().recheck(1, self.obs, self.owner))

    def test_absent_becomes_present_and_existing_disappears(self):
        for i, current in [(2, file_state(b'new outsider')), (0, {'kind': 'absent', 'status': 1})]:
            run = self.run_for(); changed = copy.deepcopy(self.obs); changed['files'][i]['current'] = current
            with self.subTest(i=i), self.assertRaises(Invalid):
                run.recheck(0, changed, self.owner)

    def test_known_later_conflict_blocks_first_write(self):
        run = self.run_for(); changed = copy.deepcopy(self.obs)
        changed['files'][2]['current'] = file_state(b'new file')
        with self.assertRaises(Invalid):
            run.recheck(0, changed, self.owner)
        self.assertNotIn('claimed', run.states)

    def test_parent_file_error_and_changed_binding_block_writes(self):
        for current in (file_state(b'parent file'), {'kind': 'error', 'status': -3}, {'kind': 'uncertain'}):
            obs = copy.deepcopy(self.obs); obs['parents'][0]['current'] = current
            with self.subTest(current=current), self.assertRaises(Invalid):
                self.plan(obs)
        obs = copy.deepcopy(self.obs); obs['namespaces'][0]['writable'] = False
        with self.assertRaises(Invalid):
            self.plan(obs)
        obs = copy.deepcopy(self.obs); obs['namespaces'] = []
        with self.assertRaises(Invalid):
            self.plan(obs)

    def test_missing_parent_is_only_ensure_directory_and_no_extra_parent(self):
        obs = copy.deepcopy(self.obs)
        obs['parents'][0]['current'] = {'kind': 'absent', 'status': 1}
        choices = choices_for(self.payload, obs); choices[1] = {'action': 'skip', 'consent': False}
        self.plan(obs, choices)
        obs['parents'].append({'volume': 'content', 'destination': '/unrelated', 'namespace_id': '7', 'current': {'kind': 'absent', 'status': 1}})
        with self.assertRaises(Invalid):
            self.plan(obs, choices)

    def test_provider_must_support_newly_selected_replace(self):
        # Valid base is create-only; dynamic replace cannot borrow its requirements.
        m, c, contents = source(); c['namespaces'][0]['operations'].remove('replace')
        path = Path(self.temp.name) / 'limited.zip'; path.write_bytes(base.pack(m, contents))
        p = Payload(path, c, True); obs = observation(p, contents)
        with self.assertRaisesRegex(Invalid, 'unsupported'):
            make_selection(p, obs, choices_for(p, obs))

    def test_equal_selection_never_returns_mutation_instruction(self):
        obs = observation(self.payload, self.contents, 'all-equal'); run = self.run_for(obs)
        for i in range(3):
            self.assertIsNone(run.recheck(i, obs, self.owner))
        self.assertTrue(run.receipt()['selected_files_verified'])
        self.assertFalse(run.receipt()['all_inventory_bytes_verified_after_final_firmware'])
        self.assertTrue(run.verify_final_inventory(obs))

    def test_skip_decline_subset_completion_is_not_payload_completion(self):
        choices = choices_for(self.payload, self.obs, skipped=(0, 2))
        run = self.run_for(choices=choices); run.recheck(1, self.obs, self.owner)
        receipt = run.receipt()
        self.assertEqual(receipt['states'], ['skipped', 'verified', 'skipped'])
        self.assertTrue(receipt['selected_files_verified'])
        self.assertFalse(run.verify_final_inventory(self.obs))
        self.assertEqual(receipt['target_consumer_usability'], 'not-established-by-this-reference')

    def test_all_skipped_is_not_claimed_as_install_success(self):
        run = self.run_for(choices=[{'action': 'skip', 'consent': False}] * 3)
        self.assertFalse(run.receipt()['selected_files_verified'])
        self.assertEqual(run.states, ['skipped'] * 3)

    def test_one_shot_mutation_claim_and_exact_source(self):
        run = self.run_for(); instruction = run.recheck(0, self.obs, self.owner)
        self.assertEqual(instruction['operation'], 'replace')
        self.assertEqual(instruction['sha256'], self.payload.files[0]['sha256'])
        with self.assertRaises(Invalid):
            run.recheck(0, self.obs, self.owner)

    def test_missing_commit_ack_or_bad_readback_stops_run(self):
        for ack, state in [(False, file_state(self.contents[self.payload.files[0]['source']])), (True, file_state(b'wrong'))]:
            run = self.run_for(); run.recheck(0, self.obs, self.owner)
            with self.subTest(ack=ack), self.assertRaises(Invalid):
                run.finish(0, 'verified', state, ack)
            self.assertEqual(run.states[0], 'uncertain')
            self.assertFalse(run.receipt()['selected_files_verified'])

    def test_cancel_media_removal_restart_and_owner_change(self):
        for reason in ('cancelled before mutation', 'media removed', 'device restarted', 'port disconnected'):
            run = self.run_for(); run.invalidate(reason)
            with self.subTest(reason=reason), self.assertRaises(Invalid):
                run.recheck(0, self.obs, self.owner)
        for owner in [SimpleNamespace(session=self.owner.session, usable=True), SimpleNamespace(session=99, usable=True)]:
            with self.assertRaises(Invalid):
                self.run_for().recheck(0, self.obs, owner)
        run = self.run_for(); self.owner.usable = False
        with self.assertRaises(Invalid):
            run.recheck(0, self.obs, self.owner)

    def test_cancel_during_claim_retains_uncertain_effect(self):
        run = self.run_for(); run.recheck(0, self.obs, self.owner); run.invalidate('cancelled during PUT')
        self.assertEqual(run.states[0], 'uncertain')
        self.assertEqual(run.states[1:], ['pending', 'pending'])

    def test_lost_put_and_commit_ack_stop_without_retry(self):
        for stage in ('PUT', 'COMMIT'):
            run = self.run_for(); run.recheck(0, self.obs, self.owner); run.finish(0, 'uncertain')
            with self.subTest(stage=stage), self.assertRaises(Invalid):
                run.recheck(0, self.obs, self.owner)
            self.assertEqual(run.receipt()['states'][0], 'uncertain')

    def test_known_failure_stops_and_preserves_prior_verified_receipt(self):
        run = self.run_for(); run.recheck(1, self.obs, self.owner)
        run.recheck(0, self.obs, self.owner); run.finish(0, 'failed-known')
        self.assertEqual(run.states, ['failed-known', 'verified', 'pending'])
        self.assertFalse(run.receipt()['selected_files_verified'])

    def test_changed_selection_after_consent_is_rejected(self):
        run = self.run_for(); run.selection['files'][0]['current']['sha256'] = '0' * 64
        with self.assertRaises(Invalid):
            run.recheck(0, self.obs, self.owner)

    def test_mutation_simulation_preserves_siblings_appdata_and_other_namespaces(self):
        card = {(7, '/example/data.bin'): b'customized by owner\n', (7, '/example/empty.txt'): b'',
                (7, '/personal.txt'): b'KEEP', (7, '/appdata/state'): b'USER STATE', (99, '/example/data.bin'): b'OTHER NS'}
        preserved = {k: v for k, v in card.items() if k not in {(7, f['destination']) for f in self.payload.files}}
        run = self.run_for(); fresh = copy.deepcopy(self.obs)
        for i, entry in enumerate(self.payload.files):
            instruction = run.recheck(i, fresh, self.owner)
            if instruction is not None:
                dest = (int(instruction['namespace_id']), instruction['destination'])
                self.assertEqual(dest in card, instruction['operation'] == 'replace')
                value = self.contents[instruction['source']]
                card[dest] = value
                fresh['files'][i]['current'] = file_state(value)
                run.finish(i, 'verified', fresh['files'][i]['current'], True)
        self.assertEqual({k: card[k] for k in preserved}, preserved)
        self.assertTrue(run.receipt()['selected_files_verified'])
        self.assertTrue(run.verify_final_inventory(fresh))

    def test_final_inventory_needs_target_stage_and_actual_admission(self):
        p, contents = payload_at(self.temp.name, 'update-before-readonly')
        before = observation(p, contents, 'all-equal')
        run = SelectionRun(p, before, make_selection(p, before, choices_for(p, before)), self.owner)
        with self.assertRaises(Invalid):
            run.verify_final_inventory(before)
        final = observation(p, contents, 'all-equal', session=99, firmware=p.target_firmware)
        self.assertTrue(run.verify_final_inventory(final))
        final['namespaces'] = []
        self.assertFalse(run.verify_final_inventory(final))

    def test_subset_uses_actual_selected_budget_and_operations(self):
        m, c, contents = source()
        c['namespaces'][0].update(write_budget_bytes=0, writable=False, operations=['stat', 'get'])
        path = Path(self.temp.name) / 'readonly.zip'; path.write_bytes(base.pack(m, contents))
        p = Payload(path, c, True)
        obs = observation(p, contents, 'all-equal')
        selected = make_selection(p, obs, choices_for(p, obs, skipped=(0, 2)))
        self.assertEqual(validate_selection(p, obs, selected)['selected_files'], 1)
        obs['files'][0]['current'] = {'kind': 'absent', 'status': 1}
        with self.assertRaisesRegex(Invalid, 'read-only'):
            make_selection(p, obs, choices_for(p, obs))
        c['namespaces'][0].update(write_budget_bytes=17, writable=True, operations=['stat', 'get', 'mkdir', 'create'])
        p = Payload(path, c, True)
        obs = observation(p, contents, 'clean')
        make_selection(p, obs, choices_for(p, obs, skipped=(0,)))
        with self.assertRaisesRegex(Invalid, 'budget'):
            make_selection(p, obs, choices_for(p, obs))

    def test_selected_file_limit_is_not_whole_inventory_limit(self):
        m, c, contents = source(); c['namespaces'][0]['max_file_bytes'] = 17
        path = Path(self.temp.name) / 'limited.zip'; path.write_bytes(base.pack(m, contents))
        p = Payload(path, c, True); obs = observation(p, contents, 'clean')
        make_selection(p, obs, choices_for(p, obs, skipped=(0,)))
        with self.assertRaisesRegex(Invalid, 'file bound'):
            make_selection(p, obs, choices_for(p, obs))

    def test_final_verification_cannot_survive_mutation_or_rejected_change(self):
        run = self.run_for()
        equal = observation(self.payload, self.contents, 'all-equal')
        self.assertTrue(run.verify_final_inventory(equal))
        run.recheck(0, self.obs, self.owner)
        self.assertFalse(run.receipt()['all_inventory_bytes_verified_after_final_firmware'])
        run = self.run_for(); self.assertTrue(run.verify_final_inventory(equal))
        changed = copy.deepcopy(equal); changed['binding']['session'] = 0
        with self.assertRaises(Invalid):
            run.verify_final_inventory(changed)
        self.assertFalse(run.receipt()['all_inventory_bytes_verified_after_final_firmware'])
        run = self.run_for(); self.assertTrue(run.verify_final_inventory(equal))
        changed = copy.deepcopy(self.obs); changed['files'][0]['current'] = file_state(b'changed')
        with self.assertRaises(Invalid):
            run.recheck(0, changed, self.owner)
        self.assertFalse(run.receipt()['all_inventory_bytes_verified_after_final_firmware'])

    def test_partial_acknowledged_bytes_survive_uncertain_receipt(self):
        run = self.run_for(); run.recheck(0, self.obs, self.owner)
        run.progress(0, 7); run.finish(0, 'uncertain')
        row = run.receipt()['files'][0]
        self.assertEqual(row['acknowledged_bytes'], 7)
        self.assertEqual(row['outcome'], 'uncertain')
        self.assertIsNone(row['readback'])

    def test_fixture_not_installable_without_opt_in(self):
        with self.assertRaisesRegex(Invalid, 'fixture is not installable'):
            Payload(Path(self.temp.name) / 'container.zip', self.payload.context)


class Transcript:
    """Exact command/reply pairs consumed by the production Python protocol client."""
    def __init__(self, rows):
        self.rows, self.pending, self.commands = list(rows), None, []

    def write(self, frame, deadline):
        words = frame.decode().strip().split(' ')
        command = ' '.join(words[3:]); self.commands.append(command)
        expected, response = self.rows.pop(0)
        assert command == expected, (command, expected)
        self.pending = (' '.join(words[:3]) + ' ' + response).encode()

    def line(self, deadline):
        if self.pending is None:
            raise TimeoutError('scripted lost response')
        result, self.pending = self.pending, None
        return result


def path_row(path):
    encoded = path.encode()
    return ('PATH 0 ' + encoded.hex(), '0 PATH ' + str(len(encoded)))


class ProtocolAdapterTests(unittest.TestCase):
    def client(self, rows):
        transport = Transcript([('HELLO', '0 HELLO 48 511 1')] + rows)
        return serial_files.Client(transport, session=0x12345678), transport

    def test_actual_client_only_stat_not_found_means_absent(self):
        for code in [1, -3, -4, -2, -6, -10, 2, 3, 99]:
            client, transport = self.client([path_row('/x'), ('STAT 7', str(code))])
            if code == 1:
                self.assertEqual(observe_current(client, '7', '/x', 100), {'kind': 'absent', 'status': 1})
            else:
                with self.subTest(code=code), self.assertRaises(serial_files.RemoteError):
                    observe_current(client, '7', '/x', 100)
            self.assertFalse(any(c.startswith('PUT') or c.startswith('MKDIR') for c in transport.commands))

    def test_actual_client_empty_and_binary_full_hash_requires_close(self):
        for data in (b'', bytes(range(105))):
            rows = [path_row('/x'), ('STAT 7', f'0 STAT 0 {len(data)}'), path_row('/x'), ('GET 7', f'0 GET {len(data)}')]
            # Deliberately legal short 17-byte reads.
            for i in range(0, len(data), 17):
                chunk = data[i:i+17]
                rows.append((f'READ {i}', f'0 READ {i+len(chunk)} {chunk.hex()}'))
            rows.extend([('CLOSE', f'0 CLOSE {zlib.crc32(data):08x}'), path_row('/x'), ('STAT 7', f'0 STAT 0 {len(data)}')])
            client, transport = self.client(rows)
            self.assertEqual(observe_current(client, '7', '/x', 1024), file_state(data))
            self.assertFalse(transport.rows)
            self.assertEqual(transport.commands.count('CLOSE'), 1)

    def test_close_crc_failure_is_not_a_current_file_identity(self):
        rows = [path_row('/x'), ('STAT 7', '0 STAT 0 0'), path_row('/x'), ('GET 7', '0 GET 0'), ('CLOSE', '0 CLOSE ffffffff')]
        client, transport = self.client(rows)
        with self.assertRaises(serial_files.FileTransferError):
            observe_current(client, '7', '/x', 100)
        self.assertFalse(client.usable)
        self.assertNotIn('ABORT', transport.commands)

    def test_namespace_absence_cannot_be_confused_with_missing_path(self):
        with tempfile.TemporaryDirectory() as temp:
            p, _ = payload_at(temp)
            client, transport = self.client([('NS 0', '0 NS 8 1 7364')])
            with self.assertRaisesRegex(Invalid, 'missing from actual admission'):
                observe(p, client, p.transfer_firmware)
            self.assertFalse(any('STAT' in c for c in transport.commands))

    def test_snapshot_adapter_uses_uint64_decimal_not_label(self):
        with tempfile.TemporaryDirectory() as temp:
            p, _ = payload_at(temp)
            rows = [('NS 0', '0 NS 7 1 7364'), path_row('/example'), ('STAT 7', '0 STAT 1 0')]
            for f in p.files:
                rows.extend([path_row(f['destination']), ('STAT 7', '1')])
            client, transport = self.client(rows)
            obs = observe(p, client, p.transfer_firmware)
            self.assertTrue(obs['session_usable'])
            self.assertTrue(all(r['current'] == {'kind': 'absent', 'status': 1} for r in obs['files']))
            self.assertFalse(transport.rows)

if __name__ == '__main__':
    unittest.main(verbosity=2)
