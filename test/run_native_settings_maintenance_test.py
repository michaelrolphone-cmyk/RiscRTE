#!/usr/bin/env python3
"""Source-pinned concrete maintenance/lifecycle, actual Runtime/Port/X4/FatFs and checked native stores."""
import argparse, hashlib, importlib.util, json, os, pathlib, subprocess, time
ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
for key in ('x4', 'drivers', 'reference-reader', 'product-store', 'output'):
    p.add_argument('--' + key, type=lambda x: pathlib.Path(x).resolve(), required=True)
p.add_argument('--sanitize', choices=('none', 'undefined', 'address,undefined'), default='none')
p.add_argument('--assembly', action='store_true', help='Use the real permanent factory with injected checked backends and PSRAM shim')
p.add_argument('--case', action='append', dest='cases')
p.add_argument('--case-timeout', type=float, default=30, help='Bound each case process in seconds')
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
product_hashes={'boot.json':'407360631dc10b73168701013a95ce19f846ae76e7a89b84db267870d8290446','board.json':'d06f9ea76fae9005f4e4c9e7f0c144b20ddc94e203d6cc53efae0b0504844f6a','sd/manifest.json':'644f64933e5e003041884d4254186dcecb21baae151084af4bbfc0245608f854'}
for name,digest in product_hashes.items():
    if hashlib.sha256((a.product_store/name).read_bytes()).hexdigest()!=digest:
        raise SystemExit('Frozen .99 graph input changed: '+name)
product_board=json.loads((a.product_store/'board.json').read_text())
product_boot=json.loads((a.product_store/'boot.json').read_text())
assert product_boot['installation']['volume_instance']==9
sd_device=next(d for d in product_board['devices'] if d['instance_id']==9)
assert sd_device['bindings']['board.power.ready']==1 and sd_device['config']['pins']==[5,41,42,40]
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
    for path in sorted({* (ROOT / 'test').glob('*native_sd_port*'),* (ROOT / 'test').glob('*native_settings_maintenance*'), ROOT / 'test/native_replica_checked_fixture.h', ROOT / 'test/native_settings_assembly_fixture.h', * (ROOT / 'test/native_nvs_shim').glob('*.h'), * (ROOT / 'test/native_settings_payload_shim').rglob('*.h')}):
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
    subprocess.run(cmd, check=True, timeout=a.case_timeout if pathlib.Path(cmd[0]).name == "test" else None)
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
          'src/ports/esp32s3/CpuPort.cpp', 'src/runtime/storage/AppDataFiles.cpp', 'src/ports/esp32s3/NativeSettingsMaintenance.cpp', 'src/ports/esp32s3/NativeSettingsBootstrap.cpp', 'test/native_settings_maintenance_test.cpp']
exe_flags = ['-fno-pie', '-no-pie'] if 'address' in a.sanitize else []
selected=['-DBOARD_XTEINK_X4_PRO', '-DRISC_NATIVE_SETTINGS_LIFECYCLE=1', '-DRISC_NATIVE_SETTINGS_WORKER=1', '-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1']
run([cxx, '-std=c++17', *flags, *inc, *selected, '-DRISC_NATIVE_STORAGE_CUSTODY=1', '-DESP_PLATFORM', '-I'+str(ROOT/'test/native_nvs_shim'), '-I'+str(ROOT/'test/native_settings_worker_shim'), '-c', ROOT/'src/ports/esp32s3/NativeSettingsLifecycle.cpp', '-o', a.output/'lifecycle.o'])
assembly_objects=[]
if a.assembly:
    selected += ['-DRISC_NATIVE_SETTINGS_ASSEMBLY=1','-DRISC_NATIVE_SETTINGS_PSRAM_PAYLOAD=1','-DRISC_NATIVE_SETTINGS_ASSEMBLY_HOST_TEST=1']
    source += ['src/ports/esp32s3/NativeSettingsAssembly.cpp']
    inc += ['-I'+str(ROOT/'test/native_settings_payload_shim'), '-I'+str(ROOT/'test/native_settings_worker_shim')]
    assembly_objects=[a.output/'payload.o']
    run([cxx,'-std=c++17',*flags,*inc,*selected,'-DESP_PLATFORM','-c',ROOT/'src/ports/esp32s3/NativeSettingsPayloadAllocator.cpp','-o',assembly_objects[0]])
run([cxx, '-std=c++17', *flags, *exe_flags, *inc, *selected, '-I'+str(ROOT/'test/native_nvs_shim'), '-DRISC_NATIVE_STORAGE_CUSTODY=1', '-pthread', '-rdynamic',
     *[ROOT / f for f in source], a.output/'lifecycle.o', *assembly_objects, '-ldl', '-lcrypto', '-Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename', '-o', a.output / 'test'])
manifest = json.loads((a.x4 / 'minimal/drivers/x4pro_sd/manifest.json').read_text())
manifest['requires'].append(dict(capability='platform.storage-worker', api=1))
(a.output / 'sd.json').write_text(json.dumps(manifest))
(a.output / 'power.json').write_text(json.dumps(dict(type='driver', id='test-power', version='1.0.0',
    driver_abi=2, architecture='xtensa-esp32s3', file_name='power.elf',
    requires=[dict(capability='hardware.device', api=1)],
    hardware_compatibility=[dict(compatible='xteink,x4-pro-peripheral-enable', revisions=['unspecified'], config_type='gpio.bank', config_version=1)],
    provides=[dict(capability='board.power.ready', api=1)])))
(a.output / 'board.json').write_text(json.dumps(product_board))
(a.output / 'boot.json').write_text(json.dumps(dict(board='board.json', default_app='default.elf',
    provider_activation='demand', drivers=[dict(manifest='power.json', instance_id=1), dict(manifest='sd.json', instance_id=9)],
    installation=dict(volume_instance=9, grants=[]), app_capabilities=[dict(manifest='app.json', grants=[dict(capability='storage.key-value',api=1,instance_id=1),dict(capability='storage.key-value.atomic',api=1,instance_id=1),dict(capability='storage.key-value.atomic',api=1,instance_id=6)])])))
(a.output/'app.json').write_text(json.dumps(dict(type='application', id='maintenance-test', version='1.0.0',architecture='xtensa-esp32s3',file_name='default.elf',entry='app_main',requires=[dict(capability='storage.key-value',api=1),dict(capability='storage.key-value.atomic',api=1)])))
(a.output/'app.c').write_text('extern void test_maintenance_app(void); __attribute__((visibility("default"))) void app_main(void){test_maintenance_app();}\n')
run([cc, '-std=c11', *flags, '-shared', '-fPIC', a.output/'app.c', '-o', a.output/'default.elf'])
cases = a.cases or ['happy','idle','draft-fence','pre-capture','save-exit','implicit-exit','absent','persisted-incarnation','foreign','usb-before','no-incarnation','cancel','retained','restore','restore-concurrent','restore-resume','restore-single','restore-no-source','restore-conflict','restore-cancel','restore-retained','first','first-concurrent','first-conflict','first-cancel']
if not a.cases:
    cases += ['bootstrap-'+s for s in ('fresh','fresh-restore','fresh-concurrent','legacy','legacy-concurrent','fresh-absent','legacy-absent','fresh-busy','legacy-busy','fresh-retained','legacy-conflict','legacy-corrupt','native-error','bare-init','fresh-resume','fresh-resume-stage','fresh-resume-rename','legacy-resume','legacy-resume-stage','legacy-resume-rename')]
if a.assembly and not a.cases:
    cases += ['payload-failure','restore-no-checkpoint','corrupt-native','six-writers','littlefs-assembly-only']
    cases += ['assembly-'+s for s in ('no-owner','owner-refuses','no-device','no-writers','many-writers','bad-writer','duplicate-writer','wrong-writer-namespace','files-root','files-hooks','files-allocator','files-context','artifact-namespace','source-namespace','legacy-namespace','legacy-tail','missing-source','no-metadata','metadata-range','guard-range','no-guard','no-space','no-digest','digest-range','partial-first','first-range','zero-incarnation','external-control','external-tail','artifact-identity','artifact-missing','artifact-tail','littlefs-identity','littlefs-root','littlefs-type','littlefs-tail')]
case_metrics=[]
for case in cases:
    command=[a.output / 'test', a.output, case, a.output / ('native-'+case)]
    commands.append([str(x) for x in command])
    before=time.perf_counter_ns()
    result=subprocess.run(command, check=True, timeout=a.case_timeout, text=True, stdout=subprocess.PIPE)
    elapsed=time.perf_counter_ns()-before
    print(result.stdout,end='',flush=True)
    case_metrics.append(dict(case=case,host_process_wall_ns=elapsed,
        checkpoints=[json.loads(line[len('HISTORY_METRIC '):]) for line in result.stdout.splitlines() if line.startswith('HISTORY_METRIC ')]))
(a.output/'case-metrics.json').write_text(json.dumps(case_metrics,indent=2)+'\n')
if runtime_sources() != runtime:
    raise SystemExit('Runtime/test input changed during build or execution; rerun with fresh output')
(a.output / 'source-manifest.json').write_text(json.dumps(dict(external_sources=records, runtime_sources=runtime,
    commands=commands, cases=cases, assembly=a.assembly, case_timeout_seconds=a.case_timeout, sanitizers=a.sanitize, product_graph=dict(source_commit="199e231d962c401c5d20617e2f5e4da07bffd780",hashes=product_hashes, scope="Full frozen .99 board; selected SD9/power1 subgraph and installation9 from frozen boot. Additive c7404623 checked-warm SD replaces frozen SD0.2.21; power is an always-ready host precondition."),
    generated_inputs={name: hashlib.sha256((a.output / name).read_bytes()).hexdigest() for name in ('sd.json', 'power.json', 'board.json', 'boot.json', 'sdk/source-hashes.json')},
    binaries={name: hashlib.sha256((a.output / name).read_bytes()).hexdigest() for name in ('driver.elf', 'power.elf', 'default.elf', 'lifecycle.o', 'test', *(['payload.o'] if a.assembly else []))},
    sanitizer_environment={k: os.environ.get(k, '') for k in ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'LSAN_OPTIONS')},
    limits='Host sector Hardware model only. Actual source-pinned X4/FatFs, validated Runtime graph, real CpuPort Sync/worker/custody. Checked native AppData/NVS store and coordinator with simulated NVS calls. No private mount/startup/product policy or physical SD qualification.'), indent=2) + '\n')
print('Actual X4/FatFs + Runtime graph + CpuPort custody PASS:', ', '.join(cases))
