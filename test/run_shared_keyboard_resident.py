#!/usr/bin/env python3
"""Actual Home, Points/BLE controller/adapter, text and scene over production Runtime.
Host-only fixture. Generated state stays under --output; sources are never mutated.
Every external root/source has a CLI option and matching CAPACITY_* environment
variable. CLI wins over environment; existing workspace locations are defaults.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--sanitize', action='store_true')
p.add_argument('--input', choices=['touch', 'hardware'], default='touch')
p.add_argument('--activation', choices=['demand', 'demand-retained'], default='demand')
build_mode = p.add_mutually_exclusive_group()
build_mode.add_argument('--build-only', action='store_true')
build_mode.add_argument('--skip-build', action='store_true')
p.add_argument('--client', choices=['points', 'ble', 'all'], default='all')
p.add_argument('--modes', default='accepted,cancelled,back,pending-close,retained-close,handoff')


def root_option(option, env, default, help_text):
    p.add_argument(option, type=Path, default=Path(os.environ.get(env, default)),
                   help=f'{help_text} (environment: {env}; default: %(default)s)')


root_option('--system-root', 'CAPACITY_SYSTEM_ROOT',
            '/workspace/shared/system-shared-text-host-current-20261010', 'System source root')
root_option('--points-root', 'CAPACITY_POINTS_ROOT',
            '/workspace/shared/points-shared-text-input', 'Points source root')
root_option('--ble-root', 'CAPACITY_BLE_ROOT',
            '/workspace/shared/ble-shared-text-input', 'BLE source root')
root_option('--utilities-root', 'CAPACITY_UTILITIES_ROOT',
            '/workspace/shared/points-catalog-storage-custody', 'Utilities/Alarm source root')
root_option('--x4-sdk-root', 'CAPACITY_X4_SDK_ROOT',
            '/workspace/shared/x4-provider-sdk-recovered', 'X4 physical SDK headers root')
root_option('--home-build-root', 'CAPACITY_HOME_BUILD_ROOT',
            '/workspace/shared/x4-home-image-050/home-target', 'Home build-evidence root')
root_option('--sleep-source', 'CAPACITY_SLEEP_SOURCE',
            '/workspace/shared/x4-fast-native-source-047/minimal/apps/portable_sleep.c',
            'Native sleep C source')
root_option('--idle-source', 'CAPACITY_IDLE_SOURCE',
            '/workspace/shared/x4-host-sleep-overlay/minimal/apps/portable_idle_sleep.c',
            'Native idle-sleep C source')
root_option('--points-build-receipt', 'CAPACITY_POINTS_BUILD_RECEIPT',
            '/workspace/shared/x4-fast-productivity-build-047/cohort-receipt.json',
            'Points product build receipt supplying build_defines')
root_option('--ble-build-receipt', 'CAPACITY_BLE_BUILD_RECEIPT',
            '/workspace/shared/x4-fast-utilities-build-047/ble_scanner/x4-native-app.json',
            'BLE product build receipt supplying build_defines')
p.add_argument('--product-store-root', type=Path,
               default=os.environ.get('CAPACITY_PRODUCT_STORE_ROOT'),
               help='Assembled product metadata (environment: CAPACITY_PRODUCT_STORE_ROOT; '
                    'default: HOME_BUILD_ROOT/../assembled/store)')
a = p.parse_args()
SYSTEM, POINTS, BLE, UTIL, SDK, HOME = [getattr(a, n).expanduser().resolve() for n in
    ['system_root', 'points_root', 'ble_root', 'utilities_root', 'x4_sdk_root', 'home_build_root']]
SLEEP = a.sleep_source.expanduser().resolve()
IDLE = a.idle_source.expanduser().resolve()
POINTS_RECEIPT = a.points_build_receipt.expanduser().resolve()
BLE_RECEIPT = a.ble_build_receipt.expanduser().resolve()
store = (a.product_store_root or HOME.parent/'assembled/store').expanduser().resolve()
out = a.output.expanduser().resolve()
selected_roots = {'runtime': ROOT, 'system': SYSTEM, 'points': POINTS, 'ble': BLE,
                  'utilities': UTIL, 'x4_physical_sdk': SDK, 'home_build': HOME,
                  'product_store': store, 'sleep_source': SLEEP, 'idle_source': IDLE,
                  'points_build_receipt': POINTS_RECEIPT, 'ble_build_receipt': BLE_RECEIPT}
for name, path in selected_roots.items():
    if not path.exists():
        p.error(f'{name} does not exist: {path}')
modes = [mode.strip() for mode in a.modes.split(',') if mode.strip()]
if not modes or any('/' in mode or '\\' in mode or mode in {'.', '..'} for mode in modes):
    p.error('--modes must contain nonempty, simple mode names')
out.mkdir(parents=True, exist_ok=True)
inc = out/'include'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def json_digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def write(name, value):
    (out/name).write_text(json.dumps(value, indent=2, sort_keys=True)+'\n')


def tree_files(path):
    if not path.exists():
        raise SystemExit(f'Missing input tree: {path}')
    return sorted(f for f in path.rglob('*') if f.is_file() and '.git' not in f.relative_to(path).parts)


def inventory(base, files):
    hashes = {str(f.relative_to(base)): digest(f) for f in sorted(set(files))}
    return {'root': str(base), 'file_count': len(hashes), 'sha256': json_digest(hashes), 'files': hashes}


def git_state(path):
    directory = path if path.is_dir() else path.parent
    def git(*args):
        result = subprocess.run(['git', '-C', str(directory), *args], capture_output=True, text=True)
        return result.stdout.strip() if result.returncode == 0 else None
    top = git('rev-parse', '--show-toplevel')
    if top is None:
        return {'available': False, 'reason': 'Input is not in an accessible git checkout'}
    status = git('status', '--porcelain=v1', '--untracked-files=all')
    return {'available': True, 'root': top, 'revision': git('rev-parse', 'HEAD'),
            'branch': git('rev-parse', '--abbrev-ref', 'HEAD'),
            'dirty': None if status is None else bool(status), 'status_porcelain': status}


# A clean staging directory prevents stale headers surviving a changed root selection.
# Skip-build preserves and later verifies the exact staged inputs of the saved build.
if not a.skip_build:
    for staged in [inc, out/'time']:
        if staged.exists():
            shutil.rmtree(staged)
    inc.mkdir()
elif not inc.is_dir() or not (out/'time').is_dir():
    p.error('--skip-build requires a previous complete build in --output')
staged_header_sources = {}
header_roots = [HOME/'paper-sdk/include', SYSTEM/'lib/PortableApps/include',
                UTIL/'lib/Alarm/include', SDK, ROOT/'sdk/app', ROOT/'sdk/driver',
                ROOT/'sdk/hardware', SYSTEM/'sdk/app', SDK,
                SYSTEM/'Services/scene_profile', SYSTEM/'Services/text_input']


def stage_header(source):
    staged_header_sources[source.name] = str(source)
    if not a.skip_build:
        shutil.copyfile(source, inc/source.name)


for directory in header_roots:
    if not directory.is_dir():
        p.error(f'Missing header directory: {directory}')
    for header in sorted(directory.glob('*.h')):
        stage_header(header)
stage_header(SYSTEM/'lib/PortableApps/include/RiscBatteryGaugeV1.h')
# Runtime extensions come from selected Runtime; product display extensions stay exact.
for name in ['RiscRuntimeV1.h', 'RiscResidentShellV1.h', 'RiscRetainedWakeV1.h']:
    stage_header(ROOT/'sdk/app'/name)
if not a.skip_build:
    shutil.copytree(SYSTEM/'lib/PortableApps/time', out/'time')
include_roots = [inc, SYSTEM/'lib/NativeApps/include', SYSTEM/'Apps', POINTS/'Apps',
                 BLE/'Apps', BLE/'lib/Bluetooth/include',
                 SLEEP.parent.parent/'drivers/x4pro_power', IDLE.parent.parent/'drivers/x4pro_power']
incs = ['-I'+str(directory) for directory in include_roots]
source_macros = {'CAPACITY_HOME_SOURCE': SYSTEM/'Apps/paper_clock.c',
                 'CAPACITY_POINTS_SOURCE': POINTS/'Apps/points_catalog_app.c',
                 'CAPACITY_BLE_SOURCE': BLE/'Apps/ble_scanner.c',
                 'CAPACITY_SYSTEM_SOURCE': SYSTEM/'lib/PortableApps/src/adapter.c'}
ble_has_paper = (BLE/'Apps/ble_scanner_paper.inc').is_file()
if ble_has_paper and 'entrypoint' in modes:
    p.error('entrypoint is the legacy geometry-refusal probe; use the legacy BLE root for this mode')
source_flags = (['-DCAPACITY_BLE_PAPER'] if ble_has_paper else []) + ['-D'+name+'='+json.dumps(str(source)) for name, source in source_macros.items()]
san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer'] if a.sanitize else []
base = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', *san, *incs, *source_flags]
wrap = ['-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free']
cc = os.environ.get('CC', 'cc')
cxx = os.environ.get('CXX', 'c++')
commands = []
compiled_inputs = {}
dependency_scans = []


def run(command):
    command = list(map(str, command))
    commands.append(command)
    subprocess.run(command, check=True)


def dependencies(compiler, flags, sources, target):
    # -M includes transitive compiler/libc/C++ headers, unlike -MMD. Keep scans
    # separate from multi-source linking so no translation unit overwrites another.
    depdir = out/'dependencies'
    depdir.mkdir(exist_ok=True)
    for index, source in enumerate(sources):
        depfile = depdir/f'{target}-{index}.d'
        run([compiler, *flags, '-M', '-MT', 'capacity-input', '-MF', depfile, source])
        body = depfile.read_text().replace('\\\n', ' ').split(':', 1)[1]
        paths = {Path(value.replace('$$', '$')).resolve() for value in shlex.split(body)}
        paths.add(Path(source).resolve())
        for path in sorted(paths):
            value = digest(path)
            if str(path) in compiled_inputs and compiled_inputs[str(path)] != value:
                raise SystemExit(f'Compiled input changed during build: {path}')
            compiled_inputs[str(path)] = value
        dependency_scans.append({'target': target, 'source': str(source),
                                 'depfile': str(depfile.relative_to(out)),
                                 'inputs': sorted(map(str, paths))})


def dso(name, sources, flags=()):
    compile_flags = [*base, '-fPIC', '-fvisibility=hidden', *flags]
    dependencies(cc, compile_flags, sources, name)
    run([cc, *compile_flags, '-shared', '-Wl,-Bsymbolic', *wrap, *sources, '-o', out/name])


helper=[SYSTEM/'lib/PortableApps/src'/n for n in ['PortableRealtimeClient.c','PortableTimeZone.c','PortableTimeZoneCatalog.c','PortableTimeZonePreference.c']]
quick=[SYSTEM/'lib/PortableApps/src'/n for n in ['quick_actions.c','quick_render.c','quick_session.c','quick_radios.c']]
meta=json.loads((HOME/'build-evidence.json').read_text())
homeflags=[x for x in meta['build_defines'] if not x.startswith('-I')]+['-DPORTABLE_STAGE_LOGS']
points_receipt = json.loads(POINTS_RECEIPT.read_text())
ble_receipt = json.loads(BLE_RECEIPT.read_text())
pointsflags = list(dict.fromkeys([flag for flag in
    points_receipt['built']['points_in_time']['build_defines'] if not flag.startswith('-I')]
    + ['-DPORTABLE_TEXT_INPUT_CLIENT']))
bleflags = list(dict.fromkeys([flag for flag in ble_receipt['build_defines']
    if not flag.startswith('-I')]
    + ['-DPORTABLE_TEXT_INPUT_CLIENT', '-DPORTABLE_APP_LAUNCH_GUARD']))
clientflags = {'points': pointsflags, 'ble': bleflags}
providers=[('display','display.output',1),('touch','input.touch.raw',1),('navigation','input.navigation',1),('battery','board.battery',1),('rtc','rtc.clock',2),('alarm','alarm.service',2),('wifi','net.wifi',1),('bluetooth','bluetooth.hci',1),('contexts','contexts.service',1),('broadcast','telemetry.broadcast',1),('keyboard','usb.hid.keyboard',1)]
providers += [('power','x4.power',1),('storage','storage.volume',1),('sensors','bluetooth.sensors',1)]
providers += [(f'padding{i}',f'test.padding{i}',1) for i in range(9)]
def req(c,v=1):return {'capability':c,'api':v}
for name,cap,v in providers:
 write(name+'.json',{'type':'driver','id':name,'version':'1.0.0','driver_abi':2,'architecture':'xtensa-esp32s3','file_name':name+'.elf','requires':[],'provides':[req(cap,v)]})
product_boot=json.loads((store/'boot.json').read_text());product_board=json.loads((store/product_boot['board']).read_text())
removed_board_bindings = []
for device in product_board['devices']:
    bindings = device.pop('bindings', None)
    if bindings:
        removed_board_bindings.append({'instance_id': device['instance_id'], 'bindings': bindings})
physical={'display':3,'touch':4,'rtc':8,'battery':7,'navigation':6,'wifi':15,'bluetooth':16,'power':17,'storage':9}
for name,instance in physical.items():
 dev=next(d for d in product_board['devices'] if d['instance_id']==instance)
 m=json.loads((out/(name+'.json')).read_text());m['requires']=[req('hardware.device')];m['hardware_compatibility']=[{'compatible':dev['compatible'],'revisions':[dev['chip']['revision']],'config_type':dev['config_type'],'config_version':dev['config_version']}];write(name+'.json',m)
for name in ['scene','text']:
 m=json.loads((SYSTEM/f'Services/{"scene_host" if name=="scene" else "text_input"}/manifest.json').read_text());m['file_name']=name+'.elf'
 if name=='text' and a.input=='hardware':m['requires'].append(req('usb.hid.keyboard'))
 write(name+'.json',m)
write('profile.json',{'type':'driver','id':'profile','version':'1.0.0','driver_abi':2,'architecture':'xtensa-esp32s3','file_name':'profile.elf','requires':[],'provides':[req('ui.presentation-profile')]})
write('cohort.json',{'schema':'riscrte.cohort','schema_version':1,'product':'test','version':'1.0.0','runtime_version':'0.1.99','source_repo':'example/test','source_revision':'1'*40,'layout':'riscrte-paired-16m-v1','store_abi':1,'firmware_size':32,'firmware_sha256':'1'*64})
write('board.json',product_board)
policies=[]
for name,original in [('default','default'),('points','points_in_time'),('ble','ble_scanner'),('springboard',None)]:
 if original:
  manifest=json.loads((store/(original+'.json')).read_text());manifest['file_name']=name+'.elf'
  policy=next(p for p in product_boot['app_capabilities'] if p['manifest']==original+'.json');grants=policy['grants'].copy()
  if name!='default':
   manifest['requires']=[r for r in manifest['requires'] if r['capability']!='ui.text-input']+[req('ui.text-input')]
   grants=[g for g in grants if g['capability']!='ui.text-input']+[{**req('ui.text-input'),'instance_id':0}]
 else:manifest={'type':'application','id':name,'version':'1.0.0','architecture':'xtensa-esp32s3','file_name':name+'.elf','entry':'app_main','requires':[]};grants=[]
 write(name+'.json',manifest);policies.append({'manifest':name+'.json','grants':grants})
write('boot.json',{'board':'board.json','default_app':'default.elf','provider_activation':a.activation,'drivers':[{'manifest':n+'.json',**({'instance_id':physical[n]} if n in physical else {})} for n in [*[r[0] for r in providers],'scene','text','profile']],'app_capabilities':policies,'resident_shell':{'api':1,'host':'default.elf','foreground':['points.elf','ble.elf','springboard.elf']}})

def input_trees():
    trees = {}
    for prefix, root, relatives in [
        ('system', SYSTEM, ['Apps', 'lib/PortableApps', 'Services', 'sdk/app', 'lib/NativeApps/include']),
        ('points', POINTS, ['Apps', 'lib/PointsCatalog']),
        ('ble', BLE, ['Apps', 'lib/Bluetooth']),
        ('runtime', ROOT, ['src', 'sdk', 'lib/ArduinoJson', 'test/drivers/stubs']),
        ('utilities', UTIL, ['lib/Alarm/include']),
    ]:
        for relative in relatives:
            trees[prefix+'/'+relative] = inventory(root/relative, tree_files(root/relative))
    trees['points_build_receipt'] = inventory(POINTS_RECEIPT.parent, [POINTS_RECEIPT])
    trees['ble_build_receipt'] = inventory(BLE_RECEIPT.parent, [BLE_RECEIPT])
    trees['runtime/fixture'] = inventory(ROOT, [Path(__file__).resolve(), *ROOT.glob('test/shared_keyboard*')])
    trees['x4_physical_sdk'] = inventory(SDK, tree_files(SDK))
    home_inputs = [HOME/'build-evidence.json', *tree_files(HOME/'paper-sdk/include')]
    if (HOME/'idle-sdk/include').is_dir():
        home_inputs.extend(tree_files(HOME/'idle-sdk/include'))
    trees['home_build'] = inventory(HOME, home_inputs)
    custody = list(HOME.parent.glob('*custody*.json'))
    if (store.parent/'build-custody.json').is_file():
        custody.append(store.parent/'build-custody.json')
    if custody:
        trees['home_build_custody'] = inventory(HOME.parent, [f for f in custody if f.is_relative_to(HOME.parent)])
    trees['product_metadata'] = inventory(store, [store/'boot.json', store/product_boot['board'],
        *[store/(name+'.json') for name in ['default', 'points_in_time', 'ble_scanner']]])
    for label, source in [('native_sleep', SLEEP), ('native_idle', IDLE)]:
        native_root = source.parent.parent
        headers = [f for f in source.parent.rglob('*') if f.is_file() and
                   f.suffix.lower() in {'.h', '.hh', '.hpp', '.inc'}]
        power = native_root/'drivers/x4pro_power'
        if power.is_dir():
            headers.extend(f for f in tree_files(power) if f.suffix.lower() in {'.h', '.hh', '.hpp', '.inc'})
        trees[label] = inventory(native_root, [source, *headers])
    trees['staged/include'] = inventory(inc, tree_files(inc))
    trees['staged/time'] = inventory(out/'time', tree_files(out/'time'))
    return trees


def tree_digest(trees):
    return json_digest({name: entry['sha256'] for name, entry in trees.items()})


configuration = {'roots': {name: str(path) for name, path in selected_roots.items()},
    'sanitize': a.sanitize, 'cc': cc, 'cxx': cxx, 'home_build_defines': homeflags,
    'client_build_defines': clientflags, 'source_macros': {k: str(v) for k, v in source_macros.items()}}
initial_trees = input_trees()
initial_tree_digest = tree_digest(initial_trees)
input_git = {name: git_state(path) for name, path in selected_roots.items()}
if a.skip_build:
    manifest = out/'build-inputs.json'
    if not manifest.is_file():
        p.error('--skip-build requires build-inputs.json from a complete build; rebuild first')
    build_inputs = json.loads(manifest.read_text())
    if build_inputs['configuration'] != configuration or build_inputs['source_trees_sha256'] != initial_tree_digest:
        p.error('--skip-build inputs/configuration differ from the saved build; rebuild first')
    for filename, expected in build_inputs['compiled_inputs'].items():
        if not Path(filename).is_file() or digest(filename) != expected:
            p.error(f'--skip-build compiled input changed: {filename}; rebuild first')
    for filename, expected in build_inputs['artifacts'].items():
        if not (out/filename).is_file() or digest(out/filename) != expected:
            p.error(f'--skip-build artifact changed: {filename}; rebuild first')

if not a.skip_build:
 dso('default.elf',[ROOT/'test/shared_keyboard_home_client.c',ROOT/'test/shared_keyboard_home_adapter.c',SYSTEM/'lib/PortableApps/src/desk_clock_faces.c',*helper,*quick,SLEEP,IDLE],homeflags)
 dso('springboard.elf',[ROOT/'test/shared_keyboard_handoff_client.c'])
 dso('points.elf',[ROOT/'test/shared_keyboard_points_client.c',SYSTEM/'lib/PortableApps/src/adapter.c',*helper],pointsflags)
 dso('ble.elf',[ROOT/'test/shared_keyboard_ble_client.c',SYSTEM/'lib/PortableApps/src/adapter.c',SYSTEM/'lib/PortableApps/src/PortableNativeTimeSource.c',*helper],bleflags)
 for name,cap,v in providers:dso(name+'.elf',[ROOT/'test/shared_keyboard_provider.c'],[f'-DCAPACITY_PROVIDER_ID="{name}"',f'-DCAPACITY_PROVIDER_CAP="{cap}"',f'-DCAPACITY_PROVIDER_VERSION={v}'])
 dso('scene.elf',[SYSTEM/'Services/scene_host/host.c']);dso('text.elf',[SYSTEM/'Services/text_input/host.c']);dso('profile.elf',[SYSTEM/'Services/scene_profile/profile.c'],['-DSCENE_PROFILE_ID="profile"','-DSCENE_PROFILE_PAPER=1','-DSCENE_DISPLAY_ROTATION=90'])
 dependencies(cc, base, [ROOT/'test/shared_keyboard_peripherals.c'], 'peripherals')
 run([cc,*base,'-c',ROOT/'test/shared_keyboard_peripherals.c','-o',out/'peripherals.o'])
 sources=[ROOT/'src'/n for n in ['bootstrap/Json.cpp','bootstrap/Board.cpp','bootstrap/Runtime.cpp','runtime/streams/AppStreamSessions.cpp','runtime/streams/ProviderQueueHost.cpp','runtime/drivers/ProviderGraphV2.cpp','runtime/drivers/ProviderModuleV2.cpp']]
 cxxflags=['-std=c++17','-O0','-g','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-DRISC_APP_POLICY_ROWS=17',*san,*['-I'+str(ROOT/d) for d in ['sdk/app','sdk/driver','sdk/hardware']],*incs,*['-I'+str(ROOT/d) for d in ['src','lib/ArduinoJson/src','test/drivers/stubs']],'-fno-pie']
 dependencies(cxx, cxxflags, [*sources, ROOT/'test/shared_keyboard_resident_test.cpp'], 'runtime')
 run([cxx,*cxxflags,'-no-pie','-rdynamic',*sources,ROOT/'test/shared_keyboard_resident_test.cpp',out/'peripherals.o','-ldl','-o',out/'test'])

if not a.skip_build:
    if tree_digest(input_trees()) != initial_tree_digest:
        raise SystemExit('Source/staged input tree changed during build; discard output and rebuild')
    for filename, expected in compiled_inputs.items():
        if digest(filename) != expected:
            raise SystemExit(f'Compiled input changed during build: {filename}')
    toolchain = {}
    for label, compiler in [('cc', cc), ('cxx', cxx)]:
        result = subprocess.run([compiler, '--version'], capture_output=True, text=True, check=True)
        resolved = shutil.which(compiler)
        toolchain[label] = {'command': compiler, 'resolved': resolved, 'version': result.stdout,
                           'sha256': digest(resolved) if resolved else None}
    build_inputs = {'schema_version': 1, 'configuration': configuration, 'git': input_git,
        'source_trees': initial_trees, 'source_trees_sha256': initial_tree_digest,
        'staged_header_sources': staged_header_sources,
        'compiled_inputs': compiled_inputs, 'compiled_inputs_sha256': json_digest(compiled_inputs),
        'dependency_scans': dependency_scans, 'commands': commands, 'toolchain': toolchain,
        'artifacts': {name: digest(out/name) for name in
                      [*[row[0]+'.elf' for row in providers], 'default.elf', 'springboard.elf',
                       'points.elf', 'ble.elf', 'scene.elf', 'text.elf', 'profile.elf', 'peripherals.o', 'test']}}
    write('build-inputs.json', build_inputs)

ble_entrypoint_scope = ('Selected BLE source contains the restored X4 paper frontend. This fixture initializes paper presentation but exercises controller entrypoints; successful complete BLE app_main execution and renderer behavior are not qualified here.' if ble_has_paper else 'Selected BLE production app_main requires 240x240 geometry, while the selected X4 adapter exposes 480x800. Naming modes replay real BLE controller functions. The separate entrypoint mode verifies geometry refusal without opening a text session.')
limitations = [
    'CAPACITY-ONLY CHECKPOINT: ' + ble_entrypoint_scope,
    'The image .50 BLE artifact has baseline-047-build-custody provenance from x4-fast-utility-clients-047/Apps/ble_scanner.c and ble_scanner_paper.inc; this fixture compiles the explicitly selected BLE source root instead and does not claim it reproduces that product artifact.',
    'Host-only production Runtime/ProviderGraph and selected production app/adapter/service sources; no device, hardware, target-memory, latency, power, or RF qualification.',
    'Controller test entrypoints call real production static startup/naming/handoff/cleanup functions; user input and peripheral completions are scripted.',
    'Provider count padded to 26 with 9 inert synthetic provider pins; peripheral, alarm, RF, and storage tables are synthetic.',
    'Product board instance IDs, compatibility and configuration are preserved; device dependency bindings are removed for synthetic peripheral manifests. This is not the complete production hardware dependency graph.',
    'BLE starts with one copied completed device; RF discovery is outside occupancy scope. Points uses an empty/default catalog plus a type draft: dataStat returns NOT_FOUND, and file read/replace are forbidden. Reported memory peaks are workload peaks, not full-catalog/full-scan worst-case application bounds.',
    'The handoff destination is a zero-grant synthetic springboard entrypoint, qualifying launcher handoff lifecycle rather than the production launcher footprint.',
    'Runtime graphPeak is the exact graph grant high-water mark. Host/foreground grant and provider peaks are sampled maxima at fixture observation points and may miss intermediate peaks.',
    'Allocation numbers are requested-live app/provider bytes and blocks observed through wrapped malloc/calloc/realloc/free. They exclude allocator bookkeeping/rounding, transient old-plus-new realloc copies, Runtime metadata, loader/linker allocations, app/provider static data and BSS, stacks, and static fake panel storage.',
    'Input digests include broad source trees plus compiler-reported transitive source/header dependencies, including host toolchain headers. Toolchain executables are fingerprinted; the complete host OS, dynamic linker, compiler support executables and linked system libraries are not vendored or fully hashed.',
]
evidence = {'checkpoint_scope': 'capacity-only',
    'ble_full_entrypoint_integrated': False,
    'ble_entrypoint_scope': ble_entrypoint_scope,
    'ble_paper_source_selected': ble_has_paper,
    'runtime_mocked': False, 'hardware_tested': False, 'selected_providers': 26,
    'full_pin_stress': a.activation == 'demand',
    'required_boot_pins_at_host_and_modal': 26 if a.activation == 'demand' else None,
    'input': a.input, 'activation': a.activation, 'sanitize': a.sanitize, 'runs': [],
    'status': 'built' if a.build_only else 'running',
    'home_build_defines': homeflags, 'client_build_defines': clientflags,
    'input_evidence': {'path': 'build-inputs.json', 'sha256': digest(out/'build-inputs.json'),
                       'source_trees_sha256': build_inputs['source_trees_sha256'],
                       'compiled_inputs_sha256': build_inputs['compiled_inputs_sha256'],
                       'build_configuration': configuration, 'current_git': input_git,
                       'reused_verified_build': a.skip_build},
    'runtime_metadata_sha256': {f.name: digest(f) for f in sorted(out.glob('*.json'))
                               if f.name not in {'evidence.json', 'build-inputs.json'}},
    'removed_board_dependency_bindings': removed_board_bindings,
    'limitations': limitations}
write('evidence.json', evidence)
if a.build_only:
    raise SystemExit(0)

failures = []
for client in ['points', 'ble'] if a.client == 'all' else [a.client]:
    for mode in modes:
        command = [str(out/'test'), str(out), client, mode]
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=30,
                env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0',
                     'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1', 'CAPACITY_INPUT': a.input})
            stdout, stderr, returncode, timed_out = result.stdout, result.stderr, result.returncode, False
        except subprocess.TimeoutExpired as error:
            def as_text(value):
                return value.decode(errors='replace') if isinstance(value, bytes) else value or ''
            stdout, stderr = as_text(error.stdout), as_text(error.stderr)
            stderr += '\nFixture exceeded 30-second timeout.\n'
            returncode, timed_out = None, True
        log = client+'-'+mode+'.log'
        (out/log).write_text(stdout+stderr)
        print(stdout+stderr, flush=True)
        passed = returncode == 0 and not timed_out
        evidence['runs'].append({'client': client, 'mode': mode, 'stdout': stdout, 'stderr': stderr,
            'returncode': returncode, 'timed_out': timed_out, 'passed': passed, 'log': log,
            'log_sha256': digest(out/log)})
        if not passed:
            failures.append(f'{client}/{mode}: '+('timeout' if timed_out else f'exit {returncode}'))
        # Preserve each result even if a later independent mode fails or the runner is interrupted.
        write('evidence.json', evidence)
evidence['status'] = 'failed' if failures else 'passed'
evidence['failures'] = failures
write('evidence.json', evidence)
if failures:
    raise SystemExit('Fixture matrix failures: '+', '.join(failures))
