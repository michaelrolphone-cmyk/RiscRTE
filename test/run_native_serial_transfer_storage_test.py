#!/usr/bin/env python3
"""Dedicated transfer qualification with independent actual LittleFS and FatFs.

Uses exact current product Git objects, actual Runtime graph/native admission,
RAM media and pre-run HELLO. Boot eligibility alone is an explicit fixture input.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import shlex
import time

ROOT = Path(__file__).resolve().parents[1]
PIN = 'f42a6e07133c599a22acfaa8b8a0672ff996dc1b'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--waveshare', type=Path, default=os.environ.get('WAVESHARE_SOURCE'))
p.add_argument('--output', type=Path)
p.add_argument('--case', action='append', help='Run only these named cases')
p.add_argument('--sanitize', choices=('none', 'undefined', 'address,undefined'),
               default='address,undefined' if os.environ.get('SANITIZE') == '1' else 'none')
a = p.parse_args()
if not a.waveshare:
    p.error('Pass --waveshare or WAVESHARE_SOURCE for the local pinned checkout')
a.waveshare = a.waveshare.resolve()
if subprocess.check_output(['git', '-C', str(a.waveshare), 'rev-parse', 'HEAD'], text=True).strip() != PIN:
    raise SystemExit('Waveshare HEAD must be exactly '+PIN)
build = (a.output or Path(tempfile.mkdtemp(prefix='native-serial-transfer-storage-'))).resolve()
build.mkdir(parents=True, exist_ok=True)
if any(build.iterdir()):
    raise SystemExit('Use an empty output directory')

paths = ('drivers/storage_sdmmc', 'lib/storage_fatfs', 'include',
         'tests/test_storage_async.c', 'hardware/board.json')
names = subprocess.check_output(['git', '-C', str(a.waveshare), 'ls-tree', '-r', '--name-only', PIN, '--', *paths], text=True).splitlines()
external = []
for name in names:
    raw = (a.waveshare/name).read_bytes()
    original = subprocess.check_output(['git', '-C', str(a.waveshare), 'show', PIN+':'+name])
    if raw != original:
        raise SystemExit('Pinned input changed: '+str(a.waveshare/name))
    external.append(dict(path=name, sha256=hashlib.sha256(raw).hexdigest()))
assert any(x['path'] == 'drivers/storage_sdmmc/driver.c' for x in external)
assert (ROOT/'sdk/app/RiscStorageVolumeV1.h').read_bytes() == (a.waveshare/'include/RiscStorageVolumeV1.h').read_bytes()

# One physical header per name prevents duplicate pragma-once ABI declarations.
# Runtime supplies the storage ABI; exact product headers supply driver helpers.
sdk = build/'sdk'
sdk.mkdir()
headers = {}
for base in (ROOT/'test/fixtures/watch/sdk/driver', a.waveshare/'include', ROOT/'sdk/driver', ROOT/'sdk/app', ROOT/'sdk/hardware'):
    for source in sorted(base.glob('*.h')):
        raw = source.read_bytes()
        (sdk/source.name).write_bytes(raw)
        headers[source.name] = dict(source=str(source), sha256=hashlib.sha256(raw).hexdigest())
manifest = json.loads((a.waveshare/'drivers/storage_sdmmc/manifest-async.json').read_text())
assert manifest['id'] == 'sdmmc-fatfs' and manifest['version'] == '0.2.2'
assert any(x['capability'] == 'platform.storage-worker' for x in manifest['requires'])
shutil.copyfile(a.waveshare/'drivers/storage_sdmmc/manifest-async.json', build/'sd.json')
board = json.loads((a.waveshare/'hardware/board.json').read_text())
board['buses'] = []
board['devices'] = [x for x in board['devices'] if x['instance_id'] == 12]
assert len(board['devices']) == 1 and board['devices'][0]['config']['pins'] == [2, 1, 3, 17]
(build/'board.json').write_text(json.dumps(board)+'\n')
(build/'app.json').write_text(json.dumps(dict(type='application', id='serial-transfer-storage-test', version='1.0.0',
    architecture='xtensa-esp32s3', file_name='default.elf', entry='app_main', requires=[]))+'\n')
(build/'boot.json').write_text(json.dumps(dict(board='board.json', default_app='default.elf', provider_activation='demand',
    drivers=[dict(manifest='sd.json', instance_id=12)], app_capabilities=[dict(manifest='app.json', grants=[])]))+'\n')

flags = ['-O0', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-overflow', '-Wno-missing-field-initializers',
         '-Wno-misleading-indentation', '-fno-omit-frame-pointer']
if a.sanitize != 'none':
    flags += ['-fsanitize='+a.sanitize, '-fno-sanitize-recover=all']
includes = ['-I'+str(x) for x in (sdk, ROOT, ROOT/'src', ROOT/'test', ROOT/'test/drivers/stubs',
    ROOT/'test/native_sleep_diagnostics_idf6_shim', ROOT/'test/native_platform_shim', ROOT/'lib/ArduinoJson/src')]
cc, cxx = os.environ.get('CC', 'cc'), os.environ.get('CXX', 'c++')
commands = []
results = []
def run(cmd):
    cmd = [str(x) for x in cmd]
    commands.append(cmd)
    label = ('case-'+cmd[-1]) if cmd[0] == str(build/'test') else 'build-'+str(len(commands))
    started = time.monotonic()
    result = subprocess.run(cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=180)
    (build/(label+'.log')).write_text(result.stdout)
    with (build/'runner.log').open('a') as log:
        log.write('$ '+shlex.join(cmd)+'\n'+result.stdout+'EXIT '+str(result.returncode)+'\n')
    print('$ '+shlex.join(cmd), flush=True)
    print(result.stdout, end='', flush=True)
    results.append(dict(label=label, returncode=result.returncode, elapsed_seconds=time.monotonic()-started))
    (build/'command-results.json').write_text(json.dumps(results, indent=2)+'\n')
    result.check_returncode()

local_names = [
    'src/bootstrap/Json.cpp', 'src/bootstrap/Board.cpp', 'src/bootstrap/Runtime.cpp',
    'src/runtime/streams/AppStreamSessions.cpp', 'src/runtime/streams/ProviderQueueHost.cpp',
    'src/runtime/drivers/ProviderGraphV2.cpp', 'src/runtime/drivers/ProviderModuleV2.cpp',
    'src/runtime/serial/Files.cpp', 'src/ports/esp32s3/NativeSerialFiles.cpp',
    'test/native_serial_transfer_storage_test.cpp']
local_records = {str(x.relative_to(ROOT)): hashlib.sha256(x.read_bytes()).hexdigest()
                 for folder in ('src', 'sdk', 'lib/ArduinoJson/src', 'test/drivers/stubs',
                                'test/native_sleep_diagnostics_idf6_shim', 'test/native_platform_shim', 'test/fixtures/watch/sdk/driver',
                                'test/native_littlefs_profile_stubs', 'test/native_diagnostic_workspace_shim', 'experimental/native_replica/pinned_littlefs')
                 for x in (ROOT/folder).rglob('*') if x.is_file() and x.suffix in ('.h', '.hpp', '.cpp', '.c', '.inc')}
for name in ('AGENTS.md', 'README.md', 'docs/HARDWARE_CONTRACT.md', 'test/native_serial_waveshare_sd_fixture.c', 'test/native_serial_transfer_storage_test.cpp',
             'test/run_native_serial_transfer_storage_test.py', 'test/native_serial_post_return_client_test.py', 'scripts/serial_files.py', 'test/compile_native_serial_littlefs.py', 'test/native_serial_littlefs_composition.cpp', 'test/RuntimeFileFixture.h', 'test/fixtures/atomic_key_value_app.c', 'test/native_littlefs_profile_fixture.c', 'test/native_littlefs_profile_fixture.h', 'test/native_app_data_mount_posix.cpp', 'test/run_native_littlefs_profile_test.py', 'experimental/native_replica/pinned_littlefs/prepare_idf6.py'):
    local_records[name] = hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
# Preserve exact compiled inputs and their Git object proof for independent review.
for name, expected in local_records.items():
    target=build/'frozen-runtime'/name
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(ROOT/name,target)
    assert hashlib.sha256(target.read_bytes()).hexdigest()==expected
for item in external:
    target=build/'frozen-product'/item['path']
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(a.waveshare/item['path'],target)
    assert hashlib.sha256(target.read_bytes()).hexdigest()==item['sha256']
run([cc, '-std=c11', *flags, *includes, '-shared', '-fPIC', '-fvisibility=hidden',
     ROOT/'test/fixtures/atomic_key_value_app.c', '-o', build/'default.elf'])
run([cc, '-std=c11', *flags, *includes, '-shared', '-fPIC', '-fvisibility=hidden',
     '-DWAVE_SERIAL_TEST_SOURCE="'+str(a.waveshare/'tests/test_storage_async.c')+'"',
     ROOT/'test/native_serial_waveshare_sd_fixture.c', a.waveshare/'lib/storage_fatfs/fatfs/ff.c',
     a.waveshare/'lib/storage_fatfs/fatfs/ffunicode.c', '-o', build/'driver.elf'])
import compile_native_serial_littlefs
lfs_objects, lfs_selection, lfs_link = compile_native_serial_littlefs.build(ROOT, build/'littlefs-composition', run, a.sanitize)
run([cxx, '-std=c++17', *flags, *includes, '-DRISC_NATIVE_SERIAL_FILES=1', '-DRISC_NATIVE_SERIAL_SD=1', '-DRISC_NATIVE_SERIAL_TRANSFER_MODE=1',
     *lfs_selection, '-rdynamic', '-no-pie', '-pthread',
     *[ROOT/x for x in local_names], *lfs_objects, *lfs_link, '-ldl', '-o', build/'test'])
generated_sources={str(x.relative_to(build)):hashlib.sha256(x.read_bytes()).hexdigest() for x in (build/'littlefs-composition').rglob('*') if x.is_file() and x.suffix in ('.c','.cpp','.h','.json')}
cases = ('cold', 'absent-card', 'healthy', 'disconnect', 'failed-close', 'failed-write', 'failed-release', 'failed-prepare')
if a.case and set(a.case) - set(cases):
    p.error('Unknown case: '+repr(sorted(set(a.case) - set(cases))))
cases = tuple(a.case or cases)
(build/'baseline-inputs.json').write_text(json.dumps(dict(product_revision=PIN, external_sources=external, runtime_sources=local_records, commands=commands, sanitizers=a.sanitize),indent=2)+'\n')
for case in cases:
    run([build/'test', build, case])
for item in external:
    assert hashlib.sha256((a.waveshare/item['path']).read_bytes()).hexdigest() == item['sha256'], 'Product input changed during test'
for name, expected in local_records.items():
    if hashlib.sha256((ROOT/name).read_bytes()).hexdigest() != expected:
        raise SystemExit('Runtime input changed during build; rerun in a fresh directory: '+name)
for name, expected in generated_sources.items():
    assert hashlib.sha256((build/name).read_bytes()).hexdigest()==expected, 'Generated composition source changed: '+name
footprint=subprocess.check_output(['nm','-S','--size-sort',str(build/'test')],text=True)
footprint='\n'.join(line for line in footprint.splitlines() if any(name in line for name in ('workspaceStorage','sessionStorage','privateProviderStorage')))
(build/'bounded-workspaces.txt').write_text(footprint+'\n')
limits = ('Host source qualification only. Exact current Waveshare f42a6e07133c599a22acfaa8b8a0672ff996dc1b asynchronous sdmmc-fatfs 0.2.2, actual Runtime graph/native admission and dedicated serial protocol. '
          'RAM SD card plus independent actual pinned LittleFS namespace2, GPIO/sync/storage-worker mocks and host ELF loading. Early HELLO skips all app hooks; only actual parked nativeFilesCheckpoint acknowledges readiness. '
          'Boot eligibility is an explicit always-eligible fixture; actual bank confirmation gate is qualified separately. Cold activation stays UNAVAILABLE without explicit preparation. '
          'No real CpuPort, native worker task, physical SD, USB serial, device, reset execution or timing qualification. LeakSanitizer disabled for ptrace environment; ASan/UBSan still enabled in sanitized runs.')
(build/'source-manifest.json').write_text(json.dumps(dict(product_root=str(a.waveshare), product_revision=PIN,
    external_sources=external, runtime_sources=local_records, staged_headers=headers, cases=cases,
    commands=commands, command_results=results, sanitizers=a.sanitize, sanitizer_environment={key:os.environ.get(key) for key in ('ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS')}, generated_sources=generated_sources, bounded_workspace_symbols=footprint, immutable_inputs_verified=True, limits=limits), indent=2)+'\n')
print('Dedicated transfer independent storage host qualification PASS:', build)
print(limits)
