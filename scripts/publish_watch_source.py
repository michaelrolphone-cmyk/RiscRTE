#!/usr/bin/env python3
"""Publish an immutable, non-latest Watch cohort source snapshot."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import urllib.error
import urllib.parse
import urllib.request


def require(value, message):
    if not value:
        raise ValueError(message)


def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()


def validate(config, repository):
    require(config['schema'] == 1, 'Unsupported provenance schema')
    require(config['repository'] == repository and
            re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository),
            'Owning repository mismatch')
    require(config['tag'] == 'watch-v1.0.12', 'Unreviewed cohort tag')
    for key in ('local_commit', 'public_commit', 'tree'):
        require(re.fullmatch('[0-9a-f]{40}', config['source'][key]),
                'Require complete immutable source identities')
    require(re.fullmatch('[0-9a-f]{40}', config['verification']['commit']),
            'Require complete verification commit')
    checks = config['verification']['checks']
    require(checks and all(isinstance(name, str) and name for name in checks)
            and len(checks) == len(set(checks)), 'Require unique CI check names')


def verify_source(config, git_command=git):
    source = config['source']['public_commit']
    verify = config['verification']['commit']
    require(git_command('rev-parse', source + '^{commit}') == source,
            'Source is not the expected commit')
    require(git_command('rev-parse', source + '^{tree}') == config['source']['tree'],
            'Source tree mismatch')
    git_command('merge-base', '--is-ancestor', source, 'HEAD')
    git_command('merge-base', '--is-ancestor', source, verify)
    git_command('merge-base', '--is-ancestor', verify, 'HEAD')


class GitHub:
    def __init__(self, repository, token):
        self.repository = repository
        self.token = token

    def request(self, path, data=None):
        return self._request('https://api.github.com/repos/' + self.repository + path,
                             data=None if data is None else json.dumps(data).encode(),
                             content_type='application/json')

    def upload(self, release_id, name, payload):
        return self._request(
            'https://uploads.github.com/repos/' + self.repository +
            '/releases/' + str(int(release_id)) + '/assets?' +
            urllib.parse.urlencode({'name': name}), payload, 'application/json')

    def _request(self, url, data, content_type):
        request = urllib.request.Request(url, data=data, headers={
            'Authorization': 'Bearer ' + self.token,
            'Accept': 'application/vnd.github+json',
            'Content-Type': content_type,
            'X-GitHub-Api-Version': '2022-11-28'})
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if error.code == 404 and data is None:
                return None
            raise


def verify_ci(config, api):
    latest = {}
    page = 1
    commit = config['verification']['commit']
    while True:
        result = api.request('/commits/' + commit +
                             '/check-runs?per_page=100&page=' + str(page))
        require(result is not None, 'Verification checks unavailable')
        checks = result['check_runs']
        for check in checks:
            if (check.get('app', {}).get('slug') == 'github-actions' and
                    check['head_sha'] == commit and
                    check['id'] > latest.get(check['name'], {}).get('id', -1)):
                latest[check['name']] = check
        if page * 100 >= result['total_count']:
            break
        require(checks, 'Incomplete CI pagination')
        page += 1
    for name in config['verification']['checks']:
        check = latest.get(name)
        require(check and check['status'] == 'completed' and
                check['conclusion'] == 'success', 'Required CI not successful: ' + name)


def tag_target(api, tag):
    ref = api.request('/git/ref/tags/' + urllib.parse.quote(tag, safe=''))
    if ref is None:
        return None
    obj = ref['object']
    for _ in range(8):
        if obj['type'] == 'commit':
            return obj['sha']
        require(obj['type'] == 'tag', 'Existing tag does not resolve to a commit')
        annotated = api.request('/git/tags/' + obj['sha'])
        require(annotated is not None, 'Missing annotated tag')
        obj = annotated['object']
    raise ValueError('Excessive annotated tag nesting')


def release_body(config):
    source = config['source']
    return ('Source snapshot contributing to the accepted Watch 1.0.12 binary.\n\n'
            'Public commit: ' + source['public_commit'] + '\n'
            'Original local build commit: ' + source['local_commit'] + '\n'
            'Identical source tree: ' + source['tree'] + '\n\n'
            + config.get('notes', '') + '\n\n'
            'GitHub source archives refer to the exact public commit. The attached '
            'provenance records the original build mapping and CI verification identity. '
            'Deployable binaries belong to the Watch product release. '
            'This cohort snapshot does not replace the repository latest release.\n')


def asset_name(config):
    return config['tag'] + '-source-provenance.json'


def provenance(config):
    return (json.dumps(config, indent=2, sort_keys=True) + '\n').encode()


def verify_release(release, config):
    require(release['tag_name'] == config['tag'] and not release['draft'] and
            not release['prerelease'], 'Existing release identity/state collision')
    require(release['name'] == config['title'] and release['body'] == release_body(config),
            'Existing release metadata collision; no overwrite permitted')


def existing_asset(api, release, config):
    assets = []
    page = 1
    while True:
        batch = api.request('/releases/' + str(release['id']) +
                            '/assets?per_page=100&page=' + str(page))
        require(batch is not None, 'Release assets unavailable')
        assets.extend(batch)
        if len(batch) < 100:
            break
        page += 1
    matches = [a for a in assets if a['name'] == asset_name(config)]
    require(len(matches) <= 1, 'Duplicate provenance asset')
    if matches:
        payload = provenance(config)
        require(matches[0]['state'] == 'uploaded' and matches[0]['size'] == len(payload)
                and matches[0].get('digest') == 'sha256:' + hashlib.sha256(payload).hexdigest(),
                'Provenance asset collision; no overwrite permitted')
        return matches[0]
    return None


def publish(config, repository, api, git_command=git):
    validate(config, repository)
    verify_source(config, git_command)
    verify_ci(config, api)
    tag = config['tag']
    source = config['source']['public_commit']
    target = tag_target(api, tag)
    require(target in (None, source), 'Immutable tag collision')
    release = api.request('/releases/tags/' + tag)
    asset = None
    if release is not None:
        require(target == source, 'Existing release lacks expected source tag')
        verify_release(release, config)
        asset = existing_asset(api, release, config)
    if target is None:
        api.request('/git/refs', {'ref': 'refs/tags/' + tag, 'sha': source})
    require(tag_target(api, tag) == source, 'Tag verification failed')
    if release is None:
        release = api.request('/releases', {
            'tag_name': tag, 'target_commitish': source, 'name': config['title'],
            'body': release_body(config), 'draft': False, 'prerelease': False,
            'make_latest': 'false'})
        verify_release(release, config)
    if asset is None:
        api.upload(release['id'], asset_name(config), provenance(config))
    require(existing_asset(api, release, config), 'Provenance upload verification failed')
    require(tag_target(api, tag) == source, 'Final tag verification failed')
    print(release['html_url'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', default='release/watch-1.0.12-source.json')
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    config = json.loads(Path(args.config).read_text())
    repository = os.environ.get('GITHUB_REPOSITORY', config['repository'])
    validate(config, repository)
    verify_source(config)
    if args.verify_only:
        print('Source commit, tree and merged ancestry verified.')
        return
    event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
    require(os.environ['GITHUB_EVENT_NAME'] == 'push' and
            event['repository']['full_name'] == repository and
            os.environ['GITHUB_REF'] == 'refs/heads/' + event['repository']['default_branch'],
            'Only the owning default-branch push may publish')
    require(git('rev-parse', 'HEAD') == os.environ['GITHUB_SHA'], 'Checkout differs from push')
    publish(config, repository, GitHub(repository, os.environ['GH_TOKEN']))


if __name__ == '__main__':
    main()
