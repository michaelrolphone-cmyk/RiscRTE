#!/usr/bin/env python3
"""Generic identity-registry boundaries and actual lifecycle host integration."""
import argparse,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True)
 flags=['-O0','-g','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-fno-omit-frame-pointer']
 if os.environ.get('SANITIZE')=='1':flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
 include_dirs=('','src','src/bootstrap','sdk/app','sdk/driver','sdk/hardware','lib/hal','lib/ArduinoJson/src','test/drivers/stubs','test/native_nvs_shim','test/native_sleep_diagnostics_idf6_shim','test/native_platform_shim','test/native_settings_worker_shim','test')
 inc=['-I'+str(ROOT/x) for x in include_dirs];commands=[]
 def run(cmd,label):
  cmd=list(map(str,cmd));commands.append(cmd)
  r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
  (out/(label+'.log')).write_text(r.stdout);print(r.stdout,end='',flush=True)
  if r.returncode:raise RuntimeError('Failed '+label)
 cxx=os.environ.get('CXX','c++')
 run([cxx,'-std=c++17',*flags,*inc,'-no-pie',ROOT/'test/native_settings_writer_registry_test.cpp','-o',out/'registry'],'registry-compile')
 run([out/'registry'],'registry')
 selected=['-DRISC_NATIVE_SERIAL_FILES=1','-DRISC_NATIVE_SETTINGS_PORT_SELECTED=1','-DRISC_NATIVE_SETTINGS_LIFECYCLE=1','-DRISC_NATIVE_SETTINGS_WORKER=1','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1','-DRISC_NATIVE_STORAGE_CUSTODY=1','-DRISC_NATIVE_SETTINGS_MAX_WRITERS=7','-DRISC_NATIVE_SETTINGS_MAX_IDENTITIES=6','-DRISC_RUNTIME_ATOMIC_BINDING_CAPACITY=9']
 run([cxx,'-std=c++17',*flags,*inc,*selected,'-DESP_PLATFORM','-c',ROOT/'src/ports/esp32s3/NativeSettingsLifecycle.cpp','-o',out/'lifecycle.o'],'lifecycle-compile')
 sources=['src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp','src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp','src/runtime/storage/AppDataFiles.cpp','src/runtime/serial/Files.cpp','src/ports/esp32s3/NativeSerialFiles.cpp','src/ports/esp32s3/CpuPort.cpp','test/native_settings_writer_identity_test.cpp']
 run([cxx,'-std=c++17',*flags,*inc,*selected,'-DRISC_NATIVE_ESP_IDF=1','-rdynamic','-no-pie','-pthread',*[ROOT/s for s in sources],out/'lifecycle.o','-ldl','-Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename','-o',out/'lifecycle'],'integration-compile')
 for mode in ('valid','duplicate','overflow','long','activation','retained'):
  directory=out/mode;directory.mkdir()
  run([out/'lifecycle',mode,directory],mode)
 (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 print('Identity registry and actual lifecycle host tests passed; target memory/native firmware not qualified')
if __name__=='__main__':main()
