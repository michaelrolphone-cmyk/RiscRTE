#!/usr/bin/env python3
"""Cache the expensive Runtime translation unit in host compile-and-link commands.

This is opt-in through --install. Ordinary developer/target compiler invocations
are unchanged. Unsupported compiler options fall back to the original command.
ccache owns content/compiler/flags/header validation, diagnostics and atomicity.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

SOURCE_SUFFIXES = {'.c', '.cc', '.cpp', '.cxx', '.C'}
VALUE_OPTIONS = {'-I', '-isystem', '-iquote', '-idirafter', '-include', '-imacros', '-D', '-U'}
# Only these common, audited host-test flags can be moved to a separate compile.
PREFIX_OPTIONS = ('-std=', '-D', '-U', '-I', '-fsanitize=', '-fvisibility=')
COMPILE_OPTIONS = {
    '-g', '-g0', '-g1', '-g2', '-g3', '-O0', '-O1', '-O2', '-O3', '-Os', '-Og',
    '-pthread', '-fPIC', '-fpic', '-fPIE', '-fpie', '-fno-pie',
    '-fno-omit-frame-pointer', '-fno-sanitize-recover=all', '-fno-access-control',
    '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers',
    '-Wno-misleading-indentation', '-Wno-unused-private-field',
    '-Wno-unused-but-set-variable', '-Wno-unused-function', '-Wno-unused-variable',
    '-Wno-unused-parameter', '-Wno-sign-compare',
}
LINK_OPTIONS = {'-rdynamic', '-shared', '-no-pie', '-static', '-static-libasan',
                '-static-libubsan', '-static-libstdc++', '-static-libgcc'}


def split_command(args: list[str]) -> tuple[int, list[str]] | None:
    """Return the Runtime argument index and compile flags, or fail safe."""
    runtime = []
    compile_flags = []
    output = False
    i = 0
    while i < len(args):
        arg = args[i]
        if arg.startswith('@'):
            return None
        if arg == '-o':
            if output or i + 1 >= len(args):
                return None
            output = True
            i += 2
            continue
        if arg in VALUE_OPTIONS:
            if i + 1 >= len(args):
                return None
            compile_flags.extend(args[i:i + 2])
            i += 2
            continue
        if arg in ('-l', '-L'):
            if i + 1 >= len(args):
                return None
            i += 2
            continue
        if arg in LINK_OPTIONS or arg.startswith(('-Wl,', '-l', '-L')):
            i += 1
            continue
        if not arg.startswith('-') and Path(arg).suffix in SOURCE_SUFFIXES:
            if Path(arg).parts[-3:] == ('src', 'bootstrap', 'Runtime.cpp'):
                runtime.append(i)
        elif not arg.startswith('-') and Path(arg).suffix in {'.o', '.a', '.so'}:
            pass
        elif arg in COMPILE_OPTIONS or arg.startswith(PREFIX_OPTIONS):
            # LTO, plugins, coverage/profiling, save-temps, language changes,
            # dependency outputs, response files and unknown options bypass.
            compile_flags.append(arg)
        else:
            return None
        i += 1
    if len(runtime) != 1 or not output:
        return None
    return runtime[0], compile_flags


def install(directory: Path) -> None:
    """Make per-job compiler wrappers without modifying compiler installations."""
    if not shutil.which('ccache'):
        raise SystemExit('ccache is required to enable the host compiler cache')
    directory.mkdir(parents=True, exist_ok=True)
    compilers = {name: shutil.which(name) for name in ('c++', 'g++', 'clang++')}
    compilers = {name: str(Path(path).resolve()) for name, path in compilers.items() if path}
    (directory / 'compilers.json').write_text(json.dumps(compilers))
    for name in compilers:
        (directory / name).symlink_to(Path(__file__).resolve())
    if os.environ.get('GITHUB_PATH'):
        with open(os.environ['GITHUB_PATH'], 'a') as handle:
            handle.write(str(directory.resolve()) + '\n')
    print(directory.resolve())


def main() -> int:
    if len(sys.argv) == 3 and sys.argv[1] == '--install':
        install(Path(sys.argv[2]))
        return 0
    invoked = Path(sys.argv[0]).absolute()
    compilers = json.loads((invoked.parent / 'compilers.json').read_text())
    compiler = compilers[invoked.name]
    args = sys.argv[1:]
    split = split_command(args)
    if split is None or os.environ.get('RISC_HOST_CACHE_DISABLE') == '1':
        return subprocess.call([compiler, *args])
    index, flags = split
    with tempfile.TemporaryDirectory(prefix='riscrte-host-object-') as directory:
        obj = str(Path(directory) / 'Runtime.o')
        result = subprocess.call(['ccache', compiler, *flags, '-c', args[index], '-o', obj])
        if result:
            return result
        # Keep every original linker option and argument position, and compile
        # all the remaining test/fixture translation units exactly as before.
        linked = args.copy()
        linked[index] = obj
        return subprocess.call([compiler, *linked])


if __name__ == '__main__':
    sys.exit(main())
