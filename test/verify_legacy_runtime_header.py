"""Verify frozen consumer bytes, append-only Runtime ABI and rejection controls."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
# RiscRTE-T-Watch-S3 aa7b03c15a59aa99b2d60ae20905edbdf62e35e6.
LEGACY_SHA = '6fbfacaa46d532bfef083e8c7dd3bbe36613e557279fdda887d814d53915526b'


def compile_probe(legacy, current, output):
    return subprocess.run([
        os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-DLEGACY_RUNTIME_HEADER="' + str(legacy) + '"',
        '-DCURRENT_RUNTIME_HEADER="' + str(current) + '"',
        '-c', str(ROOT / 'test/legacy_runtime_header_abi.c'), '-o', str(output),
    ], capture_output=True, text=True)


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise AssertionError('negative-control fixture drift: ' + before)
    return source.replace(before, after, 1)


def verify(header):
    header = Path(header).resolve()
    if hashlib.sha256(header.read_bytes()).hexdigest() != LEGACY_SHA:
        raise ValueError('unreviewed historical SDK header')
    current = ROOT / 'sdk/app/RiscRuntimeV1.h'
    source = current.read_text()
    with tempfile.TemporaryDirectory() as temporary:
        temporary = Path(temporary)
        result = compile_probe(header, current, temporary / 'probe.o')
        if result.returncode:
            raise AssertionError('Runtime ABI qualification failed:\n' + result.stderr)

        # Same-sized fields must not be reordered in either the frozen prefix or
        # its extensions. Also reject unreviewed growth and signature changes.
        prefix = replace_once(source, '(*health)', '(*swapped_health)')
        prefix = replace_once(prefix, '(*yield_ms)', '(*health)')
        prefix = replace_once(prefix, '(*swapped_health)', '(*yield_ms)')
        suffix = replace_once(source, '(*take_launch_failure)', '(*swapped_failure)')
        suffix = replace_once(suffix, '(*initial_default)', '(*take_launch_failure)')
        suffix = replace_once(suffix, '(*swapped_failure)', '(*initial_default)')
        extra = replace_once(source, '} risc_runtime_api_v1;',
                             '  void (*unexpected_suffix)(void);\n} risc_runtime_api_v1;')
        signature = replace_once(source, 'bool (*initial_default)(void);',
                                 'uint32_t (*initial_default)(void);')
        for name, mutated, diagnostic in (
            ('prefix-order', prefix, 'risc_runtime_api_v1.health offset'),
            ('suffix-order', suffix, 'launch failure follows host control'),
            ('unknown-suffix', extra, 'twelve pointer suffixes only'),
            ('suffix-signature', signature, 'initial default signature'),
        ):
            candidate = temporary / (name + '.h')
            candidate.write_text(mutated)
            result = compile_probe(header, candidate, temporary / (name + '.o'))
            if result.returncode == 0 or diagnostic not in result.stderr:
                raise AssertionError(name + ' negative control failed:\n' + result.stderr)


if __name__ == '__main__':
    import sys
    verify(sys.argv[1])
    print('Frozen legacy Runtime header and every field/size plus twelve '
          'append-only callback suffixes verified; four ABI breakage controls rejected')
