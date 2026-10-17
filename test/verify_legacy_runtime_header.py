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


def frozen_header(header):
    header = Path(header).resolve()
    if hashlib.sha256(header.read_bytes()).hexdigest() != LEGACY_SHA:
        raise AssertionError('unreviewed historical SDK header')
    return header


def verify(header):
    header = frozen_header(header)
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
        checkpoint_signature = replace_once(source, 'int32_t (*service_checkpoint)(void);',
                                            'bool (*service_checkpoint)(void);')
        checkpoint_order = replace_once(source, '(*initial_default)', '(*swapped_default)')
        checkpoint_order = replace_once(checkpoint_order, '(*service_checkpoint)', '(*initial_default)')
        checkpoint_order = replace_once(checkpoint_order, '(*swapped_default)', '(*service_checkpoint)')
        sleep_prepare_signature = replace_once(source, 'int32_t (*sleep_prepare)(uint64_t *token);',
                                               'bool (*sleep_prepare)(uint64_t *token);')
        sleep_resume_signature = replace_once(source, 'int32_t (*sleep_resume)(uint64_t token);',
                                              'int32_t (*sleep_resume)(uint32_t token);')
        sleep_order = replace_once(source, '(*sleep_prepare)', '(*swapped_sleep)')
        sleep_order = replace_once(sleep_order, '(*sleep_resume)', '(*sleep_prepare)')
        sleep_order = replace_once(sleep_order, '(*swapped_sleep)', '(*sleep_resume)')
        shifted = replace_once(source, '  bool (*health)(risc_runtime_health_v1* out);',
                               '  uint64_t unexpected_prefix;\n  bool (*health)(risc_runtime_health_v1* out);')
        grant = replace_once(source, '  uint32_t struct_size, slot, generation;',
                             '  uint64_t struct_size; uint32_t slot, generation;')
        for name, mutated, diagnostic in (
            ('prefix-shift', shifted, 'risc_runtime_api_v1.health offset'),
            ('grant-layout', grant, 'risc_runtime_capability_v1.struct_size size'),
            ('prefix-order', prefix, 'risc_runtime_api_v1.health offset'),
            ('suffix-order', suffix, 'launch failure follows host control'),
            ('unknown-suffix', extra, 'fifteen pointer suffixes only'),
            ('suffix-signature', signature, 'initial default signature'),
            ('checkpoint-signature', checkpoint_signature, 'service checkpoint signature'),
            ('checkpoint-order', checkpoint_order, 'initial default follows launch failure'),
            ('sleep-prepare-signature', sleep_prepare_signature, 'sleep prepare signature'),
            ('sleep-resume-signature', sleep_resume_signature, 'sleep resume signature'),
            ('sleep-order', sleep_order, 'sleep prepare follows complete service checkpoint prefix'),
        ):
            candidate = temporary / (name + '.h')
            candidate.write_text(mutated)
            result = compile_probe(header, candidate, temporary / (name + '.o'))
            if (result.returncode == 0 or diagnostic not in result.stderr
                    or 'static assertion failed' not in result.stderr):
                raise AssertionError(name + ' negative control failed:\n' + result.stderr)

        changed = temporary / 'changed-frozen.h'
        changed.write_bytes(header.read_bytes() + b'\n/* changed frozen header */\n')
        try:
            frozen_header(changed)
        except AssertionError as error:
            if str(error) != 'unreviewed historical SDK header':
                raise
        else:
            raise AssertionError('Changed frozen consumer header unexpectedly accepted')
        return 12


if __name__ == '__main__':
    import sys
    rejected = verify(sys.argv[1])
    print('Frozen legacy Runtime header and every field/size plus fifteen '
          'append-only callback suffixes verified; ' + str(rejected) + ' ABI drift controls rejected')
