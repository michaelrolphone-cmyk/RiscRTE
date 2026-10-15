#!/usr/bin/env python3
"""Offline release guard regressions. No credentials, network, build or release."""
import base64
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock
import zipfile

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/publish_native_idf61_release.py'
spec = importlib.util.spec_from_file_location('native_release', SCRIPT)
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


def pin(letter):
    return {'revision': letter * 40, 'tree': letter * 40, 'submodules': []}


def zip_bytes(files):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    return stream.getvalue()


def fixtures():
    manifest = {'schema': 1, 'enabled': True, 'repository': r.REPOSITORY,
                'version': r.VERSION, 'tag': r.TAG, 'publication_branch': r.BRANCH,
                'source_sha': 'a' * 40, 'source_workflow_sha256': r.sha(b'workflow bytes\n'),
                'run_id': 12, 'run_attempt': 2, 'workflow_id': 34, 'artifacts': {}}
    archives, all_files = {}, {}
    for index, product in enumerate(r.PRODUCTS):
        sources = {'runtime': pin('a'), 'sdk': dict(pin('b'), revision=r.SDK)}
        if product != 'generic':
            sources.update(product=pin({'twatch': 'c', 'waveshare': 'd', 'x4': 'e'}[product]),
                           system=pin('f'), drivers=pin('1'), image_helper=pin('c'))
        metadata = {'GITHUB_REPOSITORY': r.REPOSITORY, 'GITHUB_SHA': manifest['source_sha'],
                    'GITHUB_REF': 'refs/heads/' + r.BRANCH, 'GITHUB_RUN_ID': '12', 'GITHUB_RUN_ATTEMPT': '2',
                    'GITHUB_WORKFLOW_REF': r.REPOSITORY + '/' + r.BUILD_WORKFLOW + '@refs/heads/' + r.BRANCH,
                    'GITHUB_WORKFLOW_SHA': manifest['source_sha'], 'PRODUCT': product,
                    'SDK_SOURCE': r.SDK, 'SDK_IMAGE': r.SDK_IMAGE}
        custody = {'schema': 1, 'uid': 1000, 'sources': {role: dict(value, clean=True, path='/sources/' + role) for role, value in sources.items()}}
        files = {'metadata/ci-run.json': r.encoded(metadata), 'metadata/workflow.yml': b'workflow bytes\n',
                 'metadata/sources-before.json': r.encoded(custody), 'metadata/sources-after.json': r.encoded(custody)}
        if product == 'generic':
            files.update({member: ('generic ' + member).encode() for member in r.GENERIC_ASSETS})
        else:
            metadata.update({key: sources[role]['revision'] for role, key in (
                ('product', 'PRODUCT_SOURCE'), ('system', 'SYSTEM_SOURCE'), ('drivers', 'DRIVERS_SOURCE'), ('image_helper', 'IMAGE_HELPER_SOURCE'))})
            files['metadata/ci-run.json'] = r.encoded(metadata)
            payloads = {name: ('qualified ' + product + '/' + name).encode() for name in r.PAYLOADS}
            if product == 'x4':
                payloads[r.X4_SD] = zip_bytes({'default.elf': b'exact SD ELF'})
            baseline = {'product': product}
            if product == 'x4':
                baseline['sd'] = r.file_record(payloads[r.X4_SD])
            files['metadata/released-baseline.json'] = r.encoded(baseline)
            native = {'firmware_bin': ('riscrte_native.bin', payloads['firmware.bin']),
                      'firmware_elf': ('riscrte_native.elf', b'ELF fixture'),
                      'firmware_map': ('riscrte_native.map', b'MAP fixture'),
                      'bootloader': ('bootloader/bootloader.bin', payloads['bootloader.bin']),
                      'partitions': ('partition_table/partition-table.bin', payloads['partitions.bin'])}
            for path, data in native.values():
                files['build/' + path] = data
            lock = {'schema': 1, 'publication_enabled': False, 'candidate_version': r.PRODUCTS[product],
                    'sources': sources, 'baseline_contract_sha256': r.sha(files['metadata/released-baseline.json']),
                    'compiler': {'version': 'GCC esp-15.2.0_20251204 15.2.0', 'sha256': '0' * 64},
                    'native_files': {key: dict(r.file_record(data), path=path) for key, (path, data) in native.items()}}
            lock_data = r.encoded(lock)
            closure = {'fresh_build': True, 'same_process_qualification': True, 'compile_jobs': 1,
                       'detached_receipt_is_attestation': False, 'sources': sources,
                       'artifacts': {key: r.file_record(data) for key, (path, data) in native.items()}}
            report = {'schema': 1, 'status': 'software-qualified-candidate-hardware-pending',
                      'publication_enabled': False, 'hardware_tested': False, 'device_accessed': False,
                      'preserving_update_kit_emitted': False, 'preserving_sdk_migration_eligible': False,
                      'factory_erases_data': True, 'preserving_sdk_migration_blocker': 'Hardware pending.',
                      'input_lock': lock, 'input_lock_sha256': r.sha(lock_data), 'baseline_contract': baseline,
                      'build_closure' if product == 'x4' else 'controlled_build': closure,
                      'artifacts': {name: r.file_record(data) for name, data in payloads.items()}}
            if product == 'x4':
                report.update(sd_unchanged=True, device_msc_sd_workflow_preserved=True,
                              native={'x4_bootlog_memory': {'input_kind': 'final-elf', 'copied_bytes_external': 4528}},
                              admission={'elf_count': 43}, admission_sanitized={'elf_count': 43},
                              sd_admission={'elf_count': 49}, sd_admission_sanitized={'elf_count': 49})
            files.update({'candidate/' + name: data for name, data in payloads.items()})
            files['candidate/source-lock.json'] = lock_data
            files['candidate/qualification.json'] = r.encoded(report)
            files['build/native-build-evidence/source-lock.json'] = lock_data
            files['build/native-build-evidence/build-closure.json'] = r.encoded(closure)
            sums = ''.join(r.sha(data) + '  ' + name.removeprefix('candidate/') + '\n'
                           for name, data in sorted(files.items()) if name.startswith('candidate/'))
            files['candidate/SHA256SUMS'] = sums.encode()
        data = zip_bytes(files)
        archives[product], all_files[product] = data, files
        manifest['artifacts'][product] = dict(r.file_record(data), id=100 + index,
            name=f'native-idf6-{product}-{manifest["source_sha"]}', job_id=200 + index,
            job_name='native-idf (' + product + ')', sources=sources,
            members={name: r.file_record(data) for name, data in files.items()})
    run = {'id': 12, 'run_attempt': 2, 'repository': {'full_name': r.REPOSITORY, 'id': 45},
           'head_repository': {'full_name': r.REPOSITORY}, 'head_sha': manifest['source_sha'],
           'head_branch': r.BRANCH, 'event': 'push', 'status': 'completed', 'conclusion': 'success',
           'workflow_id': 34, 'path': r.BUILD_WORKFLOW}
    workflow = {'id': 34, 'path': r.BUILD_WORKFLOW}
    jobs = {'total_count': 4, 'jobs': [{'id': row['job_id'], 'name': row['job_name'], 'run_id': 12,
            'head_sha': manifest['source_sha'], 'status': 'completed', 'conclusion': 'success'} for row in manifest['artifacts'].values()]}
    artifacts = {'total_count': 4, 'artifacts': [{'id': row['id'], 'name': row['name'], 'size_in_bytes': row['bytes'],
        'digest': 'sha256:' + row['sha256'], 'expired': False,
        'workflow_run': {'id': 12, 'head_sha': manifest['source_sha'], 'head_branch': r.BRANCH,
                         'repository_id': 45, 'head_repository_id': 45}} for row in manifest['artifacts'].values()]}
    return manifest, archives, all_files, run, workflow, jobs, artifacts


class FakeGitHub:
    """In-memory API exercising the whole transaction without real credentials."""
    def __init__(self, manifest, archives, run, workflow, jobs, artifacts):
        self.manifest, self.archives = manifest, archives
        self.run, self.workflow, self.jobs, self.artifacts = run, workflow, jobs, artifacts
        self.calls, self.uploads, self.downloads = [], {}, []
        self.tag, self.release = None, None
        self.corrupt_download = False

    def api(self, path, method='GET', payload=None, absent_ok=False):
        self.calls.append((method, path, payload))
        base = 'repos/' + r.REPOSITORY
        if method == 'POST' and path == base + '/git/refs':
            if self.tag is not None: raise ValueError('Existing tag')
            self.tag = {'ref': payload['ref'], 'object': {'sha': payload['sha']}}
            return self.tag
        if method == 'POST' and path == base + '/releases':
            self.release = dict(payload, id=987, assets=[], html_url=f'https://github.com/{r.REPOSITORY}/releases/tag/{r.TAG}')
            return copy.deepcopy(self.release)
        if path == base + '/releases/987':
            if method == 'PATCH': self.release.update(payload)
            return copy.deepcopy(self.release)
        if path == base + '/git/ref/tags/' + r.TAG: return copy.deepcopy(self.tag)
        if path == base + '/releases/tags/' + r.TAG: return copy.deepcopy(self.release)
        if path.startswith(base + '/releases?'): return [] if self.release is None else [copy.deepcopy(self.release)]
        if path == base + '/actions/runs/12': return self.run
        if path == base + '/actions/workflows/34': return self.workflow
        if '/attempts/2/jobs?' in path: return self.jobs
        if path.endswith('/artifacts?per_page=100'): return self.artifacts
        if '/contents/' + r.MANIFEST_PATH + '?' in path: data = r.encoded(self.manifest)
        elif '/contents/' + r.BUILD_WORKFLOW + '?' in path: data = b'workflow bytes\n'
        elif '/contents/platformio.ini?' in path: data = b'[riscrte]\nversion = 0.4.0\n'
        else: raise AssertionError('Unexpected API request: ' + path)
        return {'encoding': 'base64', 'content': base64.b64encode(data).decode()}

    def download(self, path, destination):
        self.downloads.append(path)
        if '/actions/artifacts/' in path:
            artifact_id = int(path.split('/')[-2])
            product = next(product for product, row in self.manifest['artifacts'].items() if row['id'] == artifact_id)
            data = self.archives[product]
        else:
            data = self.uploads[int(path.split('/')[-1])]
            if self.corrupt_download: data += b'tampered'
        destination.write_bytes(data)

    def upload(self, release_id, path):
        if release_id != 987: raise AssertionError('Wrong release ID')
        data = path.read_bytes(); asset_id = 1000 + len(self.uploads)
        self.uploads[asset_id] = data
        row = {'id': asset_id, 'name': path.name, 'size': len(data), 'digest': 'sha256:' + r.sha(data), 'state': 'uploaded'}
        self.release['assets'].append(row)
        return row


class ReleaseGuards(unittest.TestCase):
    def setUp(self):
        self.manifest, self.archives, self.files, self.run, self.workflow, self.jobs, self.artifacts = fixtures()
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.paths = {}
        for product, data in self.archives.items():
            path = self.root / (product + '.zip')
            path.write_bytes(data)
            self.paths[product] = path

    def ci(self):
        r.validate_ci(self.manifest, self.run, self.workflow, self.jobs, self.artifacts)

    def archive(self, product):
        r.validate_archive(self.manifest, product, self.paths[product])

    def refresh(self, product):
        """Simulate a separately pinned ZIP to reach deeper independent checks."""
        data = zip_bytes(self.files[product]); self.paths[product].write_bytes(data)
        row = self.manifest['artifacts'][product]
        row.update(r.file_record(data), members={name: r.file_record(data) for name, data in self.files[product].items()})

    def modify_json(self, product, member, change):
        value = r.load_json(self.files[product][member]); change(value)
        self.files[product][member] = r.encoded(value); self.refresh(product)

    def test_valid_all_products_and_original_zip_delivery(self):
        self.assertTrue(r.validate_manifest(self.manifest)); self.ci()
        for product in r.PRODUCTS:
            self.archive(product)
        output = self.root / 'release'
        inventory = r.stage_assets(self.manifest, self.paths, output)
        for product in r.PRODUCTS:
            self.assertEqual((output / r.ZIP_ASSETS[product]).read_bytes(), self.archives[product])
        self.assertEqual(set(inventory), set(path.name for path in output.iterdir()))
        self.assertEqual(r.load_json((output / 'Runtime-0.4.0-release-manifest.json').read_bytes()), self.manifest)
        self.assertIn('NOT YET ELIGIBLE', (output / 'Runtime-0.4.0-READ-ME-FIRST.txt').read_text())
        self.assertTrue(all('Generic' in name for name in r.GENERIC_ASSETS.values()))

    def test_disabled_manifest_is_inert_and_has_no_guessed_identity(self):
        raw = (SCRIPT.parents[1] / r.MANIFEST_PATH).read_bytes()
        value = r.load_json(raw)
        if not value['enabled']:
            self.assertFalse(r.validate_manifest(value))
            with self.assertRaisesRegex(ValueError, 'disabled'):
                r.publish(value, raw, {}, client=mock.Mock())
        else:
            # An enabled real request is validated without any publication.
            self.assertTrue(r.validate_manifest(value))
        disabled = {key: self.manifest[key] for key in ('schema', 'repository', 'version', 'tag', 'publication_branch')}
        disabled.update(enabled=False, disabled_reason='Await CI')
        disabled['source_sha'] = 'a' * 40
        with self.assertRaises(ValueError):
            r.validate_manifest(disabled)

    def test_reject_manifest_wrong_identity_missing_product_duplicate_ids(self):
        for field, value in [('repository', 'someone/fork'), ('tag', 'Runtime0.4.0'), ('version', '0.4.1'),
                             ('publication_branch', 'main'), ('source_sha', 'abc'), ('run_id', True), ('enabled', 1)]:
            with self.subTest(field=field):
                bad = copy.deepcopy(self.manifest); bad[field] = value
                with self.assertRaises(ValueError): r.validate_manifest(bad)
        for change in [lambda m: m['artifacts'].pop('x4'),
                       lambda m: m['artifacts']['x4'].update(id=m['artifacts']['twatch']['id']),
                       lambda m: m['artifacts']['x4'].update(job_id=m['artifacts']['twatch']['job_id']),
                       lambda m: m['artifacts']['x4']['sources']['drivers'].update(revision='2' * 40)]:
            bad = copy.deepcopy(self.manifest); change(bad)
            with self.assertRaises(ValueError): r.validate_manifest(bad)

    def test_reject_ci_run_not_exact_success(self):
        for field, value in [('id', 13), ('run_attempt', 1), ('head_sha', 'b' * 40), ('head_branch', 'main'),
                             ('event', 'pull_request'), ('status', 'in_progress'), ('conclusion', 'failure'),
                             ('workflow_id', 35), ('path', '.github/workflows/other.yml'),
                             ('repository', {'full_name': 'evil/fork'}), ('head_repository', {'full_name': 'evil/fork'})]:
            with self.subTest(field=field):
                old = self.run[field]; self.run[field] = value
                with self.assertRaises(ValueError): self.ci()
                self.run[field] = old

    def test_reject_any_failed_skipped_missing_or_wrong_job(self):
        for field, value in [('name', 'other job'), ('run_id', 15), ('head_sha', 'b' * 40),
                             ('status', 'queued'), ('conclusion', 'failure'), ('conclusion', 'skipped')]:
            with self.subTest(field=field, value=value):
                row = self.jobs['jobs'][2]; old = row[field]; row[field] = value
                with self.assertRaises(ValueError): self.ci()
                row[field] = old
        self.jobs['total_count'] = 5
        with self.assertRaises(ValueError): self.ci()

    def test_reject_artifact_mismatch_expiry_or_fork(self):
        row = self.artifacts['artifacts'][3]
        for field, value in [('id', 0), ('name', 'wrong'), ('size_in_bytes', 0), ('digest', None), ('expired', True)]:
            with self.subTest(field=field):
                old = row[field]; row[field] = value
                with self.assertRaises(ValueError): self.ci()
                row[field] = old
        for field in ('id', 'repository_id', 'head_repository_id', 'head_sha', 'head_branch'):
            with self.subTest(field=field):
                old = row['workflow_run'][field]; row['workflow_run'][field] = 'wrong'
                with self.assertRaises(ValueError): self.ci()
                row['workflow_run'][field] = old

    def test_reject_zip_hash_or_inventory_change(self):
        self.paths['generic'].write_bytes(self.archives['generic'] + b'changed')
        with self.assertRaisesRegex(ValueError, 'ZIP bytes'): self.archive('generic')
        self.paths['generic'].write_bytes(self.archives['generic'])
        self.manifest['artifacts']['generic']['members'].pop('metadata/ci-run.json')
        with self.assertRaisesRegex(ValueError, 'inventory'): self.archive('generic')

    def test_reject_paths_links_duplicates_and_conflicts(self):
        for name in ('../outside', '/absolute', 'C:/drive', 'a\\b', './a', 'a//b', 'a/../b', 'a\nb'):
            with self.subTest(name=name), self.assertRaises(ValueError): r.safe_member(name)
        for names in [('same', 'same'), ('same', 'SAME'), ('a', 'a/b')]:
            stream = io.BytesIO()
            with zipfile.ZipFile(stream, 'w') as archive:
                for name in names: archive.writestr(name, b'x')
            with zipfile.ZipFile(io.BytesIO(stream.getvalue())) as archive, self.assertRaises(ValueError):
                r.zip_inventory(archive)
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, 'w') as archive:
            info = zipfile.ZipInfo('link'); info.create_system = 3
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(info, b'outside')
        with zipfile.ZipFile(io.BytesIO(stream.getvalue())) as archive, self.assertRaisesRegex(ValueError, 'symlink'):
            r.zip_inventory(archive)

    def test_reject_member_hash_and_source_custody_changes(self):
        self.manifest['artifacts']['generic']['members']['build/riscrte_native.bin']['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'Member bytes'): self.archive('generic')
        self.refresh('generic')
        self.modify_json('generic', 'metadata/sources-after.json', lambda value: value['sources']['runtime'].update(clean=False))
        with self.assertRaisesRegex(ValueError, 'custody'): self.archive('generic')

    def test_reject_archived_wrong_run_attempt_and_workflow(self):
        for key, value in [('GITHUB_RUN_ATTEMPT', '1'), ('GITHUB_WORKFLOW_SHA', 'b' * 40), ('GITHUB_REPOSITORY', 'evil/fork')]:
            original = self.files['generic']['metadata/ci-run.json']
            self.modify_json('generic', 'metadata/ci-run.json', lambda row: row.update({key: value}))
            with self.assertRaisesRegex(ValueError, 'CI identity'): self.archive('generic')
            self.files['generic']['metadata/ci-run.json'] = original; self.refresh('generic')
        self.files['generic']['metadata/workflow.yml'] = b'other workflow'; self.refresh('generic')
        with self.assertRaisesRegex(ValueError, 'workflow'): self.archive('generic')

    def test_reject_hardware_or_preserving_claims_and_detached_receipts(self):
        member = 'candidate/qualification.json'; original = self.files['twatch'][member]
        for field, value in [('hardware_tested', True), ('device_accessed', True), ('publication_enabled', True),
                             ('preserving_update_kit_emitted', True), ('preserving_sdk_migration_eligible', True),
                             ('factory_erases_data', False), ('status', 'qualified')]:
            self.modify_json('twatch', member, lambda row: row.update({field: value}))
            with self.subTest(field=field), self.assertRaises(ValueError): self.archive('twatch')
            self.files['twatch'][member] = original; self.refresh('twatch')
        self.modify_json('twatch', member, lambda row: row['controlled_build'].update(detached_receipt_is_attestation=True))
        with self.assertRaises(ValueError): self.archive('twatch')

    def test_reject_lock_or_qualified_payload_tampering(self):
        self.modify_json('waveshare', 'candidate/qualification.json', lambda row: row.update(input_lock_sha256='0' * 64))
        with self.assertRaisesRegex(ValueError, 'input lock'): self.archive('waveshare')
        self.files['x4']['candidate/firmware.bin'] = b'rebuilt replacement'; self.refresh('x4')
        with self.assertRaisesRegex(ValueError, 'asset hash'): self.archive('x4')

    def test_reject_incomplete_checksums_and_extra_preserving_kit(self):
        self.files['twatch']['candidate/SHA256SUMS'] = b''; self.refresh('twatch')
        with self.assertRaisesRegex(ValueError, 'SHA256SUMS'): self.archive('twatch')
        self.files['x4']['candidate/preserving-update.zip'] = b'kit'; self.refresh('x4')
        with self.assertRaisesRegex(ValueError, 'inventory'): self.archive('x4')

    def test_reject_x4_inventory_sd_and_memory_changes(self):
        original = self.files['x4']['candidate/qualification.json']
        for change in [lambda row: row.update(sd_unchanged=False),
                       lambda row: row['admission_sanitized'].update(elf_count=42),
                       lambda row: row['sd_admission'].update(elf_count=48),
                       lambda row: row['native']['x4_bootlog_memory'].update(copied_bytes_external=0)]:
            self.modify_json('x4', 'candidate/qualification.json', change)
            with self.assertRaises(ValueError): self.archive('x4')
            self.files['x4']['candidate/qualification.json'] = original; self.refresh('x4')

    def test_reject_generic_product_masquerade(self):
        self.files['generic']['candidate/firmware.bin'] = b'fake product'; self.refresh('generic')
        with self.assertRaisesRegex(ValueError, 'masquerade'): self.archive('generic')

    def environment(self):
        return {'GITHUB_ACTIONS': 'true', 'RUNNER_ENVIRONMENT': 'github-hosted',
                'GITHUB_REPOSITORY': r.REPOSITORY, 'GITHUB_EVENT_NAME': 'push',
                'GITHUB_REF': 'refs/heads/' + r.BRANCH, 'GITHUB_SHA': 'f' * 40,
                'GITHUB_WORKFLOW_SHA': 'f' * 40, 'GITHUB_TOKEN': 'test-job-token',
                'GITHUB_WORKFLOW_REF': r.REPOSITORY + '/' + r.RELEASE_WORKFLOW + '@refs/heads/' + r.BRANCH}

    def test_no_local_publication_or_wrong_event_ref_identity(self):
        with self.assertRaisesRegex(ValueError, 'GitHub-hosted'):
            r.publication_context(self.manifest, {})
        env = self.environment(); r.publication_context(self.manifest, env)
        self.assertNotEqual(env['GITHUB_SHA'], self.manifest['source_sha'])
        for key in env:
            bad = dict(env); bad.pop(key)
            with self.subTest(key=key), self.assertRaises(ValueError): r.publication_context(self.manifest, bad)
        client = mock.Mock()
        with self.assertRaises(ValueError): r.publish(self.manifest, r.encoded(self.manifest), {}, client)
        client.assert_not_called(); self.assertEqual(client.mock_calls, [])

    def test_existing_tag_release_or_draft_always_blocks(self):
        for sequence in [[{'ref': 'existing'}], [None, {'draft': False}], [None, None, [{'tag_name': r.TAG, 'draft': True}]]]:
            client = mock.Mock(); client.api.side_effect = sequence
            with self.assertRaises(ValueError): r.ensure_new_release(client)
            self.assertTrue(all(len(call.args) == 1 for call in client.api.call_args_list))
        client = mock.Mock(); client.api.side_effect = [None, None, []]
        r.ensure_new_release(client)

    def test_upload_metadata_and_redownload_hashes(self):
        client = mock.Mock()
        data = b'exact bytes'; expected = {'evidence.zip': r.file_record(data)}
        release = {'tag_name': r.TAG, 'draft': True, 'prerelease': True,
                   'assets': [{'id': 1, 'name': 'evidence.zip', 'state': 'uploaded', 'size': len(data), 'digest': 'sha256:' + r.sha(data)}]}
        client.download.side_effect = lambda path, destination: destination.write_bytes(data)
        r.verify_release_bytes(client, release, expected, self.root, draft=True)
        (self.root / 'evidence.zip').unlink()
        client.download.side_effect = lambda path, destination: destination.write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'Redownloaded'): r.verify_release_bytes(client, release, expected, self.root, draft=True)
        release['assets'][0]['digest'] = 'sha256:' + '0' * 64
        with self.assertRaisesRegex(ValueError, 'metadata'): r.verify_release_bytes(client, release, expected, self.root, draft=True)

    def test_duplicate_json_keys_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Duplicate JSON'):
            r.load_json(b'{"enabled":false,"enabled":true}')

    def test_no_token_forwarded_to_signed_storage(self):
        client = r.GitHub('secret-not-for-storage')
        first = r.urllib.error.HTTPError('https://api.github.com/download', 302, 'redirect',
                                       {'Location': 'https://example.blob.core.windows.net/object?sig=one-time'}, None)
        response = io.BytesIO(b'zip bytes')
        with mock.patch.object(client, 'request', side_effect=first), mock.patch.object(client.opener, 'open', return_value=response) as opened:
            client.download('repos/test/artifacts/1/zip', self.root / 'download.zip')
            self.assertFalse(opened.call_args.args[0].has_header('Authorization'))
        first.headers['Location'] = 'http://evil.invalid/steal'
        with mock.patch.object(client, 'request', side_effect=first), mock.patch.object(client.opener, 'open') as opened:
            with self.assertRaisesRegex(ValueError, 'destination'): client.download('anything', self.root / 'other.zip')
            opened.assert_not_called()

    def fake_client(self):
        return FakeGitHub(self.manifest, self.archives, self.run, self.workflow, self.jobs, self.artifacts)

    def test_complete_publication_transaction_targets_original_ci_source(self):
        client = self.fake_client()
        with mock.patch('builtins.print'):
            r.publish(self.manifest, r.encoded(self.manifest), self.environment(), client)
        self.assertFalse(client.release['draft'])
        self.assertTrue(client.release['prerelease'])
        self.assertEqual(client.tag['object']['sha'], self.manifest['source_sha'])
        self.assertEqual(client.release['target_commitish'], self.manifest['source_sha'])
        mutations = [(method, path.split('/')[-1]) for method, path, data in client.calls if method != 'GET']
        self.assertEqual(mutations, [('POST', 'refs'), ('POST', 'releases'), ('PATCH', '987')])
        self.assertEqual(len(client.downloads), 4 + 2 * len(client.uploads))
        for product, asset in r.ZIP_ASSETS.items():
            row = next(row for row in client.release['assets'] if row['name'] == asset)
            self.assertEqual(client.uploads[row['id']], self.archives[product])

    def test_corrupt_draft_never_exposed_and_retry_never_adopts(self):
        client = self.fake_client(); client.corrupt_download = True
        with self.assertRaisesRegex(ValueError, 'Redownloaded'):
            r.publish(self.manifest, r.encoded(self.manifest), self.environment(), client)
        self.assertTrue(client.release['draft'])
        self.assertFalse(any(method == 'PATCH' for method, path, data in client.calls))
        old_mutations = [call for call in client.calls if call[0] != 'GET']
        with self.assertRaisesRegex(ValueError, 'Tag already exists'):
            r.publish(self.manifest, r.encoded(self.manifest), self.environment(), client)
        self.assertEqual([call for call in client.calls if call[0] != 'GET'], old_mutations)

    def test_failed_ci_or_changed_manifest_never_mutates_remote(self):
        client = self.fake_client(); self.run['conclusion'] = 'failure'
        with self.assertRaisesRegex(ValueError, 'successful'):
            r.publish(self.manifest, r.encoded(self.manifest), self.environment(), client)
        self.assertTrue(all(method == 'GET' for method, path, data in client.calls))
        self.run['conclusion'] = 'success'
        client = self.fake_client()
        with self.assertRaisesRegex(ValueError, 'manifest differs'):
            r.publish(self.manifest, b'noncanonical replacement', self.environment(), client)
        self.assertTrue(all(method == 'GET' for method, path, data in client.calls))

    def test_workflow_is_disabled_manifest_guarded_and_no_rebuild(self):
        text = (SCRIPT.parents[1] / r.RELEASE_WORKFLOW).read_text()
        self.assertIn("if: needs.guard.outputs.enabled == 'true'", text)
        self.assertIn('test/test_publish_native_idf61_release.py', text)
        self.assertNotIn('workflow_dispatch:', text)
        self.assertNotIn('idf.py', text)
        self.assertIn('persist-credentials: false', text)


if __name__ == '__main__':
    unittest.main()
