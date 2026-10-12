#!/usr/bin/env python3
"""Source-pinned additional Home controller and Wi-Fi full SD roundtrip."""
import argparse, hashlib, importlib.util, json, os, pathlib, subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
for key in ('x4', 'drivers', 'reference-reader', 'settings-system', 'wifi-system', 'output'):
    p.add_argument('--' + key, type=lambda x: pathlib.Path(x).resolve(), required=True)
p.add_argument('--controller',choices=('quick','home-low-battery','home-rules','home-preset','utility-battery','utility-ble','utility-contexts','utility-contexts-automatic'),default='quick')
p.add_argument('--utilities',type=lambda x:pathlib.Path(x).resolve())
p.add_argument('--system-cohort', choices=('historical', 'final-union'), default='historical',
               help='Final union requires the exact clean published System tree for both namespaces')
p.add_argument('--sanitize', choices=('none', 'undefined', 'address,undefined'), default='none')
a = p.parse_args()
FINAL_SYSTEM_COMMIT = '472036f0c52e8fc88f0ccefb262e25734e6eda39'
FINAL_SYSTEM_TREE = '8fcb0e9c0216d2c16db5115da975e4f5f4132b95'
if a.system_cohort == 'final-union':
    if a.settings_system != a.wifi_system:
        raise SystemExit('Final union requires one System worktree for both namespaces')
    tree = subprocess.check_output(['git', '-C', str(a.settings_system), 'rev-parse', 'HEAD^{tree}'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(a.settings_system), 'status', '--porcelain', '--untracked-files=no'], text=True).strip()
    if tree != FINAL_SYSTEM_TREE or dirty:
        raise SystemExit('Final System union must match the exact clean published tree ' + FINAL_SYSTEM_TREE)
utility=a.controller.startswith('utility-')
if a.controller!='quick' and a.utilities is None:raise SystemExit('--utilities required for actual Home rules headers')
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
if a.utilities:pins+=((a.utilities,'fb9da4c531c954e41169509b04debdc30977f387',('Apps/alarm_writer.h','lib/Alarm/include/AlarmRecords.h','scripts/build_contexts_scene.py','experimental/x4-atomic-preferences.json')), )
if utility:
    pins+=((a.settings_system,'472036f0c52e8fc88f0ccefb262e25734e6eda39',('scripts/scene_sdk.py',)),)
    assert subprocess.check_output(['git','-C',str(a.settings_system),'rev-parse','HEAD^{tree}'],text=True).strip()=='8fcb0e9c0216d2c16db5115da975e4f5f4132b95'
    assert not subprocess.check_output(['git','-C',str(a.settings_system),'status','--porcelain','--untracked-files=no'],text=True).strip()
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
    for path in sorted({* (ROOT / 'test').glob('*native_sd_port*'),* (ROOT / 'test').glob('*native_sd_synchronization*'),* (ROOT / 'test').glob('*native_settings_sd_roundtrip*'),* (ROOT / 'test').glob('runtime_native_settings*'),* (ROOT / 'test').glob('*native_extra*'),* (ROOT / 'test').glob('*native_quick*'),* (ROOT / 'test').glob('*native_home*'),* (ROOT / 'test').glob('*native_utility*'),ROOT / 'test/runtime_atomic_wifi_controller.c',ROOT / 'test/run_native_extra_settings_sd_roundtrip_test.py', ROOT / 'test/native_replica_checked_fixture.h', * (ROOT / 'test/native_nvs_shim').glob('*.h')}):
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
    subprocess.run(cmd, check=True, timeout=90 if pathlib.Path(cmd[0]).name == "test" else None)
run([cc, '-std=c11', *flags, *inc, '-shared', '-fPIC', '-DX4PRO_SD_ALLOW_LEGACY_GPIO=1',
     '-DX4_SD_TEST_SOURCE="' + str(a.x4 / 'minimal/test/sd_test.c') + '"',
     ROOT / 'test/native_settings_sd_roundtrip_driver.c', a.drivers / 'lib/StorageFatFs/fatfs/ff.c',
     a.drivers / 'lib/StorageFatFs/fatfs/ffunicode.c', '-o', a.output / 'driver.elf'])
run([cc, '-std=c11', *flags, *inc, '-shared', '-fPIC',
     '-DX4_POWER_READY_HEADER="' + str(a.x4 / 'minimal/drivers/x4pro_board_power/PowerReadyV1.h') + '"',
     ROOT / 'test/native_sd_port_power_fixture.c', '-o', a.output / 'power.elf'])
# Keep separate, exact System Settings and Wi-Fi dependency closures.
source = ['src/bootstrap/Json.cpp', 'src/bootstrap/Board.cpp', 'src/bootstrap/Runtime.cpp',
          'src/runtime/streams/AppStreamSessions.cpp', 'src/runtime/streams/ProviderQueueHost.cpp',
          'src/runtime/drivers/ProviderGraphV2.cpp', 'src/runtime/drivers/ProviderModuleV2.cpp',
          'src/ports/esp32s3/CpuPort.cpp', 'src/runtime/storage/AppDataFiles.cpp']
exe_flags = ['-fno-pie', '-no-pie'] if 'address' in a.sanitize else []
common_inc = inc + ['-I'+str(ROOT/'test/native_nvs_shim'), '-I'+str(ROOT/'sdk/app'), '-I'+str(ROOT/'sdk/driver'), '-I'+str(ROOT/'sdk/hardware')]
objects=[]
for index, name in enumerate(source):
    obj=a.output/f'common-{index}.o'
    run([cxx, '-std=c++17', *flags, *exe_flags, *common_inc, '-DRISC_NATIVE_STORAGE_CUSTODY=1', '-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1', '-pthread', '-MMD', '-MF', a.output/f'common-{index}.d', '-c', ROOT/name, '-o', obj])
    objects.append(obj)
import shutil, shlex
system_records=[];utility_records=[]
for ns, system, historical in [(1,a.settings_system,'472036f0c52e8fc88f0ccefb262e25734e6eda39' if utility else '1e721cc10327691ddf76907f48866a71a2eb02b6'), (6,a.wifi_system,'a5bd60fc5b7e7b62e3e08531860ce7e8ec855b89')]:
    commit = FINAL_SYSTEM_COMMIT if a.system_cohort == 'final-union' else historical
    folder=a.output/f'app-{ns}';folder.mkdir()
    if (ROOT/'sdk/app/RiscKeyValueAtomicV1.h').read_bytes() != (system/'lib/PortableApps/include/RiscKeyValueAtomicV1.h').read_bytes():
        raise SystemExit('Atomic SDK mismatch: '+str(system))
    staged=folder/'system-sdk';sdk_overlay={}
    overrides={'RiscRuntimeV1.h','RiscRealtimeV1.h','RiscResidentShellV1.h','RiscFailureEvidenceV1.h','RiscDiagnosticCheckpointV1.h'}
    if ns==1:
        if utility:
            spec=importlib.util.spec_from_file_location('utility_stage',a.utilities/'scripts/build_contexts_scene.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);m.stage(ROOT,system,staged)
            roots=[ROOT/'sdk/app',ROOT/'sdk/driver',ROOT/'sdk/hardware',system/'lib/PortableApps/include',system/'lib/NativeApps/include',system/'sdk/app',system/'Services/scene_profile',system/'Services/text_input',a.utilities/'lib/Contexts/include',a.utilities/'lib/Alarm/include',a.utilities/'Apps']
            for header in (staged/'include').glob('*.h'):
                origins=[r/header.name for r in roots];selected=next((x for x in origins if x.is_file() and x.read_bytes()==header.read_bytes()),None)
                if selected is None:raise SystemExit('Unmapped Utility SDK header: '+header.name)
                sdk_overlay[header.name]=selected
        else:
            shutil.copytree(system/'lib/PortableApps/include',staged/'include')
            shutil.copytree(system/'lib/PortableApps/time',staged/'time')
            for header in overrides:
                if (ROOT/'sdk/app'/header).exists():shutil.copy(ROOT/'sdk/app'/header,staged/'include'/header)
            # Native staging uses the actual pinned Runtime/Reader/X4 SDK closure.
            # Keep one physical header per API to preserve pragma-once/type identity.
            for header in ((a.output/'sdk').glob('*.h') if a.controller=='quick' else []):
                if not header.name.startswith(('Risc','Garden')):continue
                origins=[ROOT/'sdk/app'/header.name,ROOT/'sdk/driver'/header.name,ROOT/'sdk/hardware'/header.name,a.reference_reader/'sdk/driver'/header.name,a.x4/'minimal/interfaces'/header.name]
                selected=next((x for x in origins if x.is_file() and x.read_bytes()==header.read_bytes()),None)
                if selected is None:raise SystemExit('Unmapped native SDK header: '+header.name)
                shutil.copy(header,staged/'include'/header.name);sdk_overlay[header.name]=selected
        system_inc=['-I'+str(staged/'include'),'-I'+str(system/'lib/NativeApps/include')]
        if utility:
            system_inc+=['-I'+str(system/'Apps'),'-I'+str(a.utilities/'Apps'),'-I'+str(a.utilities/'lib/Bluetooth/include'),'-I'+str(system/'test/scene')]
            defines=['-D'+d for d in json.loads((a.utilities/'experimental/x4-atomic-preferences.json').read_text())['defines']]
            defines+=['-DUTILITY_CONTEXTS_AUTOMATIC='+('1' if a.controller=='utility-contexts-automatic' else '0')]
            source={'utility-battery':('BATTERY','battery_power.inc'),'utility-ble':('BLE','ble_scanner.c'),'utility-contexts':('CONTEXTS','contexts.c'),'utility-contexts-automatic':('CONTEXTS','contexts.c')}[a.controller]
            if a.controller.startswith('utility-contexts'):defines+=['-DCONTEXTS_SETTING_MASK=0x274u','-DCONTEXTS_ALARM_API=2']
            defines+=['-DUTILITY_'+source[0]+'_SOURCE=\"'+str(a.utilities/'Apps'/source[1])+'\"']
            controller=ROOT/('test/runtime_native_'+('utility_contexts' if a.controller.startswith('utility-contexts') else a.controller.replace('-','_'))+'_controller.c');extras=[]
        elif a.controller=='quick':
            defines=['-DPORTABLE_QUICK_ATOMIC_PREFERENCES','-DPORTABLE_FRONTLIGHT_TONE','-DPORTABLE_PAPER_TRANSITIONS','-DPORTABLE_RESIDENT_SHELL_HOST','-DSYSTEM_QUICK_SOURCE="'+str(system/'test/native_apps/quick_atomic_workflow_test.c')+'"']
            controller=ROOT/'test/runtime_native_quick_controller.c'
            extras=[(system/('lib/PortableApps/src/'+name+'.c'),defines[:4]) for name in ('quick_session','quick_radios','quick_actions')]
        else:
            system_inc+=['-I'+str(a.utilities/'Apps'),'-I'+str(a.utilities/'lib/Alarm/include')]
            defines=['-DHOME_CONTROLLER='+str({'home-low-battery':1,'home-rules':2,'home-preset':3}[a.controller]),'-DSYSTEM_HOME_SOURCE=\"'+str(system/'test/native_apps/home_automatic_rules_test.c')+'\"']
            controller=ROOT/'test/runtime_native_home_controller.c';extras=[]
    else:
        system_inc=['-I'+str(system/'lib/PortableApps/include'),'-I'+str(system/'lib/NativeApps/include'),'-I'+str(system/'lib/RemoteFiles/vendor/tinydtls_sha2')]
        defines=['-DTEST_IDLE_ELIGIBILITY','-DSYSTEM_WIFI_WORKFLOW_SOURCE="'+str(system/'test/native_apps/wifi_workflow_test.c')+'"']
        controller=ROOT/'test/runtime_native_settings_wifi_controller.c'
        extras=[(system/'lib/RemoteFiles/vendor/tinydtls_sha2/sha2.c',[])]
    app_objects=[]
    for index,(name,extra) in enumerate([(controller,defines),*extras]):
        obj=folder/f'controller-{index}.o'
        # Upstream Utility controller qualification uses O1 even with sanitizers.
        # Keep C++ Runtime/harness at O0 for vendored ArduinoJson diagnostics.
        run([cc,'-std=c11',*flags,*(['-O1','-ffunction-sections','-fdata-sections','-fvisibility=hidden'] if utility and ns==1 else []),'-Wno-unused-function','-Wno-misleading-indentation',*system_inc,*extra,'-MMD','-MF',folder/f'controller-{index}.d','-c',name,'-o',obj]);app_objects.append(obj)
        if utility and ns==1:
            localize=['--localize-symbol=app_main']
            if a.controller.startswith('utility-contexts'):localize+=['--localize-symbol=risc_resident_app_descriptor_v1']
            run(['objcopy',*localize,obj])
        if ns==1 and a.controller.startswith('home-'):run(['objcopy','--redefine-sym','main=unused_home_rules_fixture_main',obj])
    run([cc,'-std=c11',*flags,*common_inc,'-fPIC','-fvisibility=hidden','-shared',ROOT/'test/fixtures/atomic_key_value_app.c','-o',folder/'default.elf'])
    for version in (1,2):
        run([cc,'-std=c11',*flags,*common_inc,'-fPIC','-fvisibility=hidden','-shared','-DATOMIC_READER_VERSION='+str(version),ROOT/'test/fixtures/atomic_key_value_provider.c','-o',folder/f'reader{version}.elf'])
    obj=folder/'harness.o'
    run([cxx,'-std=c++17',*flags,*exe_flags,'-Wno-aggressive-loop-optimizations','-Wno-unused-function','-Wno-misleading-indentation',*common_inc,'-DRISC_NATIVE_STORAGE_CUSTODY=1','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1','-DSETTINGS_NAMESPACE='+str(ns),'-pthread','-MMD','-MF',folder/'harness.d','-c',ROOT/'test/native_extra_settings_sd_roundtrip_test.cpp','-o',obj])
    wraps='-Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename'
    run([cxx,*flags,*exe_flags,'-rdynamic','-no-pie',*(['-Wl,--gc-sections'] if utility and ns==1 else []),'-pthread',*objects,obj,*app_objects,'-ldl','-lcrypto',wraps,'-o',folder/'test'])
    selected=set()
    for dep in folder.glob('*.d'):
        selected.update(pathlib.Path(x).resolve() for x in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]))
    closure=[];utility_closure=[]
    for path in sorted(selected):
        actual=path.read_bytes();origin=path
        if path.is_relative_to(staged):
            rel=path.relative_to(staged)
            if rel.parts[0]=='include':
                name=str(rel.relative_to('include'));candidate=ROOT/'sdk/app'/name
                origin=sdk_overlay.get(name,candidate if name in overrides and candidate.exists() else system/'lib/PortableApps/include'/name)
            else:origin=system/'lib/PortableApps'/rel
        if actual!=origin.read_bytes():raise SystemExit('Staged dependency mismatch: '+str(path))
        if origin.is_relative_to(system):
            rel=str(origin.relative_to(system));expected=subprocess.check_output(['git','-C',str(system),'show',commit+':'+rel])
            if actual!=expected:raise SystemExit('Pinned System dependency mismatch: '+str(origin))
            closure.append(dict(path=rel,sha256=hashlib.sha256(actual).hexdigest()))
        elif a.utilities and origin.is_relative_to(a.utilities):
            rel=str(origin.relative_to(a.utilities));expected=subprocess.check_output(['git','-C',str(a.utilities),'show','fb9da4c531c954e41169509b04debdc30977f387:'+rel])
            if actual!=expected:raise SystemExit('Pinned Utilities dependency mismatch: '+str(origin))
            utility_closure.append(dict(path=rel,sha256=hashlib.sha256(actual).hexdigest()))
    if not utility or ns==6:assert any(x['path']==(('lib/PortableApps/src/quick_atomic.inc' if a.controller=='quick' else 'lib/PortableApps/src/home_automatic.inc') if ns==1 else 'Apps/wifi_atomic.inc') for x in closure)
    if utility_closure:utility_records.append(dict(namespace=ns,root=str(a.utilities),commit='fb9da4c531c954e41169509b04debdc30977f387',dependencies=utility_closure))
    system_records.append(dict(namespace=ns,root=str(system),commit=commit,dependencies=closure,defines=defines))
    print('Verified pinned System namespace',ns,'closure:',len(closure),'files at',commit)
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
# Both exact controller executables write the SAME synthetic sector image.
# Repeat with cancellation after SD publication, before native acknowledgement,
# followed by fresh native objects and exact durable-marker recovery.
campaigns=[]
for campaign,edit_mode in [('normal','edit'),('interrupted','edit-interrupted')]:
    case=a.output/campaign;case.mkdir();card=case/'card.img';native=case/'native';native.mkdir()
    for ns in (1,6):
        run([a.output/f'app-{ns}'/'test',a.output/f'app-{ns}',a.output,native/f'ns-{ns}',card,case/f'expected-{ns}.bin',edit_mode])
    # Simulate the firmware updater wiping every native AppData file and BOTH
    # simulated NVS persistence witnesses. Preserve only the external SD medium.
    prior={str(p.relative_to(native)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(native.rglob('*')) if p.is_file()}
    assert any(k.startswith('ns-1/') for k in prior) and any(k.startswith('ns-6/') for k in prior)
    assert sum(k.endswith('simulated-nvs-erasure-witness.txt') for k in prior)==2
    card_before=hashlib.sha256(card.read_bytes()).hexdigest()
    shutil.rmtree(native);native.mkdir();assert not list(native.rglob('*'))
    print(campaign,'SIMULATED FULL UPDATE ERASURE:',len(prior),'native files including both NVS witnesses deleted; SD SHA-256',card_before,flush=True)
    for ns in (1,6):
        run([a.output/f'app-{ns}'/'test',a.output/f'app-{ns}',a.output,native/f'ns-{ns}',card,case/f'expected-{ns}.bin','restore'])
        assert hashlib.sha256(card.read_bytes()).hexdigest()==card_before,'Read-only typed restore mutated SD media'
    campaigns.append(dict(name=campaign,erased_native_files=prior,preserved_sd_sha256=card_before))
for item in records:
    if hashlib.sha256((pathlib.Path(item['root'])/item['path']).read_bytes()).hexdigest()!=item['sha256']:raise SystemExit('External source changed during execution: '+item['path'])
for closure in system_records+utility_records:
    for item in closure['dependencies']:
        if hashlib.sha256((pathlib.Path(closure['root'])/item['path']).read_bytes()).hexdigest()!=item['sha256']:raise SystemExit('System source changed during execution: '+item['path'])
if runtime_sources() != runtime:
    raise SystemExit('Runtime/test input changed during build or execution; rerun with fresh output')
(a.output/'source-manifest.json').write_text(json.dumps(dict(external_sources=records,runtime_sources=runtime,system_closures=system_records,utilities_closures=utility_records,
    commands=commands,sanitizers=a.sanitize,campaigns=campaigns,system_cohort=a.system_cohort,controller=a.controller,
    generated_inputs={name:hashlib.sha256((a.output/name).read_bytes()).hexdigest() for name in ('sd.json','power.json','board.json','boot.json','sdk/source-hashes.json')},
    binaries={str(p.relative_to(a.output)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [a.output/'driver.elf',a.output/'power.elf',a.output/'app-1/test',a.output/'app-6/test']},
    sanitizer_environment={k:os.environ.get(k,'') for k in ('ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS')},
    limits='Source-bound host test only. Initial matching native/SD media (including the optional encoded preset seed) are explicitly synthetic. Actual separate pinned additional Home/Utilities and Wi-Fi controllers; actual Runtime atomic facades, shared native coordinators, checked AppData/NVS implementation, typed SD source admission/restore and real Runtime/CpuPort/X4/FatFs. UI/radio/RTC providers, NVS SDK calls, capacity accounting and sector transport are host seams. Files-style volume API verified; actual Files application, target firmware, physical SD, physical RTC, update delivery and product policy are not exercised.'),indent=2)+'\n')
print('FULL ADDITIONAL CONTROLLER/WIFI SD ROUNDTRIP PASS: namespaces 1 and 6, complete native erasure, fresh controllers, unchanged ordinary file and USB admission')
