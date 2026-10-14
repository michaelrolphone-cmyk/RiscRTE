#!/usr/bin/env python3
"""Compile-only .99-cohort probes. Emits local frames, never whole-stack/hardware claims."""
import argparse,hashlib,json,pathlib,shlex,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('compile_commands',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);p.add_argument('--lifecycle',action='store_true');p.add_argument('--maintenance-only',action='store_true');p.add_argument('--payload-only',action='store_true');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
entry=next(x for x in json.loads(a.compile_commands.read_text()) if x['file'].endswith('/Runtime.cpp'))
command=shlex.split(entry['command']);tool=command[0];flags=[];skip=False
for arg in command[1:]:
 if skip:skip=False;continue
 if arg=='-o':skip=True;continue
 if arg in ('-c','-MMD') or arg.endswith('/Runtime.cpp'):continue
 if arg.startswith('-I') and not pathlib.Path(arg[2:]).is_absolute():arg='-I'+str(root/arg[2:])
 if arg.startswith('-ffile-prefix-map='):arg=f'-ffile-prefix-map={root}=.'
 flags.append(arg)
flags+=['-I'+str(root),'-fstack-usage','-fno-access-control']
manifest={'source_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'compile_database_sha256':hashlib.sha256(a.compile_commands.read_bytes()).hexdigest(),'cohort_flags':flags,'calls':[], 'source_sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for folder in ('src','experimental/native_replica','sdk','test') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in ('.h','.cpp','.c','.inc','.py','.sh')}}
sources=[('src/bootstrap/Runtime.cpp','runtime-off',[]),('test/atomic_key_value_target_probe.cpp','runtime-layout-off',[]),('src/ports/esp32s3/NativeSettingsWorker.cpp','worker-off',[]),('src/bootstrap/Runtime.cpp','runtime-on',['RISC_RUNTIME_ATOMIC_KEY_VALUE=1']),('test/atomic_key_value_target_probe.cpp','runtime-layout-on',['RISC_RUNTIME_ATOMIC_KEY_VALUE=1']),('src/ports/esp32s3/NativeSettingsWorker.cpp','worker-on',['RISC_NATIVE_SETTINGS_WORKER=1']),('test/native_settings_worker_layout_probe.cpp','worker-layout',['RISC_NATIVE_SETTINGS_WORKER=1']),('test/native_settings_resource_probe.cpp','resources',['RISC_RUNTIME_ATOMIC_KEY_VALUE=1','RISC_NATIVE_STORAGE_CUSTODY=1']),('test/native_sd_consumers_target_probe.cpp','consumers',[]),('test/native_first_sd_checkpoint_target_probe.cpp','first',[]),('test/native_import_initialization_target_probe.cpp','import',[])]
if a.payload_only:
 defines=['BOARD_XTEINK_X4_PRO','RISC_NATIVE_SETTINGS_PSRAM_PAYLOAD=1']
 probe=out/'payload-layout.cpp';probe.write_text('#include "'+str(root/'src/ports/esp32s3/NativeSettingsPayloadAllocator.cpp')+'"\nextern "C" { char resource_payload_allocator[sizeof(RiscExperimental::NativeReplicaCore::Allocator)]; char resource_payload_alignment[RiscCpu::NativeSettingsPayloadAllocator::Alignment]; char resource_payload_header[sizeof(RiscCpu::NativeSettingsPayloadAllocator::Header)]; char resource_payload_overhead[RiscCpu::NativeSettingsPayloadAllocator::Overhead]; }\n')
 sources=[('src/ports/esp32s3/NativeSettingsPayloadAllocator.cpp','payload',defines),(str(probe),'payload-layout',defines),('src/ports/esp32s3/NativeSettingsPayloadAllocator.cpp','payload-off',[])]
elif a.maintenance_only:
 defines=['BOARD_XTEINK_X4_PRO','RISC_NATIVE_SETTINGS_LIFECYCLE=1','RISC_NATIVE_SETTINGS_WORKER=1','RISC_RUNTIME_ATOMIC_KEY_VALUE=1','RISC_NATIVE_STORAGE_CUSTODY=1']
 probe=out/'maintenance-layout.cpp';probe.write_text('#include "'+str(root/'src/ports/esp32s3/NativeSettingsMaintenance.h')+'"\n#include "'+str(root/'src/ports/esp32s3/NativeSettingsBootstrap.h')+'"\nextern "C" { char resource_maintenance[sizeof(RiscCpu::NativeSettingsMaintenance)]; char resource_bootstrap[sizeof(RiscCpu::NativeSettingsBootstrap)]; char resource_fresh_sources[sizeof(RiscExperimental::CheckedNvsFreshSources)]; char resource_first_authority[sizeof(RiscExperimental::CheckedFirstSdCheckpoint::Authority)]; }\n')
 sources=[('src/ports/esp32s3/NativeSettingsMaintenance.cpp','maintenance',defines),('src/ports/esp32s3/NativeSettingsBootstrap.cpp','bootstrap',defines),(str(probe),'maintenance-layout',defines),('src/ports/esp32s3/NativeSettingsMaintenance.cpp','maintenance-off',[]),('src/ports/esp32s3/NativeSettingsBootstrap.cpp','bootstrap-off',[])]
elif a.lifecycle:
 defines=['BOARD_XTEINK_X4_PRO','RISC_NATIVE_SETTINGS_LIFECYCLE=1','RISC_NATIVE_SETTINGS_WORKER=1','RISC_RUNTIME_ATOMIC_KEY_VALUE=1','RISC_NATIVE_STORAGE_CUSTODY=1']
 (out/'RiscBuildIdentity.h').write_text('#pragma once\n#define RISC_BUILD_IDENTITY "RTE_SOURCE=compile-only-resource-probe"\n#define RISC_BUILD_VERSION "0.0.0"\n')
 flags+=['-I'+str(out)]
 probe=out/'lifecycle-layout.cpp';probe.write_text('#include "'+str(root/'src/ports/esp32s3/NativeSettingsLifecycle.cpp')+'"\nextern "C" { char resource_lifecycle_state[sizeof(RiscCpu::NativeSettingsLifecycle::state)]; char resource_lifecycle_load[sizeof(RiscCpu::NativeSettingsLifecycle::Load)]; }\n')
 sources += [('src/main.cpp','main-off',[]),('src/main.cpp','main-lifecycle',defines),('src/bootstrap/Runtime.cpp','runtime-lifecycle',defines),('test/atomic_key_value_target_probe.cpp','runtime-layout-lifecycle',defines),('src/ports/esp32s3/NativeSettingsLifecycle.cpp','lifecycle',defines),(str(probe),'lifecycle-layout',defines),('src/ports/esp32s3/NativeSettingsLifecycle.cpp','lifecycle-off',[])]
 sources += [('src/runtime/storage/AppDataFiles.cpp','appdata',[])]
subprocess.run([tool,'--version'],check=True)
for source,stem,defines in sources:
 cmd=[tool,*flags,*['-D'+x for x in defines],'-c',str(root/source),'-o',str(out/(stem+'.o'))];manifest['calls'].append(cmd);print('COMPILE',stem,flush=True);subprocess.run(cmd,cwd=root,check=True)
 nm=tool.removesuffix('g++')+'nm';nmtext=subprocess.check_output([nm,'-S',str(out/(stem+'.o'))],text=True);(out/(stem+'.nm')).write_text(nmtext)
 if stem in ('worker-off','lifecycle-off','maintenance-off','bootstrap-off','payload-off'):assert not nmtext.strip(), stem+' must be empty'
 for line in nmtext.splitlines():
  if ' resource_' in line or ' atomic_' in line or ' native_settings_' in line:print(line,flush=True)
manifest['artifacts']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.iterdir()) if p.is_file() and p.suffix in ('.o','.su','.nm')}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
