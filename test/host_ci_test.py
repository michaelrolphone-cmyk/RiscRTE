"""Coverage, failure reporting and real ccache invalidation regression tests."""
import contextlib
import hashlib
import io
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / f'{name}.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


cache = load('host_compiler_cache')
suite = load('run_host_suite')


class CoverageTests(unittest.TestCase):
    def test_preserves_every_baseline_command_once(self):
        commands = json.loads((ROOT / 'test/host_suite.json').read_text())
        # Exact 184-command source9f8d670 baseline. Intentional suite edits must
        # update this digest alongside the documented coverage change.
        self.assertEqual(hashlib.sha256(('\n'.join(commands) + '\n').encode()).hexdigest(),
                         'd56d8fcedd4cf37a8cc3bef824af3cb883fe3f4ef1ad0d255c9e58b93edf4a13')
        assigned = list(suite.assignments(commands))
        self.assertEqual([c for _, c in assigned], commands)
        self.assertEqual(len(assigned), 184)
        self.assertEqual(sum('SANITIZE=1' in c for c in commands), 82)
        self.assertEqual(sum('TSAN=1' in c for c in commands), 2)
        self.assertEqual(set(s for s, _ in assigned), set(suite.SHARDS))
        self.assertEqual(sum(s == 'normal' for s, _ in assigned), 90)
        self.assertEqual(sum(s == 'asan-1' for s, _ in assigned), 41)
        self.assertEqual(sum(s == 'asan-2' for s, _ in assigned), 41)
        self.assertEqual(sum(s == 'tools' for s, _ in assigned), 12)

    def test_failure_remains_visible_and_later_commands_run(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'timings.json'
            with contextlib.redirect_stdout(io.StringIO()), mock.patch.dict(os.environ):
                os.environ.pop('GITHUB_STEP_SUMMARY', None)
                self.assertEqual(suite.run(['true', 'false', 'true'], 'normal', path), 1)
            records = json.loads(path.read_text())
            self.assertEqual([r['exit_code'] for r in records], [0, 1, 0])
            self.assertTrue(all(r['seconds'] >= 0 for r in records))

    def test_unsupported_commands_bypass(self):
        source = '/repo/src/bootstrap/Runtime.cpp'
        for option in ['-c', '-E', '-S', '-flto', '-fprofile-arcs', '--coverage',
                       '-save-temps', '-MD', '-MF', '@args', '@args.o', '@args.cpp', '-Wp,-MD,dependencies.d', '-Wa,-a=listing.s',
                       '-fno-integrated-as', '-x', '-fplugin=x', '-fsyntax-only']:
            self.assertIsNone(cache.split_command([source, option, '-o', 'out']), option)
        self.assertIsNone(cache.split_command(['other.cpp', '-o', 'out']))
        self.assertIsNone(cache.split_command([source, source, '-o', 'out']))

    def test_separates_compile_and_link_flags_without_reordering_link(self):
        source = '/repo/src/bootstrap/Runtime.cpp'
        args = ['-std=c++17', '-I', '/include', '-DVALUE=3', source, 'test.cpp',
                'support.o', '-pthread', '-rdynamic', '-Wl,--wrap=read', '-ldl', '-o', 'out']
        self.assertEqual(cache.split_command(args),
                         (4, ['-std=c++17', '-I', '/include', '-DVALUE=3', '-pthread']))


@unittest.skipUnless(shutil.which('ccache') and shutil.which('g++'), 'requires ccache and g++')
class CacheInvalidationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        source_dir = self.root / 'src/bootstrap'
        source_dir.mkdir(parents=True)
        self.source = source_dir / 'Runtime.cpp'
        self.header = source_dir / 'value.h'
        self.header.write_text('#define HEADER 1\n')
        self.source.write_text('#include "value.h"\n#ifndef VALUE\n#define VALUE 10\n#endif\n'
                               'int value(int n) { return VALUE + HEADER + n; }\n')
        self.main = self.root / 'main.cpp'
        self.main.write_text('#include <cstdio>\nint value(int); int main() { std::printf("%d\\n", value(1)); }\n')
        self.env = os.environ.copy()
        self.env.update(CCACHE_DIR=str(self.root / 'cache'), CCACHE_COMPILERCHECK='content',
                        CCACHE_NODIRECT='true', CCACHE_SLOPPINESS='', CCACHE_COMPILERTYPE='gcc')
        self.env.pop('CCACHE_DISABLE', None)
        self.env.pop('RISC_HOST_CACHE_DISABLE', None)
        # Isolate installation from the workflow's already-installed wrapper.
        self.real = shutil.which('g++')
        if Path(self.real).is_symlink() and Path(self.real).resolve().name == 'host_compiler_cache.py':
            self.real = json.loads((Path(self.real).parent / 'compilers.json').read_text())['g++']
        self.wrappers = self.root / 'wrappers'
        self.wrappers.mkdir()
        (self.wrappers / 'c++').symlink_to(ROOT / 'scripts/host_compiler_cache.py')
        (self.wrappers / 'compilers.json').write_text(json.dumps({'c++': self.real}))
        subprocess.run(['ccache', '--zero-stats'], env=self.env, check=True, capture_output=True)

    def stats(self):
        result = subprocess.check_output(['ccache', '--print-stats'], env=self.env, text=True)
        return {line.split()[0]: int(line.split()[1]) for line in result.splitlines()}

    def build(self, flags=(), expected=12, run=True):
        outdir = Path(tempfile.mkdtemp(dir=self.root))
        output = outdir / 'test'
        result = subprocess.run([str(self.wrappers / 'c++'), '-std=c++17', '-g', *flags,
                                 str(self.source), str(self.main), '-o', str(output)],
                                env=self.env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        if run:
            self.assertEqual(subprocess.check_output([str(output)], text=True).strip(), str(expected))
        return output

    def test_reuses_across_temporary_outputs_and_invalidates_all_inputs(self):
        self.build()
        first = self.stats()['cache_miss']
        self.build()
        self.assertEqual(self.stats()['cache_miss'], first)
        self.assertGreater(self.stats()['preprocessed_cache_hit'], 0)
        self.build(['-DVALUE=20'], expected=22)
        self.assertEqual(self.stats()['cache_miss'], first + 1)
        self.header.write_text('#define HEADER 2\n')
        self.build(expected=13)
        self.assertEqual(self.stats()['cache_miss'], first + 2)
        self.source.write_text(self.source.read_text().replace('+ n;', '+ n + 1;'))
        self.build(expected=14)
        self.assertEqual(self.stats()['cache_miss'], first + 3)
        self.build(['-O2'], expected=14)
        self.assertEqual(self.stats()['cache_miss'], first + 4)
        sanitized = self.build(['-fsanitize=address,undefined', '-fno-sanitize-recover=all'], run=False)
        self.assertIn('__asan_init', subprocess.check_output(['nm', '-u', str(sanitized)], text=True))
        self.assertEqual(self.stats()['cache_miss'], first + 5)
        self.build(expected=14)
        self.assertEqual(self.stats()['cache_miss'], first + 5)

    def test_compiler_content_and_failed_compilation(self):
        compiler = self.root / 'compiler'
        compiler.write_text(f'#!/bin/sh\nexec {self.real} "$@"\n')
        compiler.chmod(0o755)
        (self.wrappers / 'compilers.json').write_text(json.dumps({'c++': str(compiler)}))
        self.build()
        misses = self.stats()['cache_miss']
        compiler.write_text(compiler.read_text() + '# compiler revision changes\n')
        self.build()
        self.assertEqual(self.stats()['cache_miss'], misses + 1)
        self.source.write_text('syntax error\n')
        result = subprocess.run([str(self.wrappers / 'c++'), str(self.source), str(self.main),
                                 '-o', str(self.root / 'bad')], env=self.env, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / 'bad').exists())


if __name__ == '__main__':
    unittest.main()
