#!/usr/bin/env python3
"""Source-pinned X4/FatFs + validated Runtime graph + real CpuPort custody."""
import argparse, hashlib, importlib.util, json, os, pathlib, subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
for key in ('x4', 'drivers', 'reference-reader', 'output'):
    p.add_argument('--' + key, type=lambda x: pathlib.Path(x).resolve(), required=True)
p.add_argument('--sanitize', choices=('none', 'undefined', 'address,undefined'), default='none')
p.add_argument('--case', action='append', dest='cases')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
if (a.output / 'sdk').exists():
    raise SystemExit('Use a fresh output directory')
pins = ((a.x4, 'c7404623bb7b175ab3d2452d7803ed4613e0cb35',
         ('minimal/drivers/x4pro_sd', 'minimal/drivers/x4pro_board_power/PowerReadyV1.h',
          'minimal/test', 'minimal/scripts/prepare_sdk.py', 'minimal/scripts/prepare_sd_wire_fixture.py',
          'minimal/interfaces', 'Drivers/x4pro_board/x4pro_proto.h', 'Drivers/x4pro_board/x4pro_pins.h')),
        (a.drivers, '281609a0d2ae514f107d2ef67b6e8b4d25c72fb8', ('lib/StorageFatFs',)),
        (a.reference_reader, '45cf61ac013fb618e8d7fed63217a35350484d52',
         ('sdk/driver', 'Drivers/x4pro_board', 'test/storage_volume/fake')))
records = []
for root, commit, paths in pins:
    names = subprocess.check_output(['git', '-C', str(root), 'ls-tree', '-r', '--name-only', commit, '--', *paths], text=True).splitlines()
    if not names:
        raise SystemExit('Missing pinned inputs: ' + str(root))
    for name in names:
        data = (root / name).read_bytes()
        if data != subprocess.check_output(['git', '-C', str(root), 'show', commit + ':' + name]):
            raise SystemExit('Pinned input changed: ' + str(root / name))
        records.append(dict(root=str(root), commit=commit, path=name, sha256=hashlib.sha256(data).hexdigest()))
def module(name):
    spec = importlib.util.spec_from_file_location(name, a.x4 / 'minimal/scripts' / (name + '.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod
def runtime_sources():
    records = []
    for folder in ('src', 'sdk', 'experimental/native_replica', 'lib/hal', 'lib/ArduinoJson/src', 'test/drivers/stubs'):
        for path in sorted((ROOT / folder).rglob('*')):
            if path.is_file() and path.suffix in ('.h', '.hpp', '.cpp', '.c', '.inc'):
                records.append(dict(path=str(path.relative_to(ROOT)), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    for path in sorted((ROOT / 'test').glob('*native_sd_port*')):
        if path.is_file():
            records.append(dict(path=str(path.relative_to(ROOT)), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    return records
runtime = runtime_sources()
module('prepare_sdk').prepare(ROOT, a.reference_reader, a.output / 'sdk')
module('prepare_sd_wire_fixture').prepare(a.output / 'sdk')
# GCC's sanitizer/O1 combination diagnoses vendored ArduinoJson iterator internals.
# Normal coverage remains optimized; sanitizer coverage follows the repo's O0 host tests.
flags = ['-O0' if a.sanitize != 'none' else '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-overflow', '-Wno-missing-field-initializers', '-fno-omit-frame-pointer']
if a.sanitize != 'none':
    flags += ['-fsanitize=' + a.sanitize, '-fno-sanitize-recover=all']
inc = ['-I' + str(x) for x in (a.output / 'sdk', a.drivers / 'lib/StorageFatFs',
       a.reference_reader / 'Drivers/x4pro_board', a.reference_reader, ROOT, ROOT / 'src',
       ROOT / 'test/drivers/stubs')]
# Vendored third-party headers remain source-hashed; treat their diagnostics as system headers.
inc += ['-isystem', str(ROOT / 'lib/ArduinoJson/src')]
cc, cxx = os.environ.get('CC', 'cc'), os.environ.get('CXX', 'c++')
commands = []
def run(cmd):
    commands.append([str(x) for x in cmd])
    subprocess.run(cmd, check=True, timeout=30 if pathlib.Path(cmd[0]).name == "test" else None)
run([cc, '-std=c11', *flags, *inc, '-shared', '-fPIC', '-DX4PRO_SD_ALLOW_LEGACY_GPIO=1',
     '-DX4_SD_TEST_SOURCE="' + str(a.x4 / 'minimal/test/sd_test.c') + '"',
     ROOT / 'test/native_sd_port_fixture.c', a.drivers / 'lib/StorageFatFs/fatfs/ff.c',
     a.drivers / 'lib/StorageFatFs/fatfs/ffunicode.c', '-o', a.output / 'driver.elf'])
run([cc, '-std=c11', *flags, *inc, '-shared', '-fPIC',
     '-DX4_POWER_READY_HEADER="' + str(a.x4 / 'minimal/drivers/x4pro_board_power/PowerReadyV1.h') + '"',
     ROOT / 'test/native_sd_port_power_fixture.c', '-o', a.output / 'power.elf'])
source = ['src/bootstrap/Json.cpp', 'src/bootstrap/Board.cpp', 'src/bootstrap/Runtime.cpp',
          'src/runtime/streams/AppStreamSessions.cpp', 'src/runtime/streams/ProviderQueueHost.cpp',
          'src/runtime/drivers/ProviderGraphV2.cpp', 'src/runtime/drivers/ProviderModuleV2.cpp',
          'src/ports/esp32s3/CpuPort.cpp', 'test/native_sd_port_test.cpp']
exe_flags = ['-fno-pie', '-no-pie'] if 'address' in a.sanitize else []
run([cxx, '-std=c++17', *flags, *exe_flags, *inc, '-DRISC_NATIVE_STORAGE_CUSTODY=1', '-pthread', '-rdynamic',
     *[ROOT / f for f in source], '-ldl', '-o', a.output / 'test'])
manifest = json.loads((a.x4 / 'minimal/drivers/x4pro_sd/manifest.json').read_text())
manifest['requires'].append(dict(capability='platform.storage-worker', api=1))
(a.output / 'sd.json').write_text(json.dumps(manifest))
(a.output / 'power.json').write_text(json.dumps(dict(type='driver', id='test-power', version='1.0.0',
    driver_abi=2, architecture='xtensa-esp32s3', file_name='power.elf',
    requires=[dict(capability='hardware.device', api=1)],
    hardware_compatibility=[dict(compatible='test,power-ready', revisions=['unspecified'], config_type='gpio.bank', config_version=1)],
    provides=[dict(capability='board.power.ready', api=1)])))
(a.output / 'board.json').write_text(json.dumps(dict(schema='riscrte.board-hardware', schema_version=1,
    board_id='native-sd-port-test', revision='unspecified', buses=[], devices=[dict(instance_id=55,
    chip=dict(vendor='xteink', model='sd', revision='unspecified'), compatible='xteink,x4-pro-sd-native1',
    bindings={'board.power.ready':56}, config_type='gpio.bank', config_version=1, config=dict(pins=[5,41,42,40], active_high=True,
    pull_up=True, debounce_us=0, long_press_us=0, click_min_us=0)),
    dict(instance_id=56, chip=dict(vendor='test', model='ready', revision='unspecified'), compatible='test,power-ready', config_type='gpio.bank', config_version=1,
    config=dict(pins=[4], active_high=True, pull_up=False, debounce_us=0, long_press_us=0, click_min_us=0))])))
(a.output / 'boot.json').write_text(json.dumps(dict(board='board.json', default_app='default.elf',
    provider_activation='demand', drivers=[dict(manifest='power.json', instance_id=56), dict(manifest='sd.json', instance_id=55)],
    installation=dict(volume_instance=55, grants=[]))))
cases = a.cases or ['fresh-warm', 'busy-session', 'busy-handle', 'busy-worker', 'busy-export', 'busy-sleep', 'busy-sync', 'gates',
                    'cancel-running', 'read-retained', 'provider-retained', 'adapter-retained', 'cleanup-retained', 'wrapper-lifetime', 'contract-busy-token', 'contract-ok-zero']
for case in cases:
    run([a.output / 'test', a.output, case])
if runtime_sources() != runtime:
    raise SystemExit('Runtime/test input changed during build or execution; rerun with fresh output')
(a.output / 'source-manifest.json').write_text(json.dumps(dict(external_sources=records, runtime_sources=runtime,
    commands=commands, cases=cases, sanitizers=a.sanitize,
    generated_inputs={name: hashlib.sha256((a.output / name).read_bytes()).hexdigest() for name in ('sd.json', 'power.json', 'board.json', 'boot.json', 'sdk/source-hashes.json')},
    binaries={name: hashlib.sha256((a.output / name).read_bytes()).hexdigest() for name in ('driver.elf', 'power.elf', 'test')},
    sanitizer_environment={k: os.environ.get(k, '') for k in ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'LSAN_OPTIONS')},
    limits='Host sector Hardware model only. Actual source-pinned X4/FatFs, validated Runtime graph, real CpuPort Sync/worker/custody. No product policy or physical SD qualification.'), indent=2) + '\n')
print('Actual X4/FatFs + Runtime graph + CpuPort custody PASS:', ', '.join(cases))
