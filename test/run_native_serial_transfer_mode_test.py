#!/usr/bin/env python3
"""Qualify dedicated serial transfer or reproduce its frozen pre-fix baseline; host only."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys

BASELINE_REVISION = '0aab2fd3b03b4190d638e23c79a465afeecf5dfb'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--baseline', action='store_true', help='Require the pinned unchanged production tree and both known false-ready failures.')
p.add_argument('--case', action='append', dest='selected_cases', help='Run only this named case; repeat to select more.')
p.add_argument('--source-root', type=Path, help='Compile an isolated copied source tree instead of the working checkout.')
p.add_argument('--expect-deadline-erasure', action='store_true', help='Negative control only: expect missing-Busy failures with pre-fix Files sources.')
a = p.parse_args()
repo = Path(__file__).resolve().parents[1]
root = a.source_root.resolve() if a.source_root else repo
if a.baseline and a.source_root:
    p.error('--baseline requires the actual pinned checkout')
if a.expect_deadline_erasure and (not a.source_root or a.baseline):
    p.error('Deadline negative control requires --source-root and cannot be a baseline')
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
if (out / 'receipt.json').exists():
    p.error('Refusing to overwrite an existing receipt; choose a new output directory.')
started = datetime.now(timezone.utc).isoformat()
revision = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
dirty = subprocess.check_output(['git', '-C', str(repo), 'diff', 'HEAD', '--name-only'], text=True).splitlines()
if a.baseline and (revision != BASELINE_REVISION or dirty):
    p.error('Baseline requires exact pinned HEAD and no tracked source edits: ' + repr((revision, dirty)))

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

folders = ('src', 'sdk', 'lib', 'test/drivers/stubs', 'test/native_sleep_diagnostics_idf6_shim', 'test/native_platform_shim')
inputs = {str(f.relative_to(root)) for folder in folders for f in (root / folder).rglob('*')
          if f.is_file() and f.suffix in ('.h', '.hpp', '.c', '.cpp', '.inc')}
inputs.update(('AGENTS.md', 'README.md', 'docs/HARDWARE_CONTRACT.md',
               'test/native_serial_transfer_mode_test.cpp', 'test/run_native_serial_transfer_mode_test.py',
               'test/native_serial_launch_test.cpp', 'test/run_native_serial_launch_test.py',
               'test/RuntimeFileFixture.h', 'test/storage_files_adapter_test.cpp',
               'test/fixtures/atomic_key_value_app.c'))
records = {name: sha(root / name) for name in sorted(inputs)}
for name, digest in records.items():
    copied = out / 'frozen-inputs' / name
    copied.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / name, copied)
    assert sha(copied) == digest
    copied.chmod(0o444)
(out / 'source-hashes.json').write_text(json.dumps(records, indent=2) + '\n')
flags = ['-g', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-fno-omit-frame-pointer']
sanitized = os.environ.get('SANITIZE') == '1'
if sanitized:
    flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
includes = ['-I' + str(root / x) for x in ('', 'src', 'sdk/app', 'sdk/driver', 'sdk/hardware', 'lib/ArduinoJson/src',
             'test/drivers/stubs', 'test/native_sleep_diagnostics_idf6_shim', 'test/native_platform_shim')]
sources = ('src/bootstrap/Json.cpp', 'src/bootstrap/Board.cpp', 'src/bootstrap/Runtime.cpp',
           'src/runtime/streams/AppStreamSessions.cpp', 'src/runtime/streams/ProviderQueueHost.cpp',
           'src/runtime/drivers/ProviderGraphV2.cpp', 'src/runtime/drivers/ProviderModuleV2.cpp',
           'src/runtime/serial/Files.cpp', 'src/ports/esp32s3/NativeSerialFiles.cpp',
           'test/native_serial_transfer_mode_test.cpp')
cc, cxx = os.environ.get('CC', 'cc'), os.environ.get('CXX', 'c++')
toolchains = {}
for compiler in (cc, cxx):
    executable = Path(shutil.which(compiler) or compiler).resolve()
    toolchains[compiler] = dict(path=str(executable), sha256=sha(executable),
        version=subprocess.check_output([compiler, '--version'], text=True),
        target=subprocess.check_output([compiler, '-dumpmachine'], text=True).strip())
commands, results = [], []
log = (out / 'runner.log').open('w')

def emit(text):
    print(text, end='', flush=True)
    log.write(text); log.flush()

def run(command, name, timeout=180):
    command = [str(x) for x in command]
    commands.append(command)
    emit('$ ' + shlex.join(command) + '\n')
    result = subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=timeout, env={**os.environ, 'LC_ALL': 'C'})
    (out / (name + '.log')).write_text(result.stdout)
    emit(result.stdout)
    emit('EXIT ' + str(result.returncode) + '\n')
    return result

cases = ('live-hello', 'live-control-hello', 'malformed-sid', 'short-sid', 'zero-sid', 'wrong-sequence',
         'extra-token', 'short-frame', 'partial-hello', 'unframed-hello', 'parked-hello')
if not a.baseline:
    cases += ('pending-pair', 'unconfirmed-boot', 'disconnect-before-park', 'disconnect-after-return',
              'late-postqueued', 'postqueued-direct', 'parked-storage-busy', 'idle-keepalive', 'idle-expiry',
              'partial-timeout', 'duplicate-hello', 'fresh-sid', 'failed-shutdown', 'failed-close',
              'legacy-live', 'disconnect-active', 'fini-admission-deadline', 'fini-deadline-disconnect', 'duplicate-after-ready-loss')
if a.expect_deadline_erasure:
    cases = ('fini-admission-deadline', 'fini-deadline-disconnect')
if a.selected_cases:
    unknown = set(a.selected_cases) - set(cases)
    if unknown:
        p.error('Unknown cases: ' + repr(sorted(unknown)))
    cases = tuple(case for case in cases if case in a.selected_cases)
error = None
try:
    for image, extra in (('default.elf', []), ('child.elf', ['-DATOMIC_APP_CHILD=1'])):
        build = run([cc, '-std=c11', *flags, *includes, *extra, '-shared', '-fPIC', '-fvisibility=hidden',
                     root / 'test/fixtures/atomic_key_value_app.c', '-o', out / image], 'build-' + image)
        if build.returncode:
            raise RuntimeError('Fixture ELF build failed: ' + image)
    build = run([cxx, '-std=c++17', *flags, *includes, '-DRISC_NATIVE_SERIAL_FILES=1', '-DRISC_NATIVE_ESP_IDF=1',
                 '-DRISC_TEST_TRANSFER_MODE=' + ('0' if a.baseline else '1'),
                 '-rdynamic', '-no-pie', '-pthread', *[root / s for s in sources], '-ldl', '-o', out / 'test'], 'build-host')
    if build.returncode:
        raise RuntimeError('Production-path host build failed')
    for case in cases:
        result = run([out / 'test', out, case], case, timeout=30)
        expected_failure = a.baseline and case in ('live-hello', 'live-control-hello')
        exact_failure = (result.returncode == 1 and 'REGRESSION FALSE_READY_HELLO:' in result.stdout and
                         'runtime_parked=0 hello_success=1' in result.stdout and
                         'app_stack_live=1 app_fini=0' in result.stdout and
                         'RESULT case=' + case + ' violations=1 FAIL' in result.stdout and
                         'PARKED_TRANSFER loaded_app_finalized=1 real_runtime_parked=1 generic_provider_read=1 checked_stop=1 PASS' in result.stdout)
        deadline_failure = (a.expect_deadline_erasure and result.returncode == -6 and
                            'FINI_DEADLINE observed_reply_bytes=0' in result.stdout and
                            'hello_success=0 mode_active=0' in result.stdout and
                            'reply == expectedBusy' in result.stdout and 'failed' in result.stdout)
        passed = (deadline_failure if a.expect_deadline_erasure else exact_failure if expected_failure else
                  (result.returncode == 0 and 'RESULT case=' + case + ' violations=0 PASS' in result.stdout))
        results.append(dict(case=case, returncode=result.returncode, expected_regression_failure=expected_failure,
                            false_ready_regression_reproduced=exact_failure,
                            deadline_erasure_regression_reproduced=deadline_failure, expectation_met=passed))
except Exception as exc:
    error = repr(exc)
    emit('RUNNER_ERROR ' + error + '\n')
finally:
    changed = [name for name, digest in records.items() if sha(root / name) != digest]
    frozen_changed = [name for name, digest in records.items() if sha(out / 'frozen-inputs' / name) != digest]
    receipt = dict(revision=revision, expected_baseline_revision=BASELINE_REVISION,
        baseline=a.baseline, selected_cases=cases, started_at=started, completed_at=datetime.now(timezone.utc).isoformat(),
        invocation=[sys.executable, *sys.argv], cwd=str(root), platform=platform.platform(),
        source_root=str(root), checkout_root=str(repo),
        source_variant=('pre-fix Files deadline-reply-erasure negative control; other inputs are frozen copies' if a.expect_deadline_erasure else
                        'isolated source copy' if a.source_root else 'working checkout'),
        revision_scope=('Checkout metadata only; variant input hashes are authoritative' if a.source_root else 'Working checkout HEAD plus recorded input hashes'),
        tracked_edits_at_start=dirty, toolchains=toolchains, sanitized=sanitized,
        sanitizer_environment={key: os.environ.get(key) for key in ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'LSAN_OPTIONS')},
        commands=commands, results=results, runtime_sources=records,
        immutable_inputs_verified=not changed and not frozen_changed,
        changed_inputs=changed, changed_frozen_inputs=frozen_changed,
        source_snapshot='frozen-inputs (read-only copies)', error=error,
        contract_suite_passed=bool(results) and all(r['returncode'] == 0 for r in results),
        expectations_met=not error and not changed and not frozen_changed and len(results) == len(cases) and all(r['expectation_met'] for r in results),
        interface_assumptions=[
            ('The original production dispatch path is compiled without mode selection.' if a.baseline else
             'NativeSerialFiles::enableTransferMode and Port serialTransferRequested/Enter/Poll hooks select production dedicated-mode behavior.'),
            'The loaded ELF explicitly confirms boot through Runtime API and counted Port.confirmBoot; eligible = confirmed && !pendingPair is only a synthetic admission input, not real bank qualification.',
            'Runtime::nativeFilesServiceSafe after the API call/Runtime::run is the parked oracle; temporary active()==false inside a native callback is insufficient.',
            'Fresh HELLO success is prohibited before parking; malformed/short input is never treated as successful HELLO.',
            'After cooperative app return, real Runtime cleanup/fini/unload/parking precedes deferred HELLO success and generic-provider GET/READ/CLOSE.',
            'Transfer intent is irreversible until final stop/restart; disconnect discards protocol state but never resumes apps. Reconnect requires a fresh HELLO.',
            'The existing 30-second SID and partial-transfer timeout remains enabled. NS keepalive preserves a session; idle expiry requires a fresh HELLO while dedicated mode remains latched.'],
        limits='Host production Runtime, NativeSerialFiles and Files with a loaded shared ELF, synthetic generic provider and explicit synthetic boot confirmation. No target Waveshare build/profile configuration, physical timing, actual USB/SD, provider-graph hardware cohort, reboot, NativeBankStore boot-readiness policy or device qualification. Failed native shutdown is a fixture-injected custody refusal; failed generic provider close uses real RuntimeFiles retention. No network, hardware, CI or publication.')
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    (out / 'COMMANDS.txt').write_text('\n'.join(shlex.join(command) for command in commands) + '\n')
    emit(('NEGATIVE_CONTROL ' if a.expect_deadline_erasure else 'BASELINE ' if a.baseline else 'QUALIFICATION ') +
         ('expected deadline-reply-erasure failures recorded' if receipt['expectations_met'] and a.expect_deadline_erasure else
          'expected false-ready failures and independent controls recorded' if receipt['expectations_met'] and a.baseline else
                      'contract PASS' if receipt['expectations_met'] else 'unexpected result; inspect receipt.json') + '\n')
    log.close()
    sums = []
    for artifact in sorted(out.rglob('*')):
        if artifact.is_file() and artifact.name != 'SHA256SUMS':
            sums.append(sha(artifact) + '  ' + str(artifact.relative_to(out)))
            artifact.chmod(0o555 if artifact.name in ('test', 'default.elf', 'child.elf') else 0o444)
    manifest = out / 'SHA256SUMS'
    manifest.write_text('\n'.join(sums) + '\n'); manifest.chmod(0o444)
if not receipt['expectations_met']:
    sys.exit(1)
