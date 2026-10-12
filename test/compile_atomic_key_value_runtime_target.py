#!/usr/bin/env python3
"""Compile actual Runtime.cpp and layout probes with selected ESP32-S3 headers. No link or hardware qualification."""
import argparse,pathlib,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('packages',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);p.add_argument('--stack-usage',action='store_true',help='Emit compiler local-frame reports; these are not whole-call-chain or hardware stack qualification');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
sdk=a.packages/'framework-arduinoespressif32/tools/sdk/esp32s3';tool=a.packages/'toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf'
includes=[root,root/'src',root/'sdk/app',root/'sdk/driver',root/'sdk/hardware',root/'lib/ArduinoJson/src',root/'lib/elf_loader/include',root/'lib/elf_loader/src',root/'lib/hal',sdk/'qio_opi/include',sdk/'include/freertos/include/esp_additions/freertos',sdk/'include/freertos/include/esp_additions']
includes += sorted(x for x in (sdk/'include').rglob('*') if x.is_dir() and (x.name in ('include','platform_include','private_include') or x==sdk/'include/freertos/port/xtensa/include'))
flags=['-std=gnu++17','-Os','-mlongcalls','-fno-rtti','-fno-exceptions','-DESP_PLATFORM','-D_GNU_SOURCE','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers']
if a.stack_usage:flags+=['-fstack-usage']
flags+=['-I'+str(x) for x in includes]
subprocess.run([str(tool)+'-g++','--version'],check=True)
for layout,selected in [('default',[]),('cohort',['-DRISC_RUNTIME_METADATA_PSRAM=1','-DRISC_PROVIDER_CAPACITY=40','-DRISC_INSTALLED_APP_CAPACITY=64','-DRISC_APP_POLICY_ROWS=24','-DRISC_APP_REQUIREMENT_ROWS=24','-DRISC_RETAINED_WAKE_BYTES=512'])]:
 for feature in (0,1):
  name=f'{layout}-{feature}'
  for source,stem in [('src/bootstrap/Runtime.cpp','Runtime'),('test/atomic_key_value_target_probe.cpp','probe')]:
   command=[str(tool)+'-g++',*flags,*selected,f'-DRISC_RUNTIME_ATOMIC_KEY_VALUE={feature}','-c',str(root/source),'-o',str(out/(stem+'-'+name+'.o'))]
   subprocess.run(command,check=True)
  print(name,flush=True);subprocess.run([str(tool)+'-nm','-S',str(out/('probe-'+name+'.o'))],check=True)
