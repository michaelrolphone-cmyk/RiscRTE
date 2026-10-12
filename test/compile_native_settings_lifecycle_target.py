#!/usr/bin/env python3
"""Serial compile-only real entry point, lifecycle, worker and Runtime; no product selection."""
import argparse,json,pathlib,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('packages',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
framework=a.packages/'framework-arduinoespressif32';sdk=framework/'tools/sdk/esp32s3';tool=a.packages/'toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf'
(out/'RiscBuildIdentity.h').write_text('#pragma once\n#define RISC_BUILD_IDENTITY "RTE_SOURCE=compile-only"\n#define RISC_BUILD_VERSION "0.0.0"\n')
includes=[out,root,root/'src',root/'include',root/'sdk/app',root/'sdk/driver',root/'sdk/hardware',root/'lib/ArduinoJson/src',root/'lib/elf_loader/include',root/'lib/elf_loader/src',root/'lib/hal',framework/'cores/esp32',framework/'variants/esp32s3',sdk/'qio_opi/include',sdk/'include/esp_hw_support/include/soc',sdk/'include/lwip/include/apps',sdk/'include/lwip/lwip/src/include',sdk/'include/lwip/port/esp32/include',sdk/'include/lwip/port/esp32/include/arch',sdk/'include/freertos/include/esp_additions/freertos',sdk/'include/freertos/include/esp_additions']
includes+=sorted(x for x in (sdk/'include').rglob('*') if x.is_dir() and (x.name in ('include','platform_include','private_include') or x==sdk/'include/freertos/port/xtensa/include'))
flags=['-std=gnu++17','-Os','-mlongcalls','-fno-rtti','-fno-exceptions','-fstack-usage','-DESP_PLATFORM','-D_GNU_SOURCE','-include','sdkconfig.h','-DARDUINO=10812','-DARDUINO_ARCH_ESP32','-DARDUINO_ESP32S3_DEV','-DBOARD_XTEINK_X4_PRO','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-Wno-unused-function']
flags+=['-I'+str(x) for x in includes]
selected=['-DRISC_NATIVE_SETTINGS_LIFECYCLE=1','-DRISC_NATIVE_SETTINGS_WORKER=1','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1','-DRISC_NATIVE_STORAGE_CUSTODY=1']
cohort=['-DRISC_RUNTIME_METADATA_PSRAM=1','-DRISC_PROVIDER_CAPACITY=40','-DRISC_INSTALLED_APP_CAPACITY=64','-DRISC_APP_POLICY_ROWS=24','-DRISC_APP_REQUIREMENT_ROWS=24','-DRISC_RETAINED_WAKE_BYTES=512']
commands=[]
for layout,extra in [('legacy',[]),('cohort',cohort)]:
 for source in ('src/main.cpp','src/bootstrap/Runtime.cpp','src/ports/esp32s3/NativeSettingsLifecycle.cpp','src/ports/esp32s3/NativeSettingsWorker.cpp','src/ports/esp32s3/NativeSettingsMaintenance.cpp','src/ports/esp32s3/NativeSettingsBootstrap.cpp'):
  output=out/(pathlib.Path(source).stem+'-'+layout+'.o');cmd=[str(tool)+'-g++',*flags,*selected,*extra,*(['-Wno-misleading-indentation'] if source=='src/main.cpp' else []),'-c',str(root/source),'-o',str(output)];commands.append(cmd);subprocess.run(cmd,check=True)
# Compile the real graph wrapper under the hidden-app native checkpoint gate.
pin=out/'pin.cpp';pin.write_text('#include "experimental/native_replica/NativeSdPortAttempt.h"\nint native_checkpoint_graph_pin(RiscBoot::Runtime&r,RiscCpu::Port&p){RiscExperimental::NativeRuntimeStoragePin pin(r,p,55);return pin.acquire();}\n')
cmd=[str(tool)+'-g++',*flags,*selected,'-c',str(pin),'-o',str(out/'pin.o')];commands.append(cmd);subprocess.run(cmd,check=True)
# Include the actual implementation to expose its private fixed control layout.
probe=out/'layout.cpp';probe.write_text('#include "'+str(root/'src/ports/esp32s3/NativeSettingsLifecycle.cpp')+'"\nextern "C" { char native_settings_lifecycle_state_bytes[sizeof(RiscCpu::NativeSettingsLifecycle::state)]; char native_settings_lifecycle_load_bytes[sizeof(RiscCpu::NativeSettingsLifecycle::Load)]; }\n')
cmd=[str(tool)+'-g++',*flags,*selected,'-c',str(probe),'-o',str(out/'layout.o')];commands.append(cmd);subprocess.run(cmd,check=True)
subprocess.run([str(tool)+'-nm','-S',str(out/'layout.o')],check=True)
probe=out/'maintenance-layout.cpp';probe.write_text('#include "'+str(root/'src/ports/esp32s3/NativeSettingsBootstrap.h')+'"\nextern "C" { char native_settings_maintenance_bytes[sizeof(RiscCpu::NativeSettingsMaintenance)]; char native_settings_bootstrap_bytes[sizeof(RiscCpu::NativeSettingsBootstrap)]; }\n')
cmd=[str(tool)+'-g++',*flags,*selected,'-c',str(probe),'-o',str(out/'maintenance-layout.o')];commands.append(cmd);subprocess.run(cmd,check=True)
subprocess.run([str(tool)+'-nm','-S',str(out/'maintenance-layout.o')],check=True)
for source in ('src/ports/esp32s3/NativeSettingsLifecycle.cpp','src/ports/esp32s3/NativeSettingsMaintenance.cpp','src/ports/esp32s3/NativeSettingsBootstrap.cpp'):
 cmd=[str(tool)+'-g++',*flags,'-c',str(root/source),'-o',str(out/(pathlib.Path(source).stem+'-default-off.o'))];commands.append(cmd);subprocess.run(cmd,check=True)
 assert not subprocess.check_output([str(tool)+'-nm',str(out/(pathlib.Path(source).stem+'-default-off.o'))],text=True).strip()
(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
print('Actual main/Runtime/lifecycle/worker compile-only and default-off empty object PASS')
