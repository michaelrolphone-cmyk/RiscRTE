#!/usr/bin/env python3
"""Host checks for actual loader code and the fail-closed internal adapter.

Requires Clang, an explicit host SDK/sysroot, and a separate clean baseline
checkout. Never executes target ELF instructions or changes either source tree.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

BASELINE = '13eca114cb58469cf6004fafc3da84fbd9d29beb'
LEGACY_SHA256 = '45c1ed06b26b523e6405cdbc470fa686c1df2ba9d4df44368c63095767b6d0ba'
CASES = ['legacy-zero-vaddr', 'legacy-port-unavailable', 'split', 'internal-zero-vaddr',
         'internal-entry', 'flags', 'alignment', 'duplicate', 'unknown-class', 'overlap',
         'unprivileged', 'unsupported', *['allocation-fail-' + str(i) for i in range(1, 8)],
         'publication-fail', 'repeat-preserves-custody', 'legacy-real']


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', type=Path, required=True)
    parser.add_argument('--host-sdk', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--normal-only', action='store_true', help='Run all normal profiles without UBSan')
    args = parser.parse_args()
    if not __debug__:
        raise ValueError('Python assertions must remain enabled')
    root = Path(__file__).resolve().parents[1]
    test = root / 'test/internal_mapping_v1'
    baseline = args.baseline.resolve(strict=True)
    cc = args.cc.resolve(strict=True)
    sdk = args.host_sdk.resolve(strict=True)
    out = args.output.resolve()
    if out.exists() or not sdk.is_dir() or root == baseline:
        raise ValueError('fresh output, explicit SDK and separate baseline required')
    if any(out == tree or tree in out.parents for tree in (root, baseline)):
        raise ValueError('output must be outside both source trees')
    out.mkdir(parents=True)
    env = {'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'LANG': 'C', 'TMPDIR': str(out)}
    report = {'status': 'started', 'commands': [], 'cases': [], 'source_inputs': {},
              'compiler_sha256': digest(cc), 'baseline_commit': BASELINE,
              'target_execution': False, 'internal_port_available': False,
              'physical_qualification': False}
    def save():
        (out / 'RESULT.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    def run(label, command):
        row = {'label': label, 'argv': list(map(str, command))}
        report['commands'].append(row); save()
        try:
            result = subprocess.run(row['argv'], env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, timeout=180)
        except subprocess.TimeoutExpired as error:
            result = subprocess.CompletedProcess(row['argv'], 124,
                error.stdout or b'', error.stderr or b'')
        row['returncode'] = result.returncode
        for name, data in (('stdout', result.stdout), ('stderr', result.stderr)):
            path = out / (label + '.' + name)
            path.write_bytes(data); row[name + '_sha256'] = digest(path)
            if result.returncode:
                row[name + '_tail'] = data[-4000:].decode('utf-8', 'replace')
        save()
        if result.returncode:
            raise RuntimeError(label + ' failed; see ' + str(out / (label + '.stderr')))
        return result.stdout.decode()
    def remember(path):
        path = Path(path).resolve(strict=True)
        value = digest(path)
        previous = report['source_inputs'].setdefault(str(path), value)
        if previous != value:
            raise RuntimeError('source changed: ' + str(path))
    def build(label, sources, includes, extra=()):
        binary = out / label
        flags = ['-isysroot', str(sdk), '-std=c11', '-D_DARWIN_C_SOURCE', '-O1', '-g',
                 '-Wall', '-Wextra', '-Werror', '-Wno-error=sign-compare',
                 '-Wno-format', '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable']
        if label.startswith('baseline-ordinary-'):
            # Exact untouched 32-bit target expression, measured on a 64-bit
            # host. It is never dereferenced/called by the ordinary fixture.
            flags.append('-Wno-error=pointer-to-int-cast')
        argv = [cc, *flags, *extra, *['-I' + str(path) for path in includes]]
        # Effective macro checks prevent a local header from disabling asserts.
        for index, source in enumerate(sources):
            remember(source)
            macros = run(label + '-macros-' + str(index), [*argv, '-dM', '-E', source])
            if re.search(r'^#define NDEBUG(?:\s|$)', macros, re.M):
                raise RuntimeError('effective NDEBUG: ' + str(source))
            run(label + '-syntax-' + str(index), [*argv, '-fsyntax-only', source])
            diagnostics = (out / (label + '-syntax-' + str(index) + '.stderr')).read_text()
            pointer_warnings = re.findall(
                r'^(.+?):(\d+):\d+: warning: .+? \[-Wpointer-to-int-cast\]$', diagnostics, re.M)
            expected_pointer = label.startswith('baseline-ordinary-') and index == 0
            if len(pointer_warnings) != (1 if expected_pointer else 0):
                raise RuntimeError('unexpected baseline pointer diagnostic count')
            if pointer_warnings:
                owner, line = pointer_warnings[0]
                expected_owner = baseline / 'lib/elf_loader/src/esp_elf.c'
                if Path(owner).resolve() != expected_owner or (
                        expected_owner.read_text().splitlines()[int(line) - 1].strip() !=
                        'elf->sec[ELF_SEC_TEXT].addr = (Elf32_Addr)elf->ptext;'):
                    raise RuntimeError('pointer warning does not match exact baseline expression')
            warnings = re.findall(r'warning: (.+?) \[(-W[^\]]+)\]', diagnostics)
            if any(flag != '-Wsign-compare' and not (
                    expected_pointer and flag == '-Wpointer-to-int-cast') for _, flag in warnings):
                raise RuntimeError('unexpected compiler warning: ' + label)
            for expression in re.findall(r'^\s*\d+\s*\|\s*(.+)$', diagnostics, re.M):
                if 'for (' in expression and expression.strip() not in (
                        'for (int i = 0; i < nr_reloc; i++) {',
                        'for (j = 0; j < shdr[i].size / sizeof(elf32_sym_t); j++) {'):
                    raise RuntimeError('new signed-loop diagnostic: ' + expression)
        run(label + '-build', [*argv, *sources, '-o', binary])
        return binary
    def case(label, argv, expected):
        before = digest(argv[0])
        if run(label, argv) != expected or digest(argv[0]) != before:
            raise RuntimeError('case output/binary mismatch: ' + label)
        report['cases'].append(label); save()
    try:
        if run('baseline-head', ['git', '-C', baseline, 'rev-parse', 'HEAD']).strip() != BASELINE:
            raise ValueError('wrong ordinary baseline revision')
        if run('baseline-status', ['git', '-C', baseline, 'status', '--porcelain', '--untracked-files=all']):
            raise ValueError('baseline must be clean')
        run('compiler-version', [cc, '--version'])
        for tree in (root / 'lib/elf_loader', baseline / 'lib/elf_loader', test):
            for path in tree.rglob('*'):
                if path.is_file():
                    remember(path)
        fixture = json.loads((test / 'LEGACY_FIXTURE.json').read_text())
        raw = base64.b64decode(fixture['elf_base64'], validate=True)
        if len(raw) != 16756 or hashlib.sha256(raw).hexdigest() != LEGACY_SHA256:
            raise ValueError('wrong preserved real ELF fixture')
        legacy = out / 'legacy-driver.elf'; legacy.write_bytes(raw)
        modes = [('normal', [])]
        if not args.normal_only:
            modes.append(('ubsan', ['-fsanitize=undefined', '-fno-sanitize-recover=all']))
        for mode, flags in modes:
            lib = root / 'lib/elf_loader'
            binary = build('loader-' + mode,
                [test / 'loader_test.c', lib / 'src/arch/esp_elf_xtensa.c',
                 lib / 'src/esp_elf_validate.c', test / 'instrumentation_leaf.c'],
                [test / 'shims', lib / 'include', lib / 'src'], flags + ['-DRISC_ELF_INTERNAL_PLACEMENT_V1=1'])
            for name in CASES:
                case(mode + '-' + name, [binary, name, *([legacy] if name == 'legacy-real' else [])],
                     'PASS ' + name + '\n')
            for name, source in (('baseline', baseline), ('successor', root)):
                lib = source / 'lib/elf_loader'
                label = name + '-ordinary-' + mode
                binary = build(label,
                    [test / 'ordinary_test.c', lib / 'src/arch/esp_elf_xtensa.c',
                     lib / 'src/esp_elf_validate.c', test / 'instrumentation_leaf.c'],
                    [test / 'shims', lib / 'include', lib / 'src'], flags)
                case(label + '-run', [binary], 'PASS ordinary-loader-equivalent\n')
            for enabled in (False, True):
                label = 'adapter-' + mode + ('-flag' if enabled else '-default')
                binary = build(label,
                    [test / 'adapter_refusal_test.c', root / 'lib/elf_loader/src/esp_elf_adapter.c', test / 'adapter_leaf.c'],
                    [test / 'adapter-shims', root / 'lib/elf_loader/include'],
                    flags + (['-DRISC_ELF_INTERNAL_PLACEMENT_V1=1'] if enabled else []))
                case(label + '-run', [binary], 'PASS actual-adapter-refusal\n')
        configs = ['', '#define CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT 1\n',
            '#define CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR 1\n#define CONFIG_ELF_LOADER_CACHE_OFFSET 1\n#define CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT 1\n',
            '#define CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR 1\n#define CONFIG_ELF_LOADER_SET_MMU 1\n#define CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT 1\n']
        for index, config in enumerate(configs):
            host = out / ('prefix-' + str(index)); host.mkdir()
            (host / 'sdkconfig.h').write_text(config)
            binary = build('prefix-' + str(index) + '-test', [test / 'legacy_prefix_test.c'],
                           [host, root / 'lib/elf_loader/include'])
            case('prefix-' + str(index) + '-run', [binary], '')
        for selection in ('absent', 'zero'):
            label = 'default-off-' + selection
            lib = root / 'lib/elf_loader'
            binary = build(label, [test / 'default_off_test.c',
                lib / 'src/arch/esp_elf_xtensa.c', lib / 'src/esp_elf_validate.c',
                test / 'instrumentation_leaf.c'],
                [test / 'shims', lib / 'include', lib / 'src'],
                [] if selection == 'absent' else ['-DRISC_ELF_INTERNAL_PLACEMENT_V1=0'])
            # Object-level check, not just link success or dead-strip behavior.
            obj = out / (label + '.o')
            run(label + '-object', [cc, '-isysroot', sdk, '-std=c11', '-D_DARWIN_C_SOURCE',
                '-O0', '-I' + str(test / 'shims'), '-I' + str(lib / 'include'),
                '-I' + str(lib / 'src'),
                *([] if selection == 'absent' else ['-DRISC_ELF_INTERNAL_PLACEMENT_V1=0']),
                '-c', lib / 'src/esp_elf.c', '-o', obj])
            symbols = run(label + '-undefined', ['nm', '-u', obj])
            forbidden = ('esp_elf_internal_alloc_text_v1', 'esp_elf_internal_alloc_data_v1',
                         'esp_elf_internal_supported_v1', 'esp_elf_internal_publish_v1')
            if any(name in symbols for name in forbidden):
                raise RuntimeError('default loader references internal port')
            case(label + '-ordinary', [binary], 'PASS ordinary-loader-equivalent\n')
            case(label + '-internal', [binary, 'internal'], 'PASS default-off-internal-refused\n')
        if len(report['cases']) != len(modes) * 26 + 8:
            raise RuntimeError('missing case coverage')
        for path, value in report['source_inputs'].items():
            if digest(path) != value:
                raise RuntimeError('input drift: ' + path)
        if digest(cc) != report['compiler_sha256']:
            raise RuntimeError('compiler changed')
        report['status'] = 'passed-host-only'
    except Exception as error:
        report['status'] = 'failed'; report['error'] = repr(error)
    finally:
        save()
    print(json.dumps({'status': report['status'], 'cases': len(report['cases']),
                      'result': str(out / 'RESULT.json'), 'sha256': digest(out / 'RESULT.json')}))
    return 0 if report['status'] == 'passed-host-only' else 1


if __name__ == '__main__':
    raise SystemExit(main())
