#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
static const auto ownerThread=std::this_thread::get_id();
#define RADIO_STAGE_SAFE() assert(std::this_thread::get_id()==ownerThread)
#include "native_radio_async_sdk_fixture.inc"
#include "ports/esp32s3/NativeRadioAsyncTask.h"
#include "ports/esp32s3/NativeRadioResourceOwner.h"
#include "WifiApi.h"
extern "C" const risc_driver_v2* production_wifi_get(uint32_t);
#ifdef APP_WORKFLOW_OBJECT
extern "C" void app_workflow_start(const wifi_async_v1*);
extern "C" void app_workflow_scan(void);
extern "C" void app_workflow_blocked(void);
extern "C" void app_workflow_cleaned(void);
#ifdef APP_WORKFLOW_UNAVAILABLE
extern "C" void app_workflow_unavailable(void);
#endif
#endif
static_assert(offsetof(garden_radio_async_v1,base)==0 && offsetof(garden_radio_async_v1,async_tag)==sizeof(garden_radio_v1),"Garden prefix must be exact");
static_assert(offsetof(wifi_async_v1,base)==0 && offsetof(wifi_async_v1,async_tag)==sizeof(wifi_api_v1),"Wi-Fi prefix must be exact");
static_assert(sizeof(risc_radio_request_v1)==112 && sizeof(risc_radio_progress_v1)==636,"copied ABI bounds");
namespace Async=RiscCpu::NativeRadioAsync;
static RiscCpu::NativeRadioResourceOwner resourceOwner;
static bool taskCreateOk=false;static unsigned taskCreates=0;
BaseType_t xTaskCreate(void(*entry)(void*),const char* name,uint32_t bytes,void* argument,unsigned priority,TaskHandle_t* out){
 ++taskCreates;assert(entry==Async::task && !strcmp(name,"native-wifi") && bytes==8192 && !argument && priority==1);
 if(!taskCreateOk)return 0;
 *out=reinterpret_cast<void*>(uintptr_t(1));return pdPASS;
}
void vTaskDelay(TickType_t){assert(false);} // production infinite task is not invoked by this allocation shim
namespace RiscDiagnostics {uint64_t monotonicUs(){return uint64_t(now.load());}}
static std::mutex gateMutex;
static std::condition_variable gateCv;
static std::string gated;
static bool entered=false,released=false;
static void boundary(const char* name){
 assert(std::this_thread::get_id()!=ownerThread); // every SDK call is worker-only
 if(gated!=name)return;
 std::unique_lock<std::mutex> hold(gateMutex);
 if(entered)return;
 entered=true;gateCv.notify_all();gateCv.wait(hold,[]{return released;});
}
static void waitEntered(){std::unique_lock<std::mutex> hold(gateMutex);assert(gateCv.wait_for(hold,std::chrono::seconds(3),[]{return entered;}));}
static void unblock(){std::lock_guard<std::mutex> hold(gateMutex);released=true;gateCv.notify_all();}
static void runStep(){std::thread worker([](){Async::step();});worker.join();}
int main(int argc,char** argv){
 assert(argc==2);const std::string test=argv[1];
 using namespace RiscCpu;
 if(test=="workspace-allocation-failure"){
  taskCreateOk=true;workspaceFails=true;assert(!Async::provision() && !Async::api() && !Async::buffers && !taskCreates && !workspaceAllocation);
  risc_radio_request_v1 request{};request.struct_size=sizeof(request);request.kind=RISC_RADIO_REQUEST_SCAN;uint32_t id=9;
  assert(Async::begin(&request,&id)==RISC_RADIO_UNAVAILABLE && !id && calls.empty());
  workspaceFails=false;assert(Async::provision() && Async::api() && Async::buffers && taskCreates==1 && workspaceAllocations==2 && !workspaceFrees);
  assert(Async::provision() && taskCreates==1 && workspaceAllocations==2);puts("Native mailbox allocation failure and one lifetime allocation PASS");return 0;
 }
 if(test=="startup-failure"){
  assert(!Async::provision() && !Async::api() && !Async::workerTask && taskCreates==1 && !Async::buffers && !workspaceAllocation && workspaceFrees==1);
  risc_radio_request_v1 request{};request.struct_size=sizeof(request);request.kind=RISC_RADIO_REQUEST_SCAN;uint32_t id=9;
  assert(Async::begin(&request,&id)==RISC_RADIO_UNAVAILABLE && !id && calls.empty());
  taskCreateOk=true;assert(Async::provision() && Async::api() && taskCreates==2);
  assert(Async::provision() && taskCreates==2);puts("Native worker startup failure/single lifetime allocation PASS");return 0;
 }
 Hardware h{};h.owner=[](){return std::this_thread::get_id()==ownerThread;};
 h.radioJoin=Async::join;h.radioState=Async::state;h.radioLeave=Async::leave;
 h.radioAddresses=Async::addresses;h.radioScanStart=Async::scanStart;h.radioScanPoll=Async::scanPoll;h.radioScanCancel=Async::scanCancel;h.radioIdle=Async::idle;
 assert(Async::allocateBuffers() && Async::provisioned());resourceOwner.configure(h.owner,Async::tryShared,Async::endShared,Async::sharedPhaseReady,Async::sharedReady);
 auto nativeTable=*Async::api();nativeTable.tryShared=[](){return resourceOwner.enter();};nativeTable.endShared=[](){resourceOwner.leave();};h.radioAsync=&nativeTable;
 h.hciSafe=[](){return true;};h.hciIdle=[](){return true;};h.hciOpen=[](){assert(false);return false;};h.hciClose=[](){assert(false);return false;};
 h.radioIqReady=[](){return true;};h.radioIqPrepare=[](){return true;};h.radioIqCleanup=[](){return true;};
 Port port(h);auto& c=port.radios_[0];c.port=&port;port.iq_.port=&port;port.hci_.port=&port;
 garden_radio_async_v1 native={{1,sizeof(native),&c,Port::radioClaim,Port::radioJoin,Port::radioState,Port::radioLeave,Port::radioRelease,Port::radioStartAp,Port::radioStopAp,Port::radioAddresses,Port::radioScanStart,Port::radioScanPoll,Port::radioScanCancel},RISC_RADIO_ASYNC_TAG,RISC_RADIO_ASYNC_VERSION,Port::radioAsyncBegin,Port::radioAsyncPoll,Port::radioAsyncCancel,Port::radioServiceBegin,Port::radioServiceEnd};
 const risc_hw_radio_v1 config={sizeof(config),0,1};
 const risc_hardware_device_v1 device={1,sizeof(device),15,"espressif,esp32s3-wifi","unspecified","radio.integrated",1,sizeof(config),&config};
 const risc_provider_dependency_v1 deps[]={{"hardware.device",1,&device},{"platform.radio",1,&native}};
 const bool legacyOnly=test.rfind("suffix:",0)==0;
 if(test=="suffix:size")native.base.struct_size=sizeof(garden_radio_v1);
 if(test=="suffix:tag")native.async_tag=0;
 if(test=="suffix:version")native.async_version=2;
 if(test=="suffix:callback")native.cancel=nullptr;
 if(test=="suffix:service")native.service_begin=nullptr;
 const auto* driver=production_wifi_get(2);assert(driver && driver->start(deps,2));
 const auto* api=static_cast<const wifi_async_v1*>(driver->capability);
 if(legacyOnly){assert(api->base.struct_size==sizeof(wifi_api_v1));assert(driver->quiesce());driver->stop();puts("Legacy fallback rejects malformed async suffix PASS");return 0;}
 assert(api->base.struct_size==sizeof(*api));
 risc_radio_request_v1 request{};request.struct_size=sizeof(request);request.kind=RISC_RADIO_REQUEST_SCAN;
 risc_radio_progress_v1 progress{};progress.struct_size=sizeof(progress);uint32_t id=0;
 auto poll=[&](){progress.struct_size=sizeof(progress);return api->poll(nullptr,id,&progress);};
 auto begin=[&](){assert(api->begin(nullptr,&request,&id)==RISC_RADIO_ACCEPTED && id);assert(c.operation==id && c.active && !c.closing);};
 auto close=[&](){assert(api->cancel(nullptr,id)==RISC_RADIO_PENDING);runStep();assert(poll()==RISC_RADIO_QUIESCENT && progress.phase==RISC_RADIO_IDLE && progress.quiescent && !progress.owned);assert(port.appExitSafe() && port.providerStorageSafe());};
 auto pending=[&](){
  const auto before=std::chrono::steady_clock::now();
  for(unsigned tick=0;tick<100;++tick){
   assert(poll()==RISC_RADIO_PENDING);
   assert(api->cancel(nullptr,id)==RISC_RADIO_PENDING);
   assert(!Async::idle() && !port.appExitSafe() && port.providerStorageSafe());
   assert(!port.radioSharedReady() && !c.closing && c.active);
   assert(!resourceOwner.enter());
   uint32_t serviceToken=99;assert(api->service_begin(nullptr,&serviceToken)==RISC_RADIO_BUSY && !serviceToken);
   uint64_t hciToken=99;assert(!Port::hciOpen(&port.hci_,0,&hciToken) && !hciToken && !port.hci_.token && !port.hci_.closing);
   uint32_t replacement=44;assert(api->begin(nullptr,&request,&replacement)==RISC_RADIO_BUSY && !replacement);
   assert(api->cancel(nullptr,id+1)==RISC_RADIO_STALE);
   assert(!driver->quiesce()); // no unload/release while SDK is stalled
   uint64_t iq=0;assert(!Port::radioIqClaim(&port.iq_,&iq) && !iq);
   assert(!Port::radioRelease(&c,c.token));
   auto& gpio=port.gpios_[0];gpio.port=&port;port.pins_[4]={&gpio,90,false};risc_light_sleep_result_v1 wake{sizeof(wake),0};
   assert(Port::gpioLightSleep(&gpio,90,false,&wake)==RISC_LIGHT_SLEEP_BUSY);
   assert(Port::gpioDeepSleep(&gpio,90,false)==RISC_DEEP_SLEEP_BUSY);port.pins_[4]={};
  }
  assert(std::chrono::steady_clock::now()-before<std::chrono::seconds(1));
 };
 sdkBoundary=boundary;
#ifdef APP_WORKFLOW_OBJECT
#ifdef APP_WORKFLOW_UNAVAILABLE
 if(test=="app-workflow-unavailable"){
  app_workflow_start(api);c.asyncUnavailable=true;Async::ready=false;
  app_workflow_unavailable();
  assert(calls.empty() && !c.operation && !c.active && !c.closing && port.appExitSafe());
 }else
#endif
 if(test=="app-workflow"){
  app_workflow_start(api);app_workflow_scan();
  assert(c.operation && c.active && !c.closing);id=c.operation;
  gated="wifi_init";std::thread worker([](){Async::step();});waitEntered();
  const auto before=std::chrono::steady_clock::now();app_workflow_blocked();
  assert(std::chrono::steady_clock::now()-before<std::chrono::seconds(1));
  assert(c.operation==id && c.active && c.token && !c.closing);
  assert(!port.appExitSafe() && port.providerStorageSafe() && !Async::idle());
  assert(poll()==RISC_RADIO_PENDING && progress.phase==RISC_RADIO_STOPPING);
  unblock();worker.join();app_workflow_cleaned();
  assert(Async::idle() && !c.operation && !c.active && !c.closing && port.appExitSafe());
 }else
#endif
 if(test=="startup-unavailable"){
  c.asyncUnavailable=true;Async::ready=false;
  assert(api->base.struct_size==sizeof(*api));
  assert(api->begin(nullptr,&request,&id)==RISC_RADIO_UNAVAILABLE && !id);
  assert(calls.empty() && !c.active && !c.operation && !c.closing && port.appExitSafe());
 }else if(test=="invalid"){
  request.flags=1;assert(api->begin(nullptr,&request,&id)==RISC_RADIO_INVALID && !id);request.flags=0;
  request.struct_size--;assert(api->begin(nullptr,&request,&id)==RISC_RADIO_INVALID && !id);request.struct_size++;
  Async::ready=false;assert(api->begin(nullptr,&request,&id)==RISC_RADIO_UNAVAILABLE && !id);Async::ready=true;
  Async::nextId=UINT32_MAX;assert(api->begin(nullptr,&request,&id)==RISC_RADIO_UNAVAILABLE && !id);
 }else if(test=="before-dequeue"){
  begin();assert(api->cancel(nullptr,id)==RISC_RADIO_PENDING);runStep();assert(calls.empty());assert(poll()==RISC_RADIO_QUIESCENT);
 }else if(test=="repeated"){
  for(unsigned n=0;n<40;++n){begin();runStep();assert(poll()==RISC_RADIO_PENDING && progress.phase==RISC_RADIO_SCANNING);close();}
  assert(scanAllocations==40 && scanFrees==40);
 }else if(test=="concurrent"){
  std::atomic<bool> stop{false};
  std::thread worker([&](){while(!stop.load(std::memory_order_acquire)){Async::step();std::this_thread::yield();}});
  for(unsigned n=0;n<100;++n){
    begin();
    // Owner snapshots/cancellation race real worker startup/publication.
    for(unsigned tick=0;tick<16;++tick){const auto result=poll();assert(result==RISC_RADIO_PENDING || result==RISC_RADIO_AGAIN);}
    auto result=api->cancel(nullptr,id);assert(result==RISC_RADIO_PENDING || result==RISC_RADIO_QUIESCENT);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    do{result=poll();assert(result==RISC_RADIO_PENDING || result==RISC_RADIO_AGAIN || result==RISC_RADIO_QUIESCENT);assert(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}while(result!=RISC_RADIO_QUIESCENT);
    assert(port.appExitSafe() && port.providerStorageSafe());
    if(n%10==0){sdkBoundary=nullptr;assert(api->base.connect(nullptr,"Synthetic AP","synthetic-password"));assert(api->base.disconnect_checked(nullptr));sdkBoundary=boundary;}
  }
  stop.store(true,std::memory_order_release);worker.join();
 }else if(test=="terminal-contention"){
  begin();runStep();assert(api->cancel(nullptr,id)==RISC_RADIO_PENDING);
  assert(Async::lock());runStep();assert(!Async::finishedId.load() && !Async::idle());Async::unlock();
  assert(api->cancel(nullptr,id)==RISC_RADIO_PENDING);runStep();
  assert(Async::finishedId.load()==id);assert(poll()==RISC_RADIO_QUIESCENT);
  assert(api->cancel(nullptr,id)==RISC_RADIO_QUIESCENT);const uint32_t previous=id;
  begin();assert(id>previous);assert(api->cancel(nullptr,previous)==RISC_RADIO_STALE);runStep();close();
 }else if(test=="service-lease"){
  uint32_t lease=0;assert(api->service_begin(nullptr,&lease)==RISC_RADIO_ACCEPTED && lease);
  assert(resourceOwner.heldReady() && !port.appExitSafe() && port.providerStorageSafe());
  assert(!driver->quiesce() && !Port::radioRelease(&c,c.token));
  assert(api->service_end(nullptr,lease+1)==RISC_RADIO_STALE && resourceOwner.heldReady());
  assert(resourceOwner.enter()); // nested native NVS/app-data guard
  resourceOwner.leave();assert(resourceOwner.heldReady());
  begin();runStep();assert(calls.empty());
  assert(api->service_end(nullptr,lease)==RISC_RADIO_ACCEPTED);
  assert(api->service_end(nullptr,lease)==RISC_RADIO_STALE);
  runStep();assert(poll()==RISC_RADIO_PENDING);
  assert(api->service_begin(nullptr,&lease)==RISC_RADIO_ACCEPTED);
  assert(resourceOwner.enter());resourceOwner.leave();
  const auto callCount=calls.size();assert(api->cancel(nullptr,id)==RISC_RADIO_PENDING);runStep();assert(calls.size()==callCount);
  assert(api->service_end(nullptr,lease)==RISC_RADIO_ACCEPTED);runStep();assert(poll()==RISC_RADIO_QUIESCENT);
 }else if(test=="resource-lease"){
  assert(resourceOwner.enter() && resourceOwner.enter() && resourceOwner.heldReady());
  bool nonowner=true;std::thread stranger([&](){nonowner=resourceOwner.enter();resourceOwner.leave();});stranger.join();assert(!nonowner);
  begin();runStep();assert(calls.empty());resourceOwner.leave();runStep();assert(calls.empty());resourceOwner.leave();runStep();assert(!calls.empty());
  assert(resourceOwner.enter());const auto count=calls.size();
  assert(api->cancel(nullptr,id)==RISC_RADIO_PENDING);runStep();assert(calls.size()==count && !Async::idle());
  resourceOwner.leave();runStep();assert(poll()==RISC_RADIO_QUIESCENT);
 }else if(test=="legacy"){
  begin();runStep();assert(!api->base.connect(nullptr,"Synthetic AP","synthetic-password"));close();
  sdkBoundary=nullptr;
  assert(api->base.connect(nullptr,"Synthetic AP","synthetic-password"));
  uint32_t other=0;assert(api->begin(nullptr,&request,&other)==RISC_RADIO_BUSY && !other);
  assert(api->base.disconnect_checked(nullptr));
  sdkBoundary=boundary;begin();runStep();close();
 }else if(test=="copied"){
  request.kind=RISC_RADIO_REQUEST_JOIN;strcpy(request.ssid,"Synthetic AP");strcpy(request.password,"synthetic-password");begin();memset(&request,0,sizeof(request));runStep();
  assert(!strcmp(reinterpret_cast<char*>(copied.sta.ssid),"Synthetic AP"));
  assert(!strcmp(reinterpret_cast<char*>(copied.sta.password),"synthetic-password"));
  assert(zero(&Async::buffers->request,sizeof(Async::buffers->request)) && credentialWipeObserved);close();
  assert(zero(&copied,sizeof(copied)) && zero(&Async::buffers->snapshot,sizeof(Async::buffers->snapshot)) && zero(&Async::buffers->working,sizeof(Async::buffers->working)));
 }else if(test=="contention"){
  assert(Async::lock());assert(api->begin(nullptr,&request,&id)==RISC_RADIO_AGAIN && !id);Async::unlock();begin();
  assert(Async::lock());assert(poll()==RISC_RADIO_AGAIN);assert(api->cancel(nullptr,id)==RISC_RADIO_PENDING);Async::unlock();runStep();assert(poll()==RISC_RADIO_QUIESCENT);
 }else if(test=="allocation"){
  allocationFails=true;begin();runStep();assert(poll()==RISC_RADIO_QUIESCENT && progress.failure==RISC_RADIO_FAILURE_SETUP);
 }else if(test=="complete-cancel"){
  startEmitsDone=true;found.resize(1);memcpy(found[0].ssid,"Synthetic AP",13);found[0].primary=6;found[0].authmode=WIFI_AUTH_OPEN;
  begin();runStep();assert(poll()==RISC_RADIO_PENDING && progress.phase==RISC_RADIO_RESULTS && progress.scan.count==1);
  assert(api->cancel(nullptr,id)==RISC_RADIO_PENDING);assert(poll()==RISC_RADIO_PENDING && progress.phase==RISC_RADIO_STOPPING && !progress.scan.count);runStep();assert(poll()==RISC_RADIO_QUIESCENT);
 }else if(test.rfind("setup:",0)==0 || test.rfind("cleanup:",0)==0 || test.rfind("failure:",0)==0){
  const bool cleanupCase=test.rfind("cleanup:",0)==0,failureCase=test.rfind("failure:",0)==0;
  const std::string name=test.substr(test.find(':')+1);
  const bool joinCase=name=="credentials_copy" || name=="connect" || name=="disconnect" || name=="credentials_clear";
  if(joinCase){request.kind=RISC_RADIO_REQUEST_JOIN;strcpy(request.ssid,"Synthetic AP");strcpy(request.password,"synthetic-password");}
  if(name=="records"){startEmitsDone=true;found.resize(1);}
  if(cleanupCase || failureCase){begin();runStep();assert(poll()==RISC_RADIO_PENDING);assert(api->cancel(nullptr,id)==RISC_RADIO_PENDING);}
  if(failureCase)failure=name;
  gated=name;
  if(!cleanupCase && !failureCase)begin();
  std::thread worker([](){Async::step();});waitEntered();pending();unblock();worker.join();
  const auto result=poll();
  if(failureCase){assert(result==RISC_RADIO_RETAINED && progress.phase==RISC_RADIO_CLEANUP_FAILED && progress.owned && !progress.quiescent);assert(!port.providerStorageSafe() && !port.appExitSafe());const auto count=calls.size();runStep();assert(calls.size()==count);assert(api->cancel(nullptr,id)==RISC_RADIO_RETAINED);}
  else {assert(result==RISC_RADIO_QUIESCENT && progress.phase==RISC_RADIO_IDLE && progress.quiescent);assert(Async::idle() && port.appExitSafe());
   if(!cleanupCase && name!="scan_start" && name!="records")assert(!called("scan_start"));
   if(!cleanupCase && name!="connect")assert(!called("connect"));
  }
 }else assert(false);
 if(!c.closing){assert(driver->quiesce());driver->stop();assert(port.quiescent());}
 for(unsigned n=0;n<10;++n)Async::drain();
 for(const auto& line:stageLines){assert(line.find("Synthetic")==std::string::npos && line.find("synthetic-password")==std::string::npos);}
 printf("Production provider + CpuPort + native async worker: %s PASS\n",test.c_str());
}
