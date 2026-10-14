"""Execute real setup/cooperate and lifecycle with checked native host stores.
The Runtime/RTOS are shims here; the real Runtime checkpoint has a separate test.
No private target filesystem, product selector, firmware or hardware is admitted.
"""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest
import test_native_startup as baseline
ROOT = baseline.ROOT
SHIM = baseline.SHIM.replace('inline uint32_t millis(){return 0;}', 'extern uint32_t clockMs;inline uint32_t millis(){return clockMs;}')
SHIM = SHIM.replace('inline void vTaskDelay(uint32_t){mark("yield");}', 'void testScheduler(uint32_t);inline void vTaskDelay(uint32_t n){testScheduler(n);}')
SHIM = SHIM.replace('using esp_err_t=int;constexpr esp_err_t ESP_OK=0;', '#include <nvs.h>')
SHIM = SHIM.replace('namespace RiscDiagnostics{', 'extern unsigned diagnosticsPolls;namespace RiscDiagnostics{').replace('inline void poll(){}', 'inline void poll(){++diagnosticsPolls;}')
SHIM = SHIM.replace('bool(*controlExitRequested)();};', 'bool(*controlExitRequested)();bool(*nativeOwnerCheckpoint)(bool);bool(*nativeDiscoveryCheckpoint)();bool(*nativeSettingsShutdown)();};\nvoid testRuntimeRun(Port&);')
a = SHIM.index('class Runtime{Board b;')
b = SHIM.index('\n}\nnamespace RiscCpu', a)
SHIM = SHIM[:a] + r'''
extern bool dataGrants,runtimeRetained;class Runtime{Board b;Port p;public:explicit Runtime(Port v):p(v){}Board&board(){return b;}
 bool registerAtomicKeyValueReadThrough(uint32_t ns,RiscExperimental::NativeAtomicKeyValue&e){assert(e.namespaceId()==ns);mark(ns==1?"read1":"read6");return true;}
 bool registerAtomicKeyValue(const char*,uint32_t ns,RiscExperimental::NativeAtomicKeyValue&e){assert(e.namespaceId()==ns);mark(ns==1?"write1":"write6");return true;}
 struct KeyValueAvailability {void*context;int32_t(*snapshot)(void*,uint32_t,risc_key_value_availability_state_v1*);};
 bool registerKeyValueAvailability(uint32_t,KeyValueAvailability){return true;}
 bool keyValueReconciliationSafe(uint32_t)const{return !dataGrants;}
 bool keyValueRetentionObserved()const{return runtimeRetained;}
 bool prepare(const char*){mark("prepare");return true;}bool run(){mark("run");testRuntimeRun(p);return true;}const char*error(){return "test";}
};
''' + SHIM[b:]
SHIM = SHIM.replace('namespace RiscBoot{', '#include "experimental/native_replica/NativeSettingsCoordinator.h"\n#include <RiscKeyValueAvailabilityV1.h>\nnamespace RiscBoot{', 1)
SHIM = SHIM.replace('class Port{public:explicit Port(int){}', 'class Port{public:bool attached=false;explicit Port(int){}bool nativeStorageAttached(){return attached;}')
SHIM = SHIM.replace('bool providerStorageSafe(){return true;}', 'bool providerStorageSafe(){return !attached;}bool nativeDiscoveryProviderSafe(){return true;}bool nativeDiscoveryAppExitSafe(){return true;}')
WORKER = r'''
#pragma once
#include "experimental/native_replica/NativeSettingsCoordinator.h"
namespace RiscCpu { namespace NativeSettingsWorker {
using Coordinator=RiscExperimental::NativeSettingsCoordinator;
bool prepare(uint32_t,RiscExperimental::NativeReplicaCore&,RiscExperimental::CheckedNativeReplicaStore&,bool(*)());
Coordinator*coordinator(uint32_t);bool exitSafe();
} }
'''
HARNESS = r'''
#include "shim.h"
#include "NativeSettingsLifecycle.h"
#include "NativeSettingsWorker.h"
#include "test/native_replica_checked_fixture.h"
#include <memory>
namespace L=RiscCpu::NativeSettingsLifecycle;namespace W=RiscCpu::NativeSettingsWorker;namespace NF=NativeFixture;
namespace RiscBoot {bool dataGrants=false,runtimeRetained=false;}
std::vector<std::string>calls;uint32_t clockMs=0;unsigned diagnosticsPolls=0;
std::string mode;bool selected=false;uint32_t onNamespace=0;unsigned registered=0,delays=0;bool failSecond=false;
std::unique_ptr<W::Coordinator>services[2];
const void*deniedInternal=nullptr;
bool esp_ptr_internal(const void*p){return p&&p!=deniedInternal;}
namespace RiscCpu {namespace NativeSettingsWorker {
bool prepare(uint32_t ns,C&core,S&store,bool(*owner)()){
 assert(!NF::onWorker&&!core.ready()&&core.namespaceId()==ns&&store.namespaceId()==ns&&core.usesBackend(store.backend()));
 if(failSecond&&ns==6)return false;
 const unsigned i=ns==6?1:0;
 services[i]=std::make_unique<Coordinator>(core,store,Coordinator::Hooks{reinterpret_cast<void*>(uintptr_t(ns)),
 [](void*){return !NF::onWorker;},[](void*p){return NF::onWorker&&onNamespace==uintptr_t(p);},
 [](void*){if(NF::lease)return false;NF::lease=true;return true;},[](void*){assert(NF::lease);NF::lease=false;},
 [](void*,const void*p,size_t n){return p&&n;}});assert(owner());return true;
}
Coordinator*coordinator(uint32_t ns){return services[ns==6?1:0].get();}
bool exitSafe(){for(auto&s:services)if(s&&!s->exitSafe())return false;return true;}
}}
void testScheduler(uint32_t n){
 assert(!NF::onWorker);clockMs+=n;++delays;
 if(mode=="cancel-load"&&delays==1)return;
 for(unsigned i=0;i<2;++i)if(services[i]){NF::onWorker=true;onNamespace=i?6:1;services[i]->workerStep();NF::onWorker=false;onNamespace=0;}
}
struct Model {unsigned begins=0,polls=0,cancels=0,preApps=0,until=3;bool busy=false,held=false,retained=false,cancel=false;RiscCpu::Port*port=nullptr;};
Model model;std::unique_ptr<NF::Fixture>a,b;L::Admission admission{};
static const L::Writer writers[]={{"settings",1},{"paper-clock",1},{"battery",1},{"ble-scanner",1},{"contexts",1},{"wifi-settings",6}};
#ifndef NO_ADMISSION
extern "C" const L::Admission* risc_native_settings_admission(){mark("admission");return selected?&admission:nullptr;}
#endif
extern void setup();extern void loop();
void configure(){
 admission={{&a->core,&a->store},{&b->core,&b->store},writers,6,&model,sizeof model,
 [](void*,W::Coordinator&x,W::Coordinator&y){assert(x.namespaceId()==1&&y.namespaceId()==6);++registered;return true;},
 [](void*p,RiscBoot::Runtime&,RiscCpu::Port&port){auto&m=*static_cast<Model*>(p);++m.begins;assert(!m.held);if(m.busy)return L::Progress::Deferred;m.port=&port;m.held=port.attached=true;return L::Progress::Pending;},
 [](void*p){auto&m=*static_cast<Model*>(p);assert(m.held&&!L::exitSafe()&&!m.port->providerStorageSafe());++m.polls;L::cooperate();assert(L::checkpoint());
  if(m.retained)return L::Progress::Retained;
  if(m.polls<m.until)return L::Progress::Pending;
  if(mode=="preapp-loaded"){NF::onWorker=NF::lease=true;C::Value recovered{"setting","recovered",9};assert(!b->core.initialize(&recovered,1,{}));b->store.finishOperation();NF::onWorker=NF::lease=false;}
  m.held=m.port->attached=false;return L::Progress::Idle;},
 [](void*p){auto&m=*static_cast<Model*>(p);++m.cancels;m.cancel=true;},
 [](void*p){return !static_cast<Model*>(p)->held;},[](void*p){return static_cast<Model*>(p)->retained&&static_cast<Model*>(p)->held;}};
}
namespace RiscBoot {void testRuntimeRun(Port&p){
 if(mode.rfind("preapp-",0)==0||mode=="initializing"){
  assert(L::ready()&&a->core.ready()&&!b->core.ready()&&model.preApps==0);
  risc_key_value_availability_state_v1 status{};status.struct_size=sizeof(status);
  assert(!L::availability(6,&status)&&status.availability==RISC_KEY_VALUE_UNAVAILABLE);
  assert(!L::availability(1,&status)&&status.availability==RISC_KEY_VALUE_AVAILABLE);
  const auto old=delays;assert(p.nativeDiscoveryCheckpoint());assert(delays==old&&model.preApps==1);
  if(mode=="preapp-pending"||mode=="preapp-loaded"||mode=="preapp-runtime-retained"){
   assert(L::discovering()&&!L::exitSafe()&&p.exitSafe()&&p.storageSafe());
   if(mode=="preapp-runtime-retained"){runtimeRetained=true;const auto polls=model.polls;L::cooperate();assert(L::retained()&&!L::exitSafe()&&model.polls==polls&&model.held);return;}
   dataGrants=true;model.until=mode=="preapp-pending"?101:3;
   for(unsigned i=0;i<model.until;++i){const auto n=delays;assert(p.nativeDiscoveryCheckpoint());assert(delays==n);}
   assert(!L::discovering()&&L::exitSafe());
   assert(!L::availability(6,&status));
   if(mode=="preapp-loaded"){
    assert(b->core.ready()&&status.availability==RISC_KEY_VALUE_RECONCILIATION_PENDING);
    const auto generation=status.generation;assert(p.nativeDiscoveryCheckpoint());assert(!L::availability(6,&status)&&status.generation==generation);
    dataGrants=false;assert(p.nativeDiscoveryCheckpoint());assert(!L::availability(6,&status)&&status.availability==RISC_KEY_VALUE_AVAILABLE&&status.generation>generation);
    assert(NF::value(b->core,"setting")=="recovered");
   }else assert(status.availability==RISC_KEY_VALUE_UNAVAILABLE&&!b->core.ready());
  }else{
   for(unsigned i=0;i<100;++i){assert(p.nativeDiscoveryCheckpoint());}assert(model.preApps==1);
   clockMs+=1000;assert(p.nativeDiscoveryCheckpoint());assert(model.preApps==2&&!model.begins);
  }
  assert(p.nativeSettingsShutdown()&&L::exitSafe());return;
 }
 assert(L::ready()&&a->core.ready()&&b->core.ready()&&!NF::onWorker&&!NF::lease);
 assert(NF::value(a->core,"setting")=="one"&&NF::value(b->core,"setting")=="six");
 const auto before=model.begins;p.delay(2);assert(model.begins==before); // actual cooperate is drain-only
 if(mode=="busy")model.busy=true;
 if(mode=="long")model.until=65537;
 if(mode=="cancel-maintenance")model.until=5;
 if(mode=="retained")model.retained=true;
 const unsigned oldDelays=delays;
 const bool safe=p.nativeOwnerCheckpoint(true);
 if(mode=="retained"){assert(!safe&&!L::exitSafe()&&!p.exitSafe()&&!p.storageSafe());return;}
 assert(safe&&delays==oldDelays);
 if(mode=="busy"){assert(!model.polls&&!L::retained()&&L::exitSafe());model.busy=false;assert(p.nativeOwnerCheckpoint(true));}
 assert(L::continuing()&&!L::discovering()&&!L::exitSafe()&&p.exitSafe()&&p.storageSafe());
 // Even an unleased sibling cannot publish a stale draft while a sweep runs.
 for(auto&s:services){risc_key_value_atomic_state_v1 v{};v.struct_size=sizeof(v);assert(s->endpoint().state(&v)==RISC_ATOMIC_BUSY);}
 if(mode=="shutdown-maintenance"){
  assert(p.nativeSettingsShutdown()&&L::exitSafe()&&!model.cancels&&model.polls==model.until);
  assert(delays==oldDelays+model.until-1);return;
 }
 while(L::continuing()){
  const unsigned old=model.polls;
  assert(p.nativeDiscoveryCheckpoint());assert(model.polls==old+1&&delays==oldDelays);
  if(L::continuing())for(auto&s:services){risc_key_value_atomic_state_v1 v{};v.struct_size=sizeof(v);assert(s->endpoint().state(&v)==RISC_ATOMIC_BUSY);}
 }
 assert(L::exitSafe()&&p.exitSafe()&&p.storageSafe());
 if(mode=="cancel-maintenance")assert(model.cancels==1&&model.polls==5&&L::timing().cancellationRequested);
 for(auto&s:services){risc_key_value_atomic_state_v1 v{};v.struct_size=sizeof(v);assert(!s->endpoint().state(&v));}
 const auto timing=L::timing();assert(timing.lastPolls==model.until&&!timing.lastBlockedMs);
 printf("bounded checkpoint timing: polls=%u synthetic_blocked_ms=%llu\n",timing.lastPolls,(unsigned long long)timing.lastBlockedMs);
 }}
int main(int argc,char**argv){assert(argc==3);mode=argv[1];NF::rootPath=argv[2];NF::empty();
 {NF::Fixture one(1),six(6);C::Value v1{"setting","one",3},v6{"setting","six",3};assert(!one.core.initialize(&v1,1,{}));one.store.finishOperation();if(mode!="missing-native"&&mode.rfind("preapp-",0)!=0){assert(!six.core.initialize(&v6,1,{}));six.store.finishOperation();}}
 if(mode=="corrupt-native")NF::nvs["rnr00000006"]["root"][0]^=1;
 if(mode=="initializing"){Bytes receipt(48,0);std::memcpy(receipt.data(),"RTENFI1",8);receipt[8]=6;receipt[12]=1;uint32_t crc=~0u;for(unsigned i=0;i<44;++i){crc^=receipt[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}crc=~crc;for(unsigned i=0;i<4;++i)receipt[44+i]=uint8_t(crc>>(8*i));NF::nvs["rnr00000006"]["root"]=receipt;}
 a=std::make_unique<NF::Fixture>(1);b=std::make_unique<NF::Fixture>(6);configure();
 admission.beginPreApp=[](void*p,RiscBoot::Runtime&,RiscCpu::Port&port,int one,int six){auto&m=*static_cast<Model*>(p);++m.preApps;assert(one==C::Ok&&six==(mode=="initializing"?C::Initializing:C::Missing));if(mode=="preapp-pending"||mode=="preapp-loaded"||mode=="preapp-runtime-retained"){m.port=&port;m.held=port.attached=true;return L::Progress::Pending;}return mode=="preapp-false-success"?L::Progress::Idle:L::Progress::Deferred;};
 if(mode=="missing-native")admission.beginPreApp=nullptr;
 admission.cancelRequested=[](void*p){auto&m=*static_cast<Model*>(p);return ((mode=="cancel-load"||mode=="cancel-load-complete")&&clockMs>=1&&!m.begins)||(mode=="cancel-maintenance"&&m.held&&m.polls>=1);};
 selected=mode!="missing-admission";failSecond=mode=="second-fail";
 if(mode=="wrong-namespace")admission.wifi=admission.preferences;
 if(mode=="external-control")deniedInternal=reinterpret_cast<const uint8_t*>(&model)+sizeof(model)-1;
 if(mode=="external-core")deniedInternal=&b->core;
 if(mode=="duplicate-writer"){static L::Writer duplicate[]={{"settings",1},{"settings",1}};admission.writers=duplicate;admission.writerCount=2;}
 NF::onWorker=false;NF::lease=false;const auto before=NF::nvs;
 setup();if(mode!="preapp-loaded")assert(NF::nvs==before); // startup only reads; no default repair or native writes
 const bool ran=std::find(calls.begin(),calls.end(),"run")!=calls.end();
 const bool expected=mode=="normal"||mode=="busy"||mode=="long"||mode=="retained"||mode=="cancel-maintenance"||mode=="shutdown-maintenance"||mode.rfind("preapp-",0)==0||mode=="initializing";
 assert(ran==expected);
 if(ran){assert(registered==1);const auto read=std::find(calls.begin(),calls.end(),"read1");assert(read<std::find(calls.begin(),calls.end(),"prepare"));const auto begins=model.begins,diagnostics=diagnosticsPolls;loop();assert(model.begins==begins);if(L::retained())assert(diagnosticsPolls==diagnostics);}
 else assert(!model.begins&&!L::ready());
 assert(model.preApps==((mode=="preapp-pending"||mode=="preapp-loaded"||mode=="preapp-runtime-retained")?1u:(mode.rfind("preapp-",0)==0||mode=="initializing")?2u:0u));
 if(mode=="cancel-load-complete")assert(a->core.ready()&&!b->core.ready()&&!model.preApps&&L::exitSafe());
 assert(!NF::lease||mode=="retained");printf("actual startup + checked load: %s PASS\n",mode.c_str());
 // Firmware objects are permanent. The deterministic shim has no running task;
 // process exit, rather than a fake target unregister, ends these test objects.
 std::fflush(stdout);std::_Exit(0);
}
'''

class NativeSettingsStartup(unittest.TestCase):
 def test_actual_entrypoint(self):
  with tempfile.TemporaryDirectory() as directory:
   folder=pathlib.Path(directory)
   (folder/'shim.h').write_text(SHIM)
   headers=('ports/esp32s3/NativeTlsMemory.h','esp_heap_caps.h','ports/esp32s3/NativeRadioResources.h','ports/esp32s3/NvsKeyValue.h','ports/esp32s3/NativeFailureEvidence.h','ports/esp32s3/NativeCompute.h','Arduino.h','RiscBuildIdentity.h','esp_spiffs.h','esp_ota_ops.h','esp_system.h','bootstrap/Runtime.h','ports/esp32s3/CpuPort.h','ports/esp32s3/NativeRetainedWake.h','ports/esp32s3/NativeRealtime.h','ports/esp32s3/NativeBoard.h','ports/esp32s3/CooperativeDelay.h','ports/esp32s3/SleepDiagnostics.h','esp_timer.h','diagnostics/Performance.h','CpuPort.h')
   for name in headers:
    path=folder/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('#include "shim.h"\n')
   for name in ('bootstrap/AppPolicyLimits.h','diagnostics/StageLog.h','ports/esp32s3/NativeDiagnosticBinding.h','ports/esp32s3/NativeCrownRecovery.h','ports/esp32s3/NativeSettingsLifecycle.h','runtime/drivers/NativeProviderPolicyV1.h','runtime/drivers/SelectedNativeProviderPolicyV1.h','runtime/packages/PackageIdentity.h'):
    path=folder/name;path.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'src'/name,path)
   shutil.copyfile(ROOT/'src/ports/esp32s3/NativeSettingsLifecycle.h',folder/'NativeSettingsLifecycle.h')
   shutil.copyfile(ROOT/'src/ports/esp32s3/NativeSettingsLifecycle.cpp',folder/'lifecycle.cpp')
   shutil.copyfile(ROOT/'src/main.cpp',folder/'main.cpp')
   (folder/'NativeSettingsWorker.h').write_text(WORKER)
   (folder/'harness.cpp').write_text('#include <algorithm>\n'+HARNESS)
   flags=['-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-function','-DESP_PLATFORM','-DBOARD_XTEINK_X4_PRO','-DRISC_NATIVE_SETTINGS_LIFECYCLE=1','-DRISC_NATIVE_SETTINGS_WORKER=1','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1','-DRISC_NATIVE_STORAGE_CUSTODY=1']
   if os.environ.get('SANITIZE')=='1':flags+=['-fsanitize='+os.environ.get('SANITIZERS','address,undefined'),'-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie']
   includes=[folder,ROOT,ROOT/'src',ROOT/'sdk/app',ROOT/'sdk/driver',ROOT/'sdk/hardware',ROOT/'test/native_nvs_shim',ROOT/'test/native_settings_worker_shim']
   command=['c++',*flags,*['-I'+str(p) for p in includes],str(folder/'main.cpp'),str(folder/'lifecycle.cpp'),str(folder/'harness.cpp'),str(ROOT/'src/runtime/storage/AppDataFiles.cpp'),'-Wl,--wrap=read,--wrap=write,--wrap=fsync,--wrap=close,--wrap=rename','-o',str(folder/'test')]
   subprocess.run(command,check=True)
   for mode in ('normal','busy','long','retained','missing-admission','missing-native','corrupt-native','wrong-namespace','duplicate-writer','second-fail','cancel-load','cancel-load-complete','cancel-maintenance','shutdown-maintenance','preapp-deferred','preapp-false-success','preapp-pending','preapp-loaded','preapp-runtime-retained','initializing','external-control','external-core'):
    subprocess.run([str(folder/'test'),mode,str(folder/mode)],check=True)
   # An absent native selector symbol must link and fail closed, not initialize.
   subprocess.run([command[0],'-DNO_ADMISSION',*command[1:]],check=True)
   subprocess.run([str(folder/'test'),'missing-admission',str(folder/'absent-symbol')],check=True)
   rejected=subprocess.run(['c++','-std=c++17','-DRISC_NATIVE_SETTINGS_LIFECYCLE=1','-DRISC_NATIVE_SETTINGS_WORKER=1','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1','-DRISC_NATIVE_STORAGE_CUSTODY=1','-I'+str(folder),'-x','c++','-fsyntax-only','-'],input='#include "NativeSettingsLifecycle.h"\n',text=True,capture_output=True)
   self.assertNotEqual(rejected.returncode,0);self.assertIn('X4-only',rejected.stderr)
if __name__=='__main__':unittest.main()
