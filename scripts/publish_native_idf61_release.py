#!/usr/bin/env python3
"""Transport exact green GitHub CI bytes to a new Runtime0.4.0 release.

No target build, flash, detached requalification, or local publication is offered.
A disabled checked-in manifest is inert. Publication requires GitHub-hosted CI,
a complete exact manifest, all four successful jobs, and original artifact ZIPs.
"""
import argparse
import base64
import configparser
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import zipfile

REPOSITORY = 'michaelrolphone-cmyk/RiscRTE'
BRANCH = 'work/native-idf-6.1'
BUILD_WORKFLOW = '.github/workflows/native-idf6.yml'
RELEASE_WORKFLOW = '.github/workflows/native-idf61-release.yml'
MANIFEST_PATH = 'docs/native-idf61/runtime-0.4.0-release.json'
VERSION = '0.4.0'
TAG = 'firmware-v' + VERSION
SDK = 'fff9895c82d744c7237be8847347bdd1b07c6643'
SDK_IMAGE = 'espressif/idf:v6.1@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c'
PRODUCTS = {'generic': None, 'twatch': '1.1.40', 'waveshare': '0.1.49', 'x4': '0.1.122'}
ZIP_ASSETS = {
    'generic': 'Runtime-0.4.0-Generic-Native-IDF61-Evidence.zip',
    'twatch': 'Runtime-0.4.0-Watch-1.1.40-Native-IDF61-Evidence.zip',
    'waveshare': 'Runtime-0.4.0-Wave-0.1.49-Native-IDF61-Evidence.zip',
    'x4': 'Runtime-0.4.0-X4-0.1.122-Native-IDF61-Evidence.zip',
}
GENERIC_ASSETS = {
    'build/riscrte_native.bin': 'Runtime-0.4.0-Generic-Native-IDF61-app-NOT-PRODUCT-FLASH.bin',
    'build/riscrte_native.elf': 'Runtime-0.4.0-Generic-Native-IDF61-debug.elf',
    'build/riscrte_native.map': 'Runtime-0.4.0-Generic-Native-IDF61.map',
    'build/bootloader/bootloader.bin': 'Runtime-0.4.0-Generic-Native-IDF61-bootloader-NOT-PRODUCT-FLASH.bin',
    'build/partition_table/partition-table.bin': 'Runtime-0.4.0-Generic-Native-IDF61-partitions-NOT-PRODUCT-FLASH.bin',
    'build/sdkconfig': 'Runtime-0.4.0-Generic-Native-IDF61-sdkconfig.txt',
}
PAYLOADS = {'factory-candidate-erases-data-0x0.bin', 'firmware.bin', 'bootloader.bin', 'partitions.bin', 'store.bin'}
X4_SD = 'X4-0.1.121-Complete-SD-Keep-Existing.zip'
MAX_ARCHIVE_BYTES = 1024 * 1024 * 1024
MAX_MEMBER_BYTES = 256 * 1024 * 1024
MAX_EXPANDED_BYTES = 2 * 1024 * 1024 * 1024
MAX_MEMBERS = 20000


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, sort_keys=True, indent=2) + '\n').encode()


def load_json(data):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'Duplicate JSON key: ' + key)
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=unique)


def file_record(data):
    return {'bytes': len(data), 'sha256': sha(data)}


def hash_record(row):
    require(isinstance(row, dict) and set(row) == {'bytes', 'sha256'}, 'Expected exact byte/hash record')
    require(type(row['bytes']) is int and 0 <= row['bytes'] <= MAX_ARCHIVE_BYTES, 'Invalid byte count')
    require(isinstance(row['sha256'], str) and re.fullmatch('[0-9a-f]{64}', row['sha256']), 'Invalid SHA256')


def source_pin(row):
    require(set(row) == {'revision', 'tree', 'submodules'}, 'Unexpected source pin fields')
    for field in ('revision', 'tree'):
        require(re.fullmatch('[0-9a-f]{40}', row[field]), 'Full immutable source SHA required')
    require(isinstance(row['submodules'], list) and len(set(row['submodules'])) == len(row['submodules']), 'Invalid source submodules')
    require(all(re.fullmatch('[0-9a-f]{40} [^\n\r]+', line) for line in row['submodules']), 'Invalid source submodule pin')


def safe_member(name):
    require(isinstance(name, str) and name and '\\' not in name and ':' not in name,
            'Unsafe archive path')
    require(all(ord(c) >= 32 and ord(c) != 127 for c in name), 'Control character in archive path')
    path = PurePosixPath(name)
    require(not path.is_absolute() and str(path) == name and '..' not in path.parts,
            'Non-canonical archive path')
    return name


def validate_manifest(manifest):
    require(manifest.get('schema') == 1 and type(manifest.get('enabled')) is bool, 'Invalid manifest schema/enabled')
    require(manifest.get('repository') == REPOSITORY and manifest.get('version') == VERSION
            and manifest.get('tag') == TAG and manifest.get('publication_branch') == BRANCH,
            'Release identity differs')
    if not manifest['enabled']:
        require(set(manifest) == {'schema', 'enabled', 'repository', 'version', 'tag', 'publication_branch', 'disabled_reason'},
                'Disabled manifest must contain no guessed CI identities')
        require(bool(manifest['disabled_reason']), 'Disabled reason required')
        return False
    require(set(manifest) == {'schema', 'enabled', 'repository', 'version', 'tag', 'publication_branch',
                             'source_sha', 'source_workflow_sha256', 'run_id', 'run_attempt', 'workflow_id', 'artifacts'},
            'Unexpected active manifest fields')
    require(re.fullmatch('[0-9a-f]{40}', manifest['source_sha']), 'Exact source SHA required')
    require(re.fullmatch('[0-9a-f]{64}', manifest['source_workflow_sha256']), 'Exact workflow digest required')
    for key in ('run_id', 'run_attempt', 'workflow_id'):
        require(type(manifest[key]) is int and manifest[key] > 0, 'Invalid CI identity: ' + key)
    require(set(manifest['artifacts']) == set(PRODUCTS), 'Exactly four product artifacts required')
    ids, jobs = set(), set()
    for product, row in manifest['artifacts'].items():
        require(set(row) == {'id', 'name', 'bytes', 'sha256', 'job_id', 'job_name', 'sources', 'members'},
                'Unexpected artifact manifest fields')
        for key in ('id', 'job_id'):
            require(type(row[key]) is int and row[key] > 0, 'Invalid artifact/job identity')
        require(row['id'] not in ids and row['job_id'] not in jobs, 'Duplicate artifact/job identity')
        ids.add(row['id']); jobs.add(row['job_id'])
        require(isinstance(row['job_name'], str) and row['job_name'], 'Exact job name required')
        require(row['name'] == f'native-idf6-{product}-{manifest["source_sha"]}', 'Artifact name/source differs')
        hash_record({key: row[key] for key in ('bytes', 'sha256')})
        roles = {'runtime', 'sdk'} if product == 'generic' else {'product', 'runtime', 'system', 'drivers', 'sdk', 'image_helper'}
        require(set(row['sources']) == roles, 'Exact source role set required')
        for pin in row['sources'].values():
            source_pin(pin)
        require(row['sources']['runtime']['revision'] == manifest['source_sha'] and row['sources']['sdk']['revision'] == SDK,
                'Runtime/SDK source pin differs')
        require(isinstance(row['members'], dict) and 0 < len(row['members']) <= MAX_MEMBERS, 'Complete member inventory required')
        for name, record in row['members'].items():
            safe_member(name); hash_record(record)
    shared = ('runtime', 'sdk', 'system', 'drivers', 'image_helper')
    for role in shared:
        values = [row['sources'][role] for row in manifest['artifacts'].values() if role in row['sources']]
        require(all(value == values[0] for value in values), 'Mixed shared source pins: ' + role)
    watch = manifest['artifacts']['twatch']['sources']
    require(watch['product'] == watch['image_helper'], 'Watch/image-helper source pins differ')
    return True


def validate_ci(manifest, run, workflow, jobs, artifacts):
    require(run.get('id') == manifest['run_id'] and run.get('run_attempt') == manifest['run_attempt'], 'CI run/attempt differs')
    require(run.get('repository', {}).get('full_name') == REPOSITORY
            and run.get('head_repository', {}).get('full_name') == REPOSITORY, 'CI repository differs')
    require(run.get('head_sha') == manifest['source_sha'] and run.get('head_branch') == BRANCH, 'CI source/ref differs')
    require(run.get('event') == 'push' and run.get('status') == 'completed' and run.get('conclusion') == 'success',
            'Exact CI run is not a successful completed push')
    require(run.get('workflow_id') == manifest['workflow_id'] and run.get('path') == BUILD_WORKFLOW,
            'CI workflow differs')
    require(workflow.get('id') == manifest['workflow_id'] and workflow.get('path') == BUILD_WORKFLOW,
            'Workflow identity differs')
    require(jobs.get('total_count') == 4 and len(jobs.get('jobs', [])) == 4, 'All four jobs must be present, without pagination/truncation')
    actual_jobs = {row['id']: row for row in jobs['jobs']}
    require(len(actual_jobs) == 4, 'Duplicate CI jobs')
    require(artifacts.get('total_count') == 4 and len(artifacts.get('artifacts', [])) == 4,
            'Exactly four CI artifacts required, without pagination/truncation')
    actual_artifacts = {row['id']: row for row in artifacts['artifacts']}
    require(len(actual_artifacts) == 4, 'Duplicate CI artifacts')
    repository_id = run['repository']['id']
    for row in manifest['artifacts'].values():
        job = actual_jobs.get(row['job_id'], {})
        require(job.get('name') == row['job_name'] and job.get('run_id') == manifest['run_id']
                and job.get('head_sha') == manifest['source_sha'] and job.get('status') == 'completed'
                and job.get('conclusion') == 'success', 'Required product job did not succeed at exact source')
        artifact = actual_artifacts.get(row['id'], {})
        require(artifact.get('name') == row['name'] and artifact.get('expired') is False
                and artifact.get('digest') == 'sha256:' + row['sha256']
                and artifact.get('size_in_bytes') == row['bytes'], 'Artifact identity, expiration, size, or digest differs')
        origin = artifact.get('workflow_run', {})
        require(origin.get('id') == manifest['run_id'] and origin.get('head_sha') == manifest['source_sha']
                and origin.get('head_branch') == BRANCH and origin.get('repository_id') == repository_id
                and origin.get('head_repository_id') == repository_id, 'Artifact run/source repository differs')


def zip_inventory(archive):
    files, normalized, total = {}, set(), 0
    infos = archive.infolist()
    require(len(infos) <= MAX_MEMBERS, 'Too many ZIP members')
    for info in infos:
        name = safe_member(info.filename[:-1] if info.is_dir() else info.filename)
        folded = name.casefold()
        require(folded not in normalized, 'Duplicate/case-colliding archive member')
        normalized.add(folded)
        mode = info.external_attr >> 16
        kind = stat.S_IFMT(mode)
        require(kind in (0, stat.S_IFDIR if info.is_dir() else stat.S_IFREG), 'ZIP symlink or special file rejected')
        require(not info.flag_bits & 1, 'Encrypted ZIP rejected')
        require(info.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED), 'Unsupported ZIP compression')
        require(0 <= info.file_size <= MAX_MEMBER_BYTES, 'ZIP member too large')
        total += info.file_size
        require(total <= MAX_EXPANDED_BYTES, 'ZIP expanded size exceeds bound')
        if not info.is_dir():
            files[name] = info
    file_names = {item.casefold() for item in files}
    for name in files:
        require(all(str(parent).casefold() not in file_names
                    for parent in PurePosixPath(name).parents if str(parent) != '.'), 'ZIP file/directory conflict')
    return files


def checked_data(archive, name, expected):
    data = archive.read(name)
    require(file_record(data) == expected, 'Member bytes differ: ' + name)
    return data


def validate_product(product, archive, row):
    read = lambda name: checked_data(archive, name, row['members'][name])
    lock_data, report_data = read('candidate/source-lock.json'), read('candidate/qualification.json')
    lock, report = load_json(lock_data), load_json(report_data)
    require(lock.get('schema') == 1 and lock.get('publication_enabled') is False
            and lock.get('candidate_version') == PRODUCTS[product], 'Candidate source-lock identity differs')
    require(lock['sources'] == row['sources'], 'Candidate source pins differ')
    require({'firmware_bin', 'firmware_elf', 'firmware_map', 'bootloader', 'partitions'} <= set(lock['native_files']),
            'Required native build hash locks missing')
    require(report.get('schema') == 1 and report.get('status') == 'software-qualified-candidate-hardware-pending', 'Candidate is not software-qualified/hardware-pending')
    for field in ('publication_enabled', 'hardware_tested', 'device_accessed', 'preserving_update_kit_emitted', 'preserving_sdk_migration_eligible'):
        require(report.get(field) is False, 'Unexpected qualification claim: ' + field)
    require(report.get('factory_erases_data') is True and bool(report.get('preserving_sdk_migration_blocker')),
            'Factory erase and preserving-migration blocker must be explicit')
    require(report.get('input_lock') == lock and report.get('input_lock_sha256') == sha(lock_data), 'Qualification input lock differs')
    require(read('build/native-build-evidence/source-lock.json') == lock_data, 'Build/candidate source-lock bytes differ')
    closure = report['build_closure' if product == 'x4' else 'controlled_build']
    require(load_json(read('build/native-build-evidence/build-closure.json')) == closure, 'Controlled build closure differs')
    require(closure.get('fresh_build') is True and closure.get('same_process_qualification') is True
            and closure.get('compile_jobs') == 1 and closure.get('detached_receipt_is_attestation') is False,
            'Missing same-process fresh single-job build qualification')
    require(closure['sources'] == lock['sources'], 'Controlled build source pins differ')
    require(set(closure['artifacts']) == set(lock['native_files']), 'Controlled/native artifact key set differs')
    for key, record in lock['native_files'].items():
        path = 'build/' + safe_member(record['path'])
        expected = {field: record[field] for field in ('bytes', 'sha256')}
        require(row['members'].get(path) == expected and closure['artifacts'].get(key) == expected,
                'Native locked/qualified build artifact differs: ' + key)
    baseline = read('metadata/released-baseline.json')
    require(lock.get('baseline_contract_sha256') == sha(baseline)
            and report.get('baseline_contract') == load_json(baseline)
            and report['baseline_contract'].get('product') == product, 'Released baseline contract differs')
    require('15.2.0' in lock['compiler']['version'] and 'esp-15.2.0_20251204' in lock['compiler']['version'], 'Exact GCC15 compiler missing')
    payloads = PAYLOADS | ({X4_SD} if product == 'x4' else set())
    require(set(report['artifacts']) == payloads, 'Unexpected or incomplete qualified product payload set')
    candidate_members = {name.removeprefix('candidate/') for name in row['members'] if name.startswith('candidate/')}
    require(candidate_members == payloads | {'qualification.json', 'source-lock.json', 'SHA256SUMS'}, 'Candidate inventory differs')
    for name, expected in report['artifacts'].items():
        require(row['members'].get('candidate/' + name) == expected, 'Qualified product asset hash differs: ' + name)
    sums = ''.join(row['members']['candidate/' + name]['sha256'] + '  ' + name + '\n'
                   for name in sorted(candidate_members - {'SHA256SUMS'}))
    require(read('candidate/SHA256SUMS') == sums.encode(), 'Candidate SHA256SUMS incomplete/different')
    bindings = {'firmware.bin': 'firmware_bin', 'bootloader.bin': 'bootloader', 'partitions.bin': 'partitions'}
    for candidate_name, build_key in bindings.items():
        require(report['artifacts'][candidate_name] == {field: lock['native_files'][build_key][field] for field in ('bytes', 'sha256')},
                'Product payload differs from locked native build')
    if product == 'x4':
        require(report.get('sd_unchanged') is True and report.get('device_msc_sd_workflow_preserved') is True, 'X4 SD workflow preservation missing')
        memory = report['native']['x4_bootlog_memory']
        require(memory.get('input_kind') == 'final-elf' and memory.get('copied_bytes_external') == 4528, 'X4 bootlog placement differs')
        for field, count in (('admission', 43), ('admission_sanitized', 43), ('sd_admission', 49), ('sd_admission_sanitized', 49)):
            require(report[field].get('elf_count') == count, 'X4 exact ELF inventory differs: ' + field)
        require(report['artifacts'][X4_SD] == {key: report['baseline_contract']['sd'][key] for key in ('bytes', 'sha256')}, 'X4 released SD archive changed')
        with zipfile.ZipFile(io.BytesIO(read('candidate/' + X4_SD))) as sd_archive:
            zip_inventory(sd_archive)
    return report


def validate_archive(manifest, product, path):
    row = manifest['artifacts'][product]
    require(path.is_file() and not path.is_symlink(), 'Original CI ZIP must be a regular file')
    require(path.stat().st_size <= MAX_ARCHIVE_BYTES, 'CI ZIP exceeds byte bound')
    require(file_record(path.read_bytes()) == {key: row[key] for key in ('bytes', 'sha256')}, 'Original CI ZIP bytes differ')
    with zipfile.ZipFile(path) as archive:
        inventory = zip_inventory(archive)
        require(set(inventory) == set(row['members']), 'ZIP member inventory differs')
        for name in inventory:
            checked_data(archive, name, row['members'][name])
        read = lambda name: checked_data(archive, name, row['members'][name])
        require(sha(read('metadata/workflow.yml')) == manifest['source_workflow_sha256'], 'Archived CI workflow differs')
        metadata = load_json(read('metadata/ci-run.json'))
        identity = {'GITHUB_REPOSITORY': REPOSITORY, 'GITHUB_SHA': manifest['source_sha'],
                    'GITHUB_REF': 'refs/heads/' + BRANCH, 'GITHUB_RUN_ID': str(manifest['run_id']),
                    'GITHUB_RUN_ATTEMPT': str(manifest['run_attempt']),
                    'GITHUB_WORKFLOW_REF': REPOSITORY + '/' + BUILD_WORKFLOW + '@refs/heads/' + BRANCH,
                    'GITHUB_WORKFLOW_SHA': manifest['source_sha'], 'PRODUCT': product,
                    'SDK_SOURCE': SDK, 'SDK_IMAGE': SDK_IMAGE}
        for key, value in identity.items():
            require(metadata.get(key) == value, 'Archived CI identity differs: ' + key)
        before = load_json(read('metadata/sources-before.json'))
        after = load_json(read('metadata/sources-after.json'))
        require(before == after and before.get('schema') == 1, 'Source custody changed during build')
        require(set(before['sources']) == set(row['sources']), 'CI source role set differs')
        for role, pin in row['sources'].items():
            actual = before['sources'][role]
            require(actual.get('clean') is True and {key: actual[key] for key in ('revision', 'tree', 'submodules')} == pin,
                    'CI source custody differs: ' + role)
        if product == 'generic':
            require(set(GENERIC_ASSETS) <= set(inventory), 'Generic native Runtime outputs incomplete')
            require(not any(name.startswith('candidate/') for name in inventory), 'Generic Runtime must not masquerade as a product candidate')
        else:
            for role, key in (('product', 'PRODUCT_SOURCE'), ('system', 'SYSTEM_SOURCE'), ('drivers', 'DRIVERS_SOURCE'), ('image_helper', 'IMAGE_HELPER_SOURCE')):
                require(metadata.get(key) == row['sources'][role]['revision'], 'CI matrix source pin differs: ' + role)
            validate_product(product, archive, row)


def publication_context(manifest, environ):
    require(validate_manifest(manifest), 'Publication manifest is disabled')
    require(environ.get('GITHUB_ACTIONS') == 'true' and environ.get('RUNNER_ENVIRONMENT') == 'github-hosted',
            'Publication is only supported in GitHub-hosted Actions')
    require(environ.get('GITHUB_REPOSITORY') == REPOSITORY and environ.get('GITHUB_EVENT_NAME') == 'push'
            and environ.get('GITHUB_REF') == 'refs/heads/' + BRANCH, 'Publication event/repository/ref differs')
    require(environ.get('GITHUB_WORKFLOW_REF') == REPOSITORY + '/' + RELEASE_WORKFLOW + '@refs/heads/' + BRANCH,
            'Publication workflow identity differs')
    require(re.fullmatch('[0-9a-f]{40}', environ.get('GITHUB_SHA', ''))
            and environ.get('GITHUB_WORKFLOW_SHA') == environ['GITHUB_SHA'], 'Publication workflow SHA differs')
    require(environ.get('GITHUB_TOKEN'), 'Only the job-scoped GITHUB_TOKEN may publish')


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class GitHub:
    """GitHub-owned token use only; signed redirect downloads never receive it."""
    def __init__(self, token):
        self.token = token
        self.opener = urllib.request.build_opener(NoRedirect())

    def request(self, url, method='GET', data=None, accept='application/vnd.github+json', content_type=None):
        parsed = urllib.parse.urlsplit(url)
        require(parsed.scheme == 'https' and parsed.hostname in ('api.github.com', 'uploads.github.com'), 'Unexpected authenticated API host')
        headers = {'Authorization': 'Bearer ' + self.token, 'Accept': accept,
                   'X-GitHub-Api-Version': '2022-11-28', 'User-Agent': 'riscrte-native-idf61-release'}
        if content_type:
            headers['Content-Type'] = content_type
        return self.opener.open(urllib.request.Request(url, data=data, headers=headers, method=method), timeout=120)

    def api(self, path, method='GET', payload=None, absent_ok=False):
        try:
            with self.request('https://api.github.com/' + path, method,
                              None if payload is None else encoded(payload), content_type='application/json') as response:
                return load_json(response.read())
        except urllib.error.HTTPError as error:
            if absent_ok and error.code == 404:
                return None
            raise

    def download(self, path, destination):
        require(not destination.exists(), 'Download must not replace an existing file')
        # Actions archive endpoints negotiate GitHub JSON before redirecting;
        # only Release asset endpoints use the octet-stream media type.
        accept = ('application/vnd.github+json' if '/actions/artifacts/' in path
                  and path.endswith('/zip') else 'application/octet-stream')
        try:
            response = self.request('https://api.github.com/' + path, accept=accept)
        except urllib.error.HTTPError as error:
            require(error.code in (301, 302, 303, 307, 308),
                    f'Artifact/release download failed: HTTP {error.code} at {path}')
            url = error.headers['Location']
            for _ in range(6):
                parsed = urllib.parse.urlsplit(url)
                host = parsed.hostname or ''
                require(parsed.scheme == 'https' and parsed.username is None and parsed.port in (None, 443)
                        and any(host.endswith(suffix) for suffix in ('.blob.core.windows.net', '.githubusercontent.com', '.amazonaws.com')),
                        'Unexpected signed download destination')
                try:
                    # Never forward a GitHub token to signed storage URLs.
                    response = self.opener.open(urllib.request.Request(url), timeout=120)
                    break
                except urllib.error.HTTPError as redirect:
                    require(redirect.code in (301, 302, 303, 307, 308),
                            f'Signed download failed: HTTP {redirect.code}')
                    url = redirect.headers['Location']
            else:
                raise ValueError('Too many signed download redirects')
        count = 0
        with response, destination.open('xb') as output:
            while True:
                data = response.read(1024 * 1024)
                if not data:
                    break
                count += len(data)
                require(count <= MAX_ARCHIVE_BYTES, 'Download exceeds byte bound')
                output.write(data)

    def upload(self, release_id, path):
        url = f'https://uploads.github.com/repos/{REPOSITORY}/releases/{release_id}/assets?name=' + urllib.parse.quote(path.name, safe='')
        with self.request(url, 'POST', path.read_bytes(), content_type='application/octet-stream') as response:
            return load_json(response.read())


def check_remote_ci(client, manifest):
    base = f'repos/{REPOSITORY}'
    run = client.api(f'{base}/actions/runs/{manifest["run_id"]}')
    workflow = client.api(f'{base}/actions/workflows/{manifest["workflow_id"]}')
    jobs = client.api(f'{base}/actions/runs/{manifest["run_id"]}/attempts/{manifest["run_attempt"]}/jobs?per_page=100')
    artifacts = client.api(f'{base}/actions/runs/{manifest["run_id"]}/artifacts?per_page=100')
    validate_ci(manifest, run, workflow, jobs, artifacts)
    source = client.api(f'{base}/contents/{BUILD_WORKFLOW}?ref={manifest["source_sha"]}')
    require(source.get('encoding') == 'base64' and sha(base64.b64decode(source['content'])) == manifest['source_workflow_sha256'],
            'Source workflow bytes differ from reviewed manifest')
    version = client.api(f'{base}/contents/platformio.ini?ref={manifest["source_sha"]}')
    require(version.get('encoding') == 'base64', 'Source version file unavailable')
    parser = configparser.ConfigParser(interpolation=None)
    parser.read_string(base64.b64decode(version['content']).decode())
    require(parser['riscrte']['version'] == VERSION, 'Runtime source version differs')


def ensure_new_release(client):
    base = f'repos/{REPOSITORY}'
    require(client.api(f'{base}/git/ref/tags/{TAG}', absent_ok=True) is None, 'Tag already exists; never overwrite or adopt it')
    require(client.api(f'{base}/releases/tags/{TAG}', absent_ok=True) is None, 'Release already exists; never overwrite or adopt it')
    # Include drafts: a prior incomplete attempt is never silently resumed.
    page = 1
    while True:
        releases = client.api(f'{base}/releases?per_page=100&page={page}')
        require(isinstance(releases, list), 'Invalid release listing')
        require(all(release['tag_name'] != TAG for release in releases), 'Existing draft/release blocks publication')
        if len(releases) < 100:
            break
        page += 1


def stage_assets(manifest, archives, output):
    require(not output.exists(), 'Release staging output must be new')
    output.mkdir()
    for product, source in archives.items():
        validate_archive(manifest, product, source)
        # Original compressed bytes, never extract/recompress the evidence ZIP.
        (output / ZIP_ASSETS[product]).write_bytes(source.read_bytes())
    with zipfile.ZipFile(archives['generic']) as archive:
        for member, asset in GENERIC_ASSETS.items():
            (output / asset).write_bytes(checked_data(archive, member, manifest['artifacts']['generic']['members'][member]))
    (output / 'Runtime-0.4.0-release-manifest.json').write_bytes(encoded(manifest))
    notice = (
        'Runtime0.4.0: official ESP-IDF 6.1 / GCC 15.2.0 software-qualified artifacts.\n'
        'Hardware validation remains pending for every product.\n'
        'Generic Runtime images are NOT device product flash images. They omit the product package/configuration.\n'
        'Product factory images ERASE DATA. No device flashing is performed or authorized by publication.\n'
        'Preserving SDK migration is NOT YET ELIGIBLE: deployed bootloader, candidate boot, health confirmation and rollback require hardware validation.\n'
        'No preserving-update kit is included. Product ZIPs retain the exact qualified CI bytes; downstream packaging must verify the ZIP and all payload hashes without rebuilding.\n'
    )
    (output / 'Runtime-0.4.0-READ-ME-FIRST.txt').write_text(notice)
    sums = ''.join(sha(path.read_bytes()) + '  ' + path.name + '\n' for path in sorted(output.iterdir()))
    (output / 'SHA256SUMS').write_text(sums)
    return {path.name: file_record(path.read_bytes()) for path in sorted(output.iterdir())}


def verify_release_bytes(client, release, expected, target, *, draft):
    require(release.get('tag_name') == TAG and release.get('draft') is draft and release.get('prerelease') is True,
            'Release identity/state differs')
    rows = release.get('assets', [])
    require(len(rows) == len(expected) and {row['name'] for row in rows} == set(expected), 'Uploaded asset inventory differs')
    require(len({row['id'] for row in rows}) == len(rows), 'Duplicate release asset identity')
    for row in rows:
        record = expected[row['name']]
        require(row.get('state') == 'uploaded' and row.get('size') == record['bytes']
                and row.get('digest') == 'sha256:' + record['sha256'], 'Uploaded asset metadata differs')
        path = target / row['name']
        client.download(f'repos/{REPOSITORY}/releases/assets/{row["id"]}', path)
        require(file_record(path.read_bytes()) == record, 'Redownloaded release bytes differ')


def publish(manifest, manifest_bytes, environ, client=None):
    publication_context(manifest, environ)
    client = client or GitHub(environ['GITHUB_TOKEN'])
    base = f'repos/{REPOSITORY}'
    # The manifest must itself come from this exact publication commit.
    committed = client.api(f'{base}/contents/{MANIFEST_PATH}?ref={environ["GITHUB_SHA"]}')
    require(committed.get('encoding') == 'base64' and base64.b64decode(committed['content']) == manifest_bytes,
            'Publication manifest differs from triggering commit')
    check_remote_ci(client, manifest)
    ensure_new_release(client)
    with tempfile.TemporaryDirectory(prefix='runtime-idf61-release-') as directory:
        root = Path(directory)
        archives = {}
        for product, row in manifest['artifacts'].items():
            path = root / (product + '.zip')
            client.download(f'{base}/actions/artifacts/{row["id"]}/zip', path)
            archives[product] = path
        output = root / 'assets'
        inventory = stage_assets(manifest, archives, output)
        # Recheck immediately before the first mutation, including rerun attempts.
        check_remote_ci(client, manifest)
        ensure_new_release(client)
        # Atomic ref creation refuses races. A later failure leaves a blocked tag/draft;
        # it never edits or deletes a pre-existing release or tag, even on retries.
        created_tag = client.api(f'{base}/git/refs', 'POST', {'ref': 'refs/tags/' + TAG, 'sha': manifest['source_sha']})
        require(created_tag.get('ref') == 'refs/tags/' + TAG and created_tag.get('object', {}).get('sha') == manifest['source_sha'],
                'Tag creation identity differs')
        notes = (output / 'Runtime-0.4.0-READ-ME-FIRST.txt').read_text()
        notes += f'\nSource: {manifest["source_sha"]}\nCI: https://github.com/{REPOSITORY}/actions/runs/{manifest["run_id"]}/attempts/{manifest["run_attempt"]}\n'
        release = client.api(f'{base}/releases', 'POST', {'tag_name': TAG, 'target_commitish': manifest['source_sha'],
                             'name': 'Runtime0.4.0 — Native ESP-IDF 6.1', 'body': notes,
                             'draft': True, 'prerelease': True, 'make_latest': 'false'})
        release_id = release['id']
        require(type(release_id) is int and release_id > 0 and release.get('draft') is True and release.get('tag_name') == TAG,
                'New draft release identity differs')
        for path in sorted(output.iterdir()):
            client.upload(release_id, path)
        draft = client.api(f'{base}/releases/{release_id}')
        downloaded = root / 'verify-draft'; downloaded.mkdir()
        verify_release_bytes(client, draft, inventory, downloaded, draft=True)
        tag = client.api(f'{base}/git/ref/tags/{TAG}')
        require(tag.get('object', {}).get('sha') == manifest['source_sha'], 'Release tag moved during upload')
        # Only this invocation's newly-created draft ID may become public.
        client.api(f'{base}/releases/{release_id}', 'PATCH', {'draft': False, 'make_latest': 'false'})
        published = client.api(f'{base}/releases/{release_id}')
        downloaded = root / 'verify-public'; downloaded.mkdir()
        verify_release_bytes(client, published, inventory, downloaded, draft=False)
        require(published.get('html_url') == f'https://github.com/{REPOSITORY}/releases/tag/{TAG}', 'Unexpected public release URL')
        print('Published and redownload-verified:', published['html_url'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('check-manifest', 'verify-archives', 'publish'))
    parser.add_argument('--manifest', type=Path, default=Path(MANIFEST_PATH))
    parser.add_argument('--archives', type=Path, help='Directory with generic.zip, twatch.zip, waveshare.zip, x4.zip')
    parser.add_argument('--github-output', type=Path)
    args = parser.parse_args()
    raw = args.manifest.read_bytes()
    manifest = load_json(raw)
    enabled = validate_manifest(manifest)
    if args.command == 'check-manifest':
        if args.github_output:
            with args.github_output.open('a') as stream:
                stream.write('enabled=' + str(enabled).lower() + '\n')
        print('Release manifest enabled' if enabled else 'Release manifest disabled; no publication')
    elif args.command == 'verify-archives':
        require(enabled and args.archives is not None, 'Enabled manifest and archive directory required')
        for product in PRODUCTS:
            validate_archive(manifest, product, args.archives / (product + '.zip'))
        print('All four original CI ZIPs verified; no publication performed')
    else:
        publish(manifest, raw, os.environ)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, TypeError, zipfile.BadZipFile, urllib.error.URLError) as error:
        sys.exit('Native release refused: ' + str(error))
