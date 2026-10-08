import copy
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/publish_watch_source.py'
if not SCRIPT.exists():
    SCRIPT = Path(__file__).with_name('publish_watch_source.py')
SPEC = importlib.util.spec_from_file_location('watch_source', SCRIPT)
pub = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pub)

SOURCE, VERIFY, TREE = 'a' * 40, 'b' * 40, 'c' * 40
CONFIG = {
    'schema': 1, 'repository': 'owner/repo', 'tag': 'watch-v1.0.12',
    'title': 'Watch 1.0.12 source snapshot',
    'source': {'local_commit': 'd' * 40, 'public_commit': SOURCE, 'tree': TREE},
    'verification': {'commit': VERIFY, 'checks': ['build']}, 'notes': 'Source custody.'}


def git_stub(*args):
    if args[0] == 'rev-parse':
        return TREE if args[1].endswith('^{tree}') else SOURCE
    return ''


class FakeAPI:
    def __init__(self):
        self.target = None
        self.release = None
        self.assets = []
        self.writes = []
        self.checks = [{'id': 1, 'name': 'build', 'head_sha': VERIFY,
                        'app': {'slug': 'github-actions'}, 'status': 'completed',
                        'conclusion': 'success'}]
        self.annotated = False

    def request(self, path, data=None):
        if data is not None:
            self.writes.append((path, data))
            if path == '/git/refs':
                self.target = data['sha']
                return {'object': {'type': 'commit', 'sha': self.target}}
            if path == '/releases':
                self.release = dict(data, id=42, html_url='https://github.com/owner/repo/releases/tag/watch-v1.0.12')
                return self.release
            raise AssertionError(path)
        if path.startswith('/commits/'):
            return {'total_count': len(self.checks), 'check_runs': self.checks}
        if path.startswith('/git/ref/tags/'):
            if self.target is None:
                return None
            return {'object': {'type': 'tag' if self.annotated else 'commit', 'sha': self.target}}
        if path.startswith('/git/tags/'):
            return {'object': {'type': 'commit', 'sha': self.target}}
        if path.startswith('/releases/tags/'):
            return self.release
        if path.startswith('/releases/42/assets?'):
            return self.assets
        raise AssertionError(path)

    def upload(self, release_id, name, payload):
        self.writes.append(('upload', name))
        asset = {'name': name, 'size': len(payload), 'state': 'uploaded',
                 'digest': 'sha256:' + hashlib.sha256(payload).hexdigest()}
        self.assets.append(asset)
        return asset


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.config = copy.deepcopy(CONFIG)
        self.api = FakeAPI()

    def publish(self, command=git_stub):
        pub.publish(self.config, 'owner/repo', self.api, command)

    def test_publish_exact_source_and_non_latest(self):
        self.publish()
        self.assertEqual(self.api.target, SOURCE)
        self.assertEqual(self.api.release['target_commitish'], SOURCE)
        self.assertEqual(self.api.release['make_latest'], 'false')
        self.assertEqual(len(self.api.assets), 1)

    def test_idempotent_without_writes(self):
        self.publish()
        self.api.writes.clear()
        self.publish()
        self.assertEqual(self.api.writes, [])

    def test_existing_annotated_tag(self):
        self.api.target, self.api.annotated = SOURCE, True
        self.publish()
        self.assertNotIn('/git/refs', [w[0] for w in self.api.writes])

    def test_tag_collision_before_writes(self):
        self.api.target = 'e' * 40
        with self.assertRaisesRegex(ValueError, 'tag collision'):
            self.publish()
        self.assertEqual(self.api.writes, [])

    def test_latest_failed_check_not_masked(self):
        self.api.checks.append(dict(self.api.checks[0], id=2, conclusion='failure'))
        with self.assertRaisesRegex(ValueError, 'CI not successful'):
            self.publish()
        self.assertEqual(self.api.writes, [])

    def test_missing_or_pending_ci(self):
        for checks in [[], [dict(self.api.checks[0], status='in_progress', conclusion=None)]]:
            self.api.checks = checks
            with self.assertRaisesRegex(ValueError, 'CI not successful'):
                self.publish()
        self.assertEqual(self.api.writes, [])

    def test_wrong_commit_or_app_cannot_supply_ci(self):
        for field, value in [('head_sha', SOURCE), ('app', {'slug': 'unrelated'})]:
            self.api = FakeAPI()
            self.api.checks[0][field] = value
            with self.assertRaisesRegex(ValueError, 'CI not successful'):
                self.publish()
        self.assertEqual(self.api.writes, [])

    def test_wrong_source_tree_before_writes(self):
        with self.assertRaisesRegex(ValueError, 'tree mismatch'):
            self.publish(lambda *args: 'e' * 40 if args[-1].endswith('^{tree}') else git_stub(*args))
        self.assertEqual(self.api.writes, [])

    def test_unmerged_source_before_writes(self):
        def command(*args):
            if args[0] == 'merge-base':
                raise subprocess.CalledProcessError(1, args)
            return git_stub(*args)
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish(command)
        self.assertEqual(self.api.writes, [])

    def test_existing_release_metadata_collision(self):
        self.publish()
        self.api.writes.clear()
        self.api.release['body'] = 'Other release'
        with self.assertRaisesRegex(ValueError, 'metadata collision'):
            self.publish()
        self.assertEqual(self.api.writes, [])

    def test_existing_provenance_collision(self):
        self.publish()
        self.api.writes.clear()
        self.api.assets[0]['digest'] = 'sha256:' + '0' * 64
        with self.assertRaisesRegex(ValueError, 'asset collision'):
            self.publish()
        self.assertEqual(self.api.writes, [])

    def test_missing_asset_can_complete_partial_publication(self):
        self.publish()
        self.api.assets.clear()
        self.api.writes.clear()
        self.publish()
        self.assertEqual(self.api.writes, [('upload', pub.asset_name(self.config))])

    def test_config_rejects_wrong_repository_or_mutable_ref(self):
        with self.assertRaisesRegex(ValueError, 'repository mismatch'):
            pub.validate(self.config, 'another/repo')
        self.config['source']['public_commit'] = 'main'
        with self.assertRaisesRegex(ValueError, 'immutable'):
            pub.validate(self.config, 'owner/repo')

    def test_real_git_checks_source_tree_and_ancestry(self):
        with tempfile.TemporaryDirectory() as folder:
            def command(*args):
                return subprocess.check_output(['git', '-C', folder, *args], text=True,
                                               stderr=subprocess.DEVNULL).strip()
            command('init', '-q')
            command('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                    'commit', '--allow-empty', '-qm', 'source')
            source = command('rev-parse', 'HEAD')
            tree = command('rev-parse', 'HEAD^{tree}')
            command('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                    'commit', '--allow-empty', '-qm', 'verification metadata')
            self.config['source'].update(public_commit=source, tree=tree)
            self.config['verification']['commit'] = command('rev-parse', 'HEAD')
            pub.verify_source(self.config, command)


if __name__ == '__main__':
    unittest.main()
