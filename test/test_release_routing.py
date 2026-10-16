"""Offline proof that current source cannot publish Arduino release bytes."""
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import publish_firmware as legacy


class ReleaseRoutingTests(unittest.TestCase):
    def test_default_branch_workflow_is_read_only_regression(self):
        workflow = (ROOT / '.github/workflows/release.yml').read_text()
        self.assertIn('name: Shared Runtime regression\n', workflow)
        self.assertIn('    branches: [main, master]\n', workflow)
        self.assertIn('  workflow_dispatch:\n', workflow)
        self.assertIn('  contents: read\n', workflow)
        self.assertIn('uses: ./.github/workflows/integration.yml', workflow)
        jobs = workflow.split('\njobs:\n', 1)[1]
        self.assertEqual(re.findall(r'^  ([a-z][a-z0-9_-]*):$', jobs, re.M),
                         ['integration'])
        self.assertNotIn('contents: write', workflow)
        self.assertNotIn('publish_firmware.py', workflow)

    def test_legacy_publisher_rejects_before_any_external_work(self):
        for source in ('a' * 40, '4107eaeedd8c801eeaf7deb1c7bc4a943cd27342'):
            with self.subTest(source=source), \
                    mock.patch.dict(os.environ, {}, clear=True), \
                    mock.patch('subprocess.Popen') as process, \
                    mock.patch('socket.create_connection') as connection, \
                    mock.patch('urllib.request.urlopen') as request:
                with self.assertRaisesRegex(ValueError, 'Legacy Arduino publication is retired'):
                    legacy.main(source)
                process.assert_not_called()
                connection.assert_not_called()
                request.assert_not_called()

    def test_retired_cli_has_no_credential_or_artifact_prerequisite(self):
        result = subprocess.run(
            [sys.executable, '-B', str(ROOT / 'scripts/publish_firmware.py'),
             '--source-sha', 'a' * 40],
            cwd=ROOT, env={'PATH': os.environ.get('PATH', '')},
            text=True, capture_output=True, check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Legacy Arduino publication is retired', result.stderr)
        self.assertNotIn('KeyError', result.stderr)
        self.assertNotIn('FileNotFoundError', result.stderr)


if __name__ == '__main__':
    unittest.main()
