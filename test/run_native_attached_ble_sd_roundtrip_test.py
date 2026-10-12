#!/usr/bin/env python3
"""Source-pinned concrete maintenance/lifecycle, actual Runtime/Port/X4/FatFs and checked native stores."""
import argparse, hashlib, importlib.util, json, os, pathlib, subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
for key in ('x4', 'drivers', 'reference-reader', 'product-store', 'utilities', 'system', 'output'):
    p.add_argument('--' + key, type=lambda x: pathlib.Path(x).resolve(), required=True)
p.add_argument('--sanitize', choices=('none', 'undefined', 'address,undefined'), default='none')
p.add_argument('--include-wifi',action='store_true')
a = p.parse_args()
SYSTEM_PIN='472036f0c52e8fc88f0ccefb262e25734e6eda39';UTILITY_PIN='fb9da4c531c954e41169509b04debdc30977f387'
assert subprocess.check_output(['git','-C',str(a.system),'rev-parse','HEAD^{tree}'],text=True).strip()=='8fcb0e9c0216d2c16db5115da975e4f5f4132b95'
assert not subprocess.check_output(['git','-C',str(a.system),'status','--porcelain','--untracked-files=no'],text=True).strip()
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
pins+=((a.system,SYSTEM_PIN,('scripts/scene_sdk.py',)),(a.utilities,UTILITY_PIN,('scripts/build_contexts_scene.py','experimental/x4-atomic-preferences.json')))
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
    for path in sorted({* (ROOT / 'test').glob('*native_sd_port*'),* (ROOT / 'test').glob('*native_settings_maintenance*'),* (ROOT/'test').glob('*native_attached_ble*'),ROOT/'test/runtime_native_extra_fixture.h',ROOT/'test/runtime_native_settings_preferences_fixture.h',ROOT/'test/native_settings_sd_roundtrip_driver.c',ROOT/'test/runtime_atomic_wifi_controller.c', ROOT / 'test/native_replica_checked_fixture.h', * (ROOT / 'test/native_nvs_shim').glob('*.h')}):
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
    subprocess.run(cmd, check=True, timeout=120 if pathlib.Path(cmd[0]).name == "test" else None)
run([cc, '-std=c11', *flags, *inc, '-shared', '-fPIC', '-DX4PRO_SD_ALLOW_LEGACY_GPIO=1',
     '-DX4_SD_TEST_SOURCE="' + str(a.x4 / 'minimal/test/sd_test.c') + '"',
     ROOT / 'test/native_settings_sd_roundtrip_driver.c', a.drivers / 'lib/StorageFatFs/fatfs/ff.c',
     a.drivers / 'lib/StorageFatFs/fatfs/ffunicode.c', '-o', a.output / 'driver.elf'])
run([cc, '-std=c11', *flags, *inc, '-shared', '-fPIC',
     '-DX4_POWER_READY_HEADER="' + str(a.x4 / 'minimal/drivers/x4pro_board_power/PowerReadyV1.h') + '"',
     ROOT / 'test/native_sd_port_power_fixture.c', '-o', a.output / 'power.elf'])
source = ['src/bootstrap/Json.cpp', 'src/bootstrap/Board.cpp', 'src/bootstrap/Runtime.cpp',
          'src/runtime/streams/AppStreamSessions.cpp', 'src/runtime/streams/ProviderQueueHost.cpp',
          'src/runtime/drivers/ProviderGraphV2.cpp', 'src/runtime/drivers/ProviderModuleV2.cpp',
          'src/ports/esp32s3/CpuPort.cpp', 'src/runtime/storage/AppDataFiles.cpp', 'src/ports/esp32s3/NativeSettingsMaintenance.cpp', 'src/ports/esp32s3/NativeSettingsBootstrap.cpp', 'test/native_attached_ble_sd_roundtrip_test.cpp']
# Build the exact actual BLE controller closure separately from host C++.
import shlex
spec=importlib.util.spec_from_file_location('utility_stage',a.utilities/'scripts/build_contexts_scene.py');stage=importlib.util.module_from_spec(spec);spec.loader.exec_module(stage)
staged=a.output/'controller-sdk';stage.stage(ROOT,a.system,staged)
origins=[ROOT/'sdk/app',ROOT/'sdk/driver',ROOT/'sdk/hardware',a.system/'lib/PortableApps/include',a.system/'lib/NativeApps/include',a.system/'sdk/app',a.system/'Services/scene_profile',a.system/'Services/text_input',a.utilities/'lib/Contexts/include',a.utilities/'lib/Alarm/include',a.utilities/'Apps'];mapped={}
for header in (staged/'include').glob('*.h'):
    actual=next((d/header.name for d in origins if (d/header.name).is_file() and (d/header.name).read_bytes()==header.read_bytes()),None)
    assert actual is not None,header.name;mapped[header.name]=actual
cinc=['-I'+str(staged/'include'),'-I'+str(a.system/'lib/NativeApps/include'),'-I'+str(a.system/'Apps'),'-I'+str(a.utilities/'Apps'),'-I'+str(a.utilities/'lib/Bluetooth/include')]
defines=['-D'+d for d in json.loads((a.utilities/'experimental/x4-atomic-preferences.json').read_text())['defines']]+['-DUTILITY_BLE_SOURCE="'+str(a.utilities/'Apps/ble_scanner.c')+'"']
run([cc,'-std=c11',*flags,'-O1','-ffunction-sections','-fdata-sections','-fvisibility=hidden','-Wno-unused-function',*cinc,*defines,'-MMD','-MF',a.output/'controller.d','-c',ROOT/'test/runtime_native_attached_ble_controller.c','-o',a.output/'controller.o'])
run(['objcopy','--localize-symbol=app_main',a.output/'controller.o'])
run([cc,'-std=c11',*flags,*cinc,'-shared','-fPIC','-MMD','-MF',a.output/'hci.d',ROOT/'test/native_attached_ble_hci_provider.c','-o',a.output/'hci.elf'])
wifi_objects=[]
if a.include_wifi:
    wifi_inc=[*cinc,'-I'+str(a.system/'lib/RemoteFiles/vendor/tinydtls_sha2')]
    for index,(wifi_source,defines) in enumerate([(ROOT/'test/runtime_atomic_wifi_controller.c',['-DTEST_IDLE_ELIGIBILITY','-DSYSTEM_WIFI_WORKFLOW_SOURCE=\"'+str(a.system/'test/native_apps/wifi_workflow_test.c')+'\"']),(a.system/'lib/RemoteFiles/vendor/tinydtls_sha2/sha2.c',[])]):
        obj=a.output/f'wifi-{index}.o';wifi_objects.append(obj)
        run([cc,'-std=c11',*flags,'-O1','-Wno-unused-function',*wifi_inc,*defines,'-MMD','-MF',a.output/f'wifi-{index}.d','-c',wifi_source,'-o',obj])
        if index==0:
            # Independent app-local presentation adapters share public C names.
            # Rename definitions and their own references in this fixture object;
            # keep actual controller source, broker calls and assertions unchanged.
            app_local=('paper_presentation_get','portable_app_sleep_retained',
                       'portable_text_adapter_attention','portable_text_adapter_resume',
                       'portable_text_adapter_retain','portable_text_adapter_retained',
                       'portable_text_adapter_suspend')
            run(['objcopy',*['--redefine-sym='+name+'=attached_wifi_'+name for name in app_local],obj])
closure=[]
for dep in sorted(a.output.glob('*.d')):
    for f in shlex.split(dep.read_text().replace(chr(92)+chr(10),' ').split(':',1)[1]):
        path=pathlib.Path(f).resolve();origin=mapped[path.name] if path.is_relative_to(staged/'include') else a.system/'lib/PortableApps'/path.relative_to(staged) if path.is_relative_to(staged) else path
        assert path.read_bytes()==origin.read_bytes()
        for repo,pin in [(a.system,SYSTEM_PIN),(a.utilities,UTILITY_PIN)]:
            if origin.is_relative_to(repo):
                rel=str(origin.relative_to(repo));data=origin.read_bytes();assert data==subprocess.check_output(['git','-C',str(repo),'show',pin+':'+rel]);entry=dict(root=str(repo),commit=pin,path=rel,sha256=hashlib.sha256(data).hexdigest())
                if entry not in closure:closure.append(entry)
exe_flags = ['-fno-pie', '-no-pie'] if 'address' in a.sanitize else []
selected=['-DATTACHED_WIFI='+str(int(a.include_wifi)),'-DBOARD_XTEINK_X4_PRO', '-DRISC_NATIVE_SETTINGS_LIFECYCLE=1', '-DRISC_NATIVE_SETTINGS_WORKER=1', '-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1']
run([cxx, '-std=c++17', *flags, *inc, *selected, '-DRISC_NATIVE_STORAGE_CUSTODY=1', '-DESP_PLATFORM', '-I'+str(ROOT/'test/native_nvs_shim'), '-I'+str(ROOT/'test/native_settings_worker_shim'), '-c', ROOT/'src/ports/esp32s3/NativeSettingsLifecycle.cpp', '-o', a.output/'lifecycle.o'])
run([cxx, '-std=c++17', *flags, *exe_flags,'-Wno-unused-function','-Wno-misleading-indentation', *inc, *selected, '-I'+str(ROOT/'test/native_nvs_shim'), '-DRISC_NATIVE_STORAGE_CUSTODY=1', '-pthread', '-rdynamic',
     *[ROOT / f for f in source], a.output/'lifecycle.o',a.output/'controller.o',*wifi_objects,'-Wl,--gc-sections', '-ldl', '-lcrypto', '-Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename', '-o', a.output / 'test'])
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
    provider_activation='demand', drivers=[dict(manifest='power.json', instance_id=1), dict(manifest='sd.json', instance_id=9),dict(manifest='hci.json',instance_id=16)],
    installation=dict(volume_instance=9, grants=[]), app_capabilities=[dict(manifest='app.json', grants=[dict(capability='storage.key-value',api=1,instance_id=1),dict(capability='storage.key-value',api=2,instance_id=1),dict(capability='bluetooth.hci',api=1,instance_id=16),dict(capability='storage.key-value.atomic',api=1,instance_id=1),dict(capability='storage.key-value.atomic',api=1,instance_id=6)])])))
(a.output/'app.json').write_text(json.dumps(dict(type='application', id='maintenance-test', version='1.0.0',architecture='xtensa-esp32s3',file_name='default.elf',entry='app_main',requires=[dict(capability='bluetooth.hci',api=1),dict(capability='storage.key-value',api=1),dict(capability='storage.key-value',api=2),dict(capability='storage.key-value.atomic',api=1)])))
(a.output/'hci.json').write_text(json.dumps(dict(type='driver',id='attached-test-hci',version='1.0.0',driver_abi=2,architecture='xtensa-esp32s3',file_name='hci.elf',requires=[dict(capability='hardware.device',api=1)],hardware_compatibility=[dict(compatible='espressif,esp32s3-ble',revisions=['unspecified'],config_type='radio.integrated',config_version=1)],provides=[dict(capability='bluetooth.hci',api=1)])))
(a.output/'app.c').write_text('extern void test_maintenance_app(void); __attribute__((visibility("default"))) void app_main(void){test_maintenance_app();}\n')
run([cc, '-std=c11', *flags, '-shared', '-fPIC', a.output/'app.c', '-o', a.output/'default.elf'])
import shutil
native=a.output/'native';native.mkdir();card=a.output/'card.img';expected=a.output/'expected'
run([a.output/'test',a.output,native,card,expected,'edit'])
prior={str(p.relative_to(native)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(native.rglob('*')) if p.is_file()}
assert prior and 'simulated-nvs-erasure-witness.txt' in prior
image=hashlib.sha256(card.read_bytes()).hexdigest();shutil.rmtree(native);native.mkdir()
run([a.output/'test',a.output,native,card,expected,'restore'])
assert hashlib.sha256(card.read_bytes()).hexdigest()==image,'Typed pre-app restoration changed SD image'
for entry in records+closure:
    assert hashlib.sha256((pathlib.Path(entry['root'])/entry['path']).read_bytes()).hexdigest()==entry['sha256']
if runtime_sources() != runtime:
    raise SystemExit('Runtime/test input changed during build or execution; rerun with fresh output')
(a.output / 'source-manifest.json').write_text(json.dumps(dict(external_sources=records, runtime_sources=runtime,
    commands=commands,controller_closure=closure,sanitizers=a.sanitize,attached_automatic_maintenance=True,actual_two_wifi_profiles=a.include_wifi,expected_map_sha256={str(ns):hashlib.sha256((a.output/('expected-'+str(ns)+'.bin')).read_bytes()).hexdigest() for ns in (1,6)},erased_native_files=prior,preserved_sd_sha256=image, product_graph=dict(source_commit="199e231d962c401c5d20617e2f5e4da07bffd780",hashes=product_hashes, scope="Full frozen .99 board; selected SD9/power1 subgraph and installation9 from frozen boot. Additive c7404623 checked-warm SD replaces frozen SD0.2.21; power is an always-ready host precondition."),
    generated_inputs={name: hashlib.sha256((a.output / name).read_bytes()).hexdigest() for name in ('sd.json', 'power.json', 'board.json', 'boot.json', 'sdk/source-hashes.json')},
    binaries={name: hashlib.sha256((a.output / name).read_bytes()).hexdigest() for name in ('driver.elf', 'power.elf', 'default.elf', 'lifecycle.o','controller.o','hci.elf', 'test',*([p.name for p in wifi_objects]))},
    sanitizer_environment={k: os.environ.get(k, '') for k in ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'LSAN_OPTIONS')},
    limits='Actual BLE controller; HCI effects/text input, worker scheduling, NVS SDK and hardware sector transport are synthetic. Real automatic lifecycle maintenance is attached. Complete SD image and both complete native maps compared after native erasure. No physical SD, actual Files application, firmware delivery or product activation. Actual source-pinned X4/FatFs, validated Runtime graph, real CpuPort Sync/worker/custody. Checked native AppData/NVS store and coordinator with simulated NVS calls. No private backend mount or product policy selection, and no physical SD qualification.'), indent=2) + '\n')
print(('ATTACHED ACTUAL BLE + TWO WIFI PROFILES ->' if a.include_wifi else 'ATTACHED ACTUAL BLE ->')+' automatic Runtime maintenance -> full native erase -> typed bootstrap restore PASS; complete SD SHA256',image)
