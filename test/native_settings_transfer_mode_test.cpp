// Production Runtime/Settings lifecycle/coordinators with deterministic owner
// scheduling, checked host stores, and a modeled bounded maintenance client.
// A validated transfer HELLO fences new work while accepted maintenance drains.
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/CpuPort.h"
#include "ports/esp32s3/NativeSettingsLifecycle.h"
#include "ports/esp32s3/NativeSettingsWorker.h"
#include "ports/esp32s3/NativeSerialFiles.h"
#define NATIVE_FIXTURE_EXTERNAL_SOURCE 1
#include "native_replica_checked_fixture.h"
#include "RuntimeFileFixture.h"
#include <memory>

namespace F=NativeFixture;
namespace L=RiscCpu::NativeSettingsLifecycle;
namespace W=RiscCpu::NativeSettingsWorker;
namespace Serial=RiscCpu::NativeSerialFiles;
using Coordinator=RiscExperimental::NativeSettingsCoordinator;
static std::unique_ptr<Coordinator> services[2];
static unsigned workerNamespace,defaults,children,finis,readyChecks,controlChecks,serialChecks;
static unsigned settingsShutdowns,serialShutdowns;
static uint32_t nowMs;
static RiscBoot::Runtime* runtime;
static RiscCpu::Port* cpu;
static bool owner(){return !F::onWorker;}
static bool safe(){return true;}
bool esp_ptr_internal(const void*p){return p!=nullptr;}
extern "C" int64_t esp_timer_get_time(){return int64_t(nowMs)*1000;}
extern "C" bool xPortInIsrContext(){return false;}

namespace NativeFixture {
bool externalSourceDescriptor(int fd){
  char link[64],path[4096];
  snprintf(link,sizeof link,"/proc/self/fd/%d",fd);
  const auto n=readlink(link,path,sizeof(path)-1);assert(n>0);path[n]=0;
  return std::string(path).rfind(rootPath+"/",0)!=0;
}
}
namespace RiscCpu { namespace NativeSettingsWorker {
bool prepare(uint32_t ns,C&core,S&store,bool(*check)()){
  assert(check()&&!core.ready());const unsigned i=ns==6?1:0;
  services[i]=std::make_unique<Coordinator>(core,store,Coordinator::Hooks{
    reinterpret_cast<void*>(uintptr_t(ns)),
    [](void*){return owner();},
    [](void*p){return F::onWorker&&workerNamespace==uintptr_t(p);},
    [](void*){if(F::lease)return false;F::lease=true;return true;},
    [](void*){assert(F::lease);F::lease=false;},
    [](void*,const void*p,size_t n){return p&&n;}});
  return true;
}
Coordinator*coordinator(uint32_t ns){return services[ns==6?1:0].get();}
bool exitSafe(){for(auto&s:services)if(s&&!s->exitSafe())return false;return true;}
} }
static void schedule(uint32_t ms){
  assert(owner());nowMs+=ms;
  for(unsigned i=0;i<2;++i)if(services[i]){
    F::onWorker=true;workerNamespace=i?6:1;
    services[i]->workerStep();
    F::onWorker=false;workerNamespace=0;
  }
}
struct Maintenance {
  bool armed=false,held=false;
  unsigned begins=0,polls=0;
  static constexpr unsigned Steps=4;
} maintenance;
static bool appExitSafe(){
  return Serial::custodySafe()&&
    (L::continuing()?cpu->nativeDiscoveryAppExitSafe():cpu->appExitSafe())&&
    (L::continuing()||L::exitSafe());
}
static bool servicesReady(){++readyChecks;return Serial::exitSafe()&&L::exitSafe();}

extern "C" void test_atomic_app(unsigned hook,unsigned child){
  const auto*api=risc_runtime_get_api(1);assert(api);
  if(hook!=1){if(hook==2)++finis;assert(api->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_CONTEXT);return;}
  assert(Serial::exitSafe()&&!Serial::pending()&&!Serial::outputSize());
  if(child){++children;assert(L::exitSafe()&&!L::continuing());return;}
  if(++defaults!=1)return;
  assert(L::ready()&&L::exitSafe()&&api->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_OK);
  risc_runtime_capability_v1 grant{sizeof(grant)};
  assert(api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,1,&grant));
  maintenance.armed=true;
  assert(api->release(&grant)); // Runtime's real last-atomic-release checkpoint.
  assert(maintenance.begins==1&&!maintenance.polls&&L::continuing()&&!L::discovering());
  assert((L::diagnosticState()&1u)&&!L::exitSafe()&&Serial::exitSafe()&&appExitSafe());
  const auto nativeBefore=F::nvs;
  const auto callsBefore=F::nvsTotal+F::fsTotal;
  const char hello[]="RTEF1 12345678 1 HELLO\n";
  for(unsigned char c:std::string(hello))Serial::input(c);
  assert(api->service_checkpoint()==RISC_RUNTIME_CHECKPOINT_BUSY);
  assert(Serial::transferRequested()&&!Serial::transferActive()&&!Serial::outputSize());
  assert(api->exit_requested()&&runtime->active()&&!runtime->retained());
  assert(!api->request_launch("child.elf")&&!api->request_default());
  // If cleanup yield or fini starts a fresh maintenance sweep this rearmed
  // request exposes it. The already accepted operation must still finish.
  maintenance.armed=true;
  for(unsigned i=0;i<Maintenance::Steps+5;++i)api->yield_ms(1);
  assert(maintenance.begins==1&&maintenance.polls==Maintenance::Steps);
  assert(!maintenance.held&&!L::continuing()&&L::exitSafe());
  assert(F::nvs==nativeBefore&&F::nvsTotal+F::fsTotal==callsBefore);
  assert(!Serial::outputSize()&&!Serial::transferActive());
  puts("Transfer intent fenced fresh maintenance; actual accepted Settings custody drained; HELLO still withheld");
}

int main(int argc,char**argv){
  assert(argc==2);const std::string root=argv[1];F::rootPath=root+"/native-store";F::empty();
  {F::Fixture one(1),six(6);C::Value v1{"setting","one",3},v6{"setting","six",3};
    assert(!one.core.initialize(&v1,1,{}));one.store.finishOperation();
    assert(!six.core.initialize(&v6,1,{}));six.store.finishOperation();}
  auto one=std::make_unique<F::Fixture>(1),six=std::make_unique<F::Fixture>(6);
  F::onWorker=F::lease=false;
  static const L::Writer writers[]={{"settings-launch-test",1}};
  L::Admission admission{{&one->core,&one->store},{&six->core,&six->store},writers,1,
    &maintenance,sizeof maintenance,
    [](void*,Coordinator&a,Coordinator&b){return a.namespaceId()==1&&b.namespaceId()==6;},
    [](void*p,RiscBoot::Runtime&,RiscCpu::Port&){auto&m=*static_cast<Maintenance*>(p);
      if(!m.armed)return L::Progress::Idle;
      assert(!m.held);m.armed=false;m.held=true;++m.begins;return L::Progress::Pending;},
    [](void*p){auto&m=*static_cast<Maintenance*>(p);assert(m.held&&L::continuing()&&!L::exitSafe());
      if(++m.polls<Maintenance::Steps)return L::Progress::Pending;
      m.held=false;return L::Progress::Idle;},
    [](void*){assert(false);},
    [](void*p){return !static_cast<Maintenance*>(p)->held;},
    [](void*){return false;}};
  assert(L::prepare(&admission,{owner,schedule,[](){return nowMs;},nullptr}));
  RiscCpu::Hardware hardware{};hardware.owner=owner;hardware.now=[](){return uint64_t(nowMs);};hardware.sleep=schedule;
  auto port=std::make_unique<RiscCpu::Port>(hardware);cpu=port.get();
  RiscBoot::Port boot{owner,[](risc_runtime_health_v1*){return true;},schedule,[](const char*){return true;}};
  boot.appExitSafe=appExitSafe;
  boot.providerStorageSafe=[](){return !L::retained()&&(L::continuing()?cpu->nativeDiscoveryProviderSafe():cpu->providerStorageSafe());};
  boot.nativeOwnerCheckpoint=L::checkpoint;boot.nativeDiscoveryCheckpoint=L::discoveryCheckpoint;
  boot.nativeSettingsShutdown=[](){++settingsShutdowns;return L::shutdown();};
  boot.serialFilesReady=L::exitSafe;boot.sleepServicesReady=servicesReady;
  boot.serialTransferRequested=Serial::transferRequested;boot.serialTransferEnter=Serial::enterTransferMode;
  boot.serialFilesCheckpoint=[](){++serialChecks;return Serial::checkpoint();};
  boot.serialFilesControlCheckpoint=[](){++controlChecks;return Serial::controlCheckpoint();};
  boot.serialFilesShutdown=[](){++serialShutdowns;return Serial::shutdown();};boot.serialFilesStop=Serial::stop;
  RiscTest::FileSources files({root});
  const RiscSerial::Namespace serialNamespaces[]={{1,"Boot files",files.sources()->find(root.c_str()),false}};
  const Serial::Admission serialAdmission{serialNamespaces,1,L::exitSafe};
  assert(Serial::prepare(&serialAdmission,owner,safe));
  assert(Serial::enableTransferMode(safe));
  auto instance=std::make_unique<RiscBoot::Runtime>(boot);runtime=instance.get();
  assert(files.bind(*runtime)&&L::bind(*runtime)&&runtime->prepare(root.c_str()));
  assert(L::hydrate(*runtime,*cpu)&&L::attachOwner(*runtime,*cpu));
  assert(F::value(one->core,"setting")=="one"&&F::value(six->core,"setting")=="six");
  assert(runtime->run()&&!runtime->retained());
  assert(defaults==1&&!children&&finis==1&&settingsShutdowns==1);
  assert(maintenance.begins==1&&maintenance.polls==Maintenance::Steps&&L::exitSafe());
  assert(Serial::transferRequested()&&!Serial::transferActive()&&!Serial::outputSize());
  assert(runtime->nativeFilesServiceSafe()&&runtime->nativeFilesCheckpoint());
  assert(Serial::transferActive());
  std::string reply;while(Serial::outputSize()){reply.append(Serial::output(),Serial::outputSize());Serial::consumeOutput(Serial::outputSize());}
  assert(reply=="RTEF1 12345678 1 0 HELLO 48 511 1\n");
  Serial::disconnect();assert(Serial::transferActive()&&Serial::exitSafe());
  printf("Actual native Settings transfer fence PASS: accepted_polls=%u fresh_begins=0 child=0 cleanup=checked parked_hello=1\n",maintenance.polls);
  // Native clients are permanent; no artificial unregister or destruction.
  fflush(stdout);std::_Exit(0);
}
