"""Verify frozen consumer bytes, append-only compatibility, and drift rejection."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LEGACY_SHA = '6fbfacaa46d532bfef083e8c7dd3bbe36613e557279fdda887d814d53915526b'


def frozen_header(header):
    header = Path(header).resolve()
    if hashlib.sha256(header.read_bytes()).hexdigest() != LEGACY_SHA:
        raise AssertionError('unreviewed historical SDK header')
    return header


def compile_probe(header, output, include, *, reject=False):
    result = subprocess.run([
        os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-DLEGACY_RUNTIME_HEADER="' + str(header) + '"', '-I' + str(include), '-c',
        str(ROOT / 'test/legacy_runtime_header_abi.c'), '-o', str(output),
    ], capture_output=reject, text=reject, check=not reject)
    if reject:
        if result.returncode == 0:
            raise AssertionError('ABI drift unexpectedly compiled: ' + output.stem)
        # A missing compiler/header or syntax error is not evidence that the ABI
        # assertions detect the deliberate drift below.
        if 'static assertion failed' not in result.stderr:
            raise AssertionError('ABI negative probe failed for an unrelated reason: '
                                 + output.stem + '\n' + result.stderr)


def replaced_once(text, old, new):
    if text.count(old) != 1:
        raise AssertionError('ABI negative-probe anchor changed: ' + old)
    return text.replace(old, new, 1)


def negative_probes(header, temporary):
    current = (ROOT / 'sdk/app/RiscRuntimeV1.h').read_text()
    launch = '  bool (*take_launch_failure)(risc_runtime_launch_failure_v1* out);'
    initial = '  bool (*initial_default)(void);'
    prefix = '  bool (*health)(risc_runtime_health_v1* out);'
    grant = '  uint32_t struct_size, slot, generation;'
    # Only temporary copies are changed; neither consumer SDK nor production
    # header is edited, even when this verifier is run using a target compiler.
    variants = {
        'prefix-shift': replaced_once(current, prefix, '  uint64_t unexpected_prefix;\n' + prefix),
        'grant-layout': replaced_once(current, grant, '  uint64_t struct_size; uint32_t slot, generation;'),
        'tail-order': replaced_once(replaced_once(current, launch, '  /* swapped suffix probe */'),
                                    initial, initial + '\n' + launch),
        'extra-tail': replaced_once(current, initial, initial + '\n  bool (*unexpected_suffix)(void);'),
    }
    for name, content in variants.items():
        include = temporary / name
        include.mkdir()
        (include / 'RiscRuntimeV1.h').write_text(content)
        compile_probe(header, include / (name + '.o'), include, reject=True)
    changed = temporary / 'changed-frozen.h'
    changed.write_bytes(header.read_bytes() + b'\n/* changed frozen header */\n')
    try:
        frozen_header(changed)
    except AssertionError as error:
        if str(error) != 'unreviewed historical SDK header':
            raise
    else:
        raise AssertionError('Changed frozen consumer header unexpectedly accepted')
    return len(variants) + 1


def verify(header):
    header = frozen_header(header)
    with tempfile.TemporaryDirectory(prefix='legacy-runtime-abi-') as temporary:
        temporary = Path(temporary)
        compile_probe(header, temporary / 'probe.o', ROOT / 'sdk/app')
        return negative_probes(header, temporary)


if __name__ == '__main__':
    import sys
    rejected = verify(sys.argv[1])
    print('Frozen legacy Runtime header, every prefix field/size, and twelve '
          'append-only callback suffixes verified; ' + str(rejected) + ' ABI drift probes rejected')
