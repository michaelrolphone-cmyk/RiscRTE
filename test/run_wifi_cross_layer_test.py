#!/usr/bin/env python3
"""Exercise selected production Wi-Fi app/provider/native radio together."""
import argparse,hashlib,json,os,shutil,subprocess,sys,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--system',type=Path,required=True)
p.add_argument('--watch',type=Path,required=True)
p.add_argument('--utilities',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--check-policy-classification',action='store_true',help='Expect the native policy-classification repair; baseline intentionally fails')
p.add_argument('--system-ref',default='7b175418e3063d9f4c571acf06c366144831b2af',help='Immutable app/controller source commit')
a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
SYSTEM=a.system_ref;WATCH='cf30d732271db19a74492d4753736cafeafb92bb';BLE='4966acee548e6cc3997289db206e996ed4c15cda'
receipts={}
def read(repo,ref,path):
 data=subprocess.check_output(['git','-C',repo,'show',ref+':'+path]);receipts[ref+':'+path]=hashlib.sha256(data).hexdigest();return data
def extract(repo,ref,paths,out):
 for path in paths:
  target=out/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(read(repo,ref,path))
def files(repo,ref,dirs):
 return subprocess.check_output(['git','-C',repo,'ls-tree','-r','--name-only',ref,*dirs],text=True).splitlines()
flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-missing-field-initializers']
if os.getenv('SANITIZE')=='1':flags+=['-O0','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
with tempfile.TemporaryDirectory(prefix='wifi-cross-layer-') as temp:
 b=Path(temp);system=b/'system';watch=b/'watch';ble=b/'ble'
 # Reuse only SDK declarations/implementations, never the old unit-test main.
 shim=(ROOT/'test/native_radio_test.cpp').read_text().split('int main(){',1)[0]
 (b/'native_wifi_sdk_fixture.inc').write_text(shim)
 extract(a.system,SYSTEM,files(a.system,SYSTEM,['Apps','lib/PortableApps','lib/NativeApps','test/native_apps','scripts']),system)
 extract(a.watch,WATCH,files(a.watch,WATCH,['include','sdk','drivers/twatch_wifi']),watch)
 extract(a.watch,BLE,files(a.watch,BLE,['include','sdk','drivers/twatch_ble']),ble)
 entry=b/'app-entry.c';entry.write_text('#include "native_system_app_entry.c"\nconst char* cross_app_message(void){return wifi_message;}\n')
 sys.path.insert(0,str(system/'scripts'));import portable_native_toolbar_build as native
 inc=b/'include';shutil.copytree(system/'lib/PortableApps/include',inc);shutil.copytree(system/'lib/PortableApps/time',b/'time')
 for n in native.SDK_HEADERS:(inc/n).write_bytes(read(ROOT,native.RUNTIME_COMMIT,'sdk/app/'+n))
 for n in native.portable_alarm_build.HEADERS:(inc/n).write_bytes(read(a.utilities,native.portable_alarm_build.UTILITIES_COMMIT,'lib/Alarm/include/'+n))
 common=['-I'+str(inc),'-I'+str(system/'lib/NativeApps/include')]
 defines=['-Drisc_runtime_get_api=fixture_risc_runtime_get_api','-DPORTABLE_NATIVE_TIME_TOOLBAR','-DPORTABLE_NATIVE_CUSTODY_FENCE','-DALARM_SERVICE_TAGGED_V2','-DTEST_NATIVE_TOOLBAR_QUICK','-DPORTABLE_QUICK_ACTIONS','-DPORTABLE_QUICK_RADIOS','-DPORTABLE_ALARM_CLIENT','-DPORTABLE_INPUT_NAVIGATION','-DPORTABLE_HOME_APP="default.elf"','-DPORTABLE_WIFI_SETTINGS_APP','-DPORTABLE_WIFI_INSTANCE=15u','-DPORTABLE_WIFI_STORAGE_INSTANCE=6','-DWIFI_RETURN_APP="springboard.elf"','-DPORTABLE_BLE_BROADCAST','-DPORTABLE_BLE_BROADCAST_DEFAULT_OFF','-DPORTABLE_STAGE_LOGS','-DPORTABLE_TOUCH_SCROLL','-DPORTABLE_TOUCH_ROTATION=0','-DPORTABLE_PAPER_PREFERENCES','-DPORTABLE_PAPER_TRANSITIONS','-DPORTABLE_X4_IDLE_POLICY','-DPORTABLE_LOW_BATTERY','-DPORTABLE_APP_SLEEP_LOCAL']
 sources=[ROOT/'test/wifi_app_cross_layer.c',entry,*[system/s for s in native.SOURCES],*[system/'lib/PortableApps/src'/s for s in ['quick_actions.c','quick_render.c','quick_session.c','quick_radios.c']]]
 objects=[]
 for i,source in enumerate(sources):
  obj=b/(str(i)+'.o');objects.append(obj)
  subprocess.run([os.getenv('CC','cc'),'-std=c11',*flags,*common,*defines,'-I'+str(system/'test/native_apps'),'-c',source,'-o',obj],check=True)
 for name,tree in [('wifi',watch),('hci',ble)]:
  source=tree/('drivers/twatch_'+('wifi' if name=='wifi' else 'ble')+'/driver.c');obj=b/(name+'.o');objects.append(obj)
  subprocess.run([os.getenv('CC','cc'),'-std=c11',*flags,'-I'+str(tree/'include'),'-I'+str(tree/'sdk/driver'),'-Dt5_driver_get=production_'+name+'_get','-c',source,'-o',obj],check=True)
 public=b/'public';public.mkdir();shutil.copyfile(inc/'WifiApi.h',public/'WifiApi.h')
 runtime=['src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp','src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp','src/ports/esp32s3/CpuPort.cpp']
 includes=[b,ROOT/'test/native_radio_shim',ROOT/'test/native_hci_shim',ROOT/'src',ROOT/'sdk/app',ROOT/'sdk/driver',ROOT/'sdk/hardware',ROOT/'lib/ArduinoJson/src',ROOT/'test/drivers/stubs',ble/'include',ble/'sdk/driver',public]
 exe=a.output/'wifi-cross-layer'
 subprocess.run([os.getenv('CXX','c++'),'-std=c++17',*flags,'-no-pie','-rdynamic','-DRISC_STAGE_LOGS=1','-DMALLOC_CAP_INTERNAL=1','-I'+str(ROOT/'test'),*['-I'+str(x) for x in includes],*objects,*[ROOT/s for s in runtime],ROOT/'test/wifi_cross_layer_test.cpp','-Wl,--wrap=free','-ldl','-o',exe],check=True)
 outputs=[]
 cases=['cold-navigation','sdk-init-failure','allocation-failure','iq-lease','cleanup-failure','telemetry-failure','bluetooth-fault','prior-wifi-lease']
 if a.check_policy_classification:cases+=['policy-io','policy-corrupt','policy-off']
 for case in cases:
  r=subprocess.run([exe,case],text=True,capture_output=True);outputs.append(r.stdout+r.stderr);print(outputs[-1],end='');(a.output/(case+'.log')).write_text(outputs[-1]);r.check_returncode()
 (a.output/'source-receipt.json').write_text(json.dumps({'system':SYSTEM,'wifi':WATCH,'hci':BLE,'sha256':receipts,'sanitized':os.getenv('SANITIZE')=='1','hardware':'not run','app_build_defines':defines,'cases':cases,'runtime_sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'src/ports/esp32s3/NativeRadio.h',ROOT/'src/ports/esp32s3/NativeHci.h',ROOT/'src/ports/esp32s3/CpuPort.cpp',ROOT/'src/bootstrap/Runtime.cpp',ROOT/'src/bootstrap/ProviderPromotionRuntime.inc',ROOT/'test/wifi_app_cross_layer.c',ROOT/'test/wifi_cross_layer_test.cpp']},'limits':'UI, storage, time and outer grants use deterministic fixtures; production controller, adapter, Wi-Fi/HCI providers, CpuPort and native backends execute.'},indent=2)+'\n')
