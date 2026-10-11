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
// Reuse the existing complete Wi-Fi SDK fixture with a distinct allocator.
#define heap_caps_calloc radioHeapCalloc
#define heap_caps_free radioHeapFree
#include "native_radio_async_sdk_fixture.inc"
#undef heap_caps_calloc
#undef heap_caps_free
void* heap_caps_calloc(size_t,size_t,uint32_t);
void heap_caps_free(void*);
#undef ESP_OK
#undef ESP_FAIL
#include "ports/esp32s3/NativeHci.h"
#include "ports/esp32s3/NativeRadioAsync.h"
#include "ports/esp32s3/NativeRadioResourceOwner.h"
using namespace RiscCpu;
namespace Async=NativeRadioAsync;
static NativeRadioResourceOwner resourceOwner;
static esp_bt_controller_status_t btStatus=ESP_BT_CONTROLLER_STATUS_IDLE;
static const esp_vhci_host_callback_t* btCallbacks=nullptr;
static std::string btFailure,btFailure2;
static unsigned btCalls=0,btAllocations=0,btFrees=0,leaseTakes=0,leaseEnds=0;
static bool probeWorker=true,requireLease=false;
static std::mutex gateMutex;
static std::condition_variable gateCv;
static std::string gated;
static bool entered=false,released=false;
static void runStep(){std::thread worker([](){Async::step();});worker.join();}
static void hciBoundary(){
 assert(std::this_thread::get_id()==ownerThread);
 ++btCalls;
 if(requireLease){
  assert(Async::sharedLease.load()==2 && resourceOwner.heldReady());
  if(probeWorker){const auto count=calls.size();runStep();assert(calls.size()==count);}
 }
}
static void wifiBoundary(const char* name){
 assert(std::this_thread::get_id()!=ownerThread);
 assert(Async::sharedLease.load()==1);
 if(gated!=name)return;
 std::unique_lock<std::mutex> hold(gateMutex);
 if(entered)return;
 entered=true;gateCv.notify_all();gateCv.wait(hold,[]{return released;});
}
static void waitEntered(){std::unique_lock<std::mutex> hold(gateMutex);assert(gateCv.wait_for(hold,std::chrono::seconds(3),[]{return entered;}));}
static void unblock(){std::lock_guard<std::mutex> hold(gateMutex);released=true;gateCv.notify_all();}
void enterCritical(portMUX_TYPE* p){assert(!*p);*p=1;}
void exitCritical(portMUX_TYPE* p){assert(*p==1);*p=0;}
void* heap_caps_calloc(size_t count,size_t size,uint32_t flags){
 if(size!=sizeof(NativeHci::Buffers))return radioHeapCalloc(count,size,flags);
 hciBoundary();assert(count==1 && flags==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
 if(btFailure=="alloc")return nullptr;
 ++btAllocations;return std::calloc(count,size);
}
void heap_caps_free(void* p){
 if(p==workspaceAllocation || p==scanAllocation){radioHeapFree(p);return;}
 hciBoundary();assert(p && zero(p,sizeof(NativeHci::Buffers)));++btFrees;std::free(p);
}
void vTaskDelay(unsigned n){assert(n==1 && !NativeHci::mux);now.fetch_add(1000);}
esp_bt_controller_status_t esp_bt_controller_get_status(){if(requireLease)hciBoundary();return btStatus;}
static esp_err_t btResult(const char* stage){hciBoundary();return btFailure==stage || btFailure2==stage?ESP_FAIL:ESP_OK;}
esp_err_t esp_bt_controller_init(esp_bt_controller_config_t* c){
 assert(btStatus==ESP_BT_CONTROLLER_STATUS_IDLE && c->bluetooth_mode==ESP_BT_MODE_BLE && c->sleep_mode==ESP_BT_SLEEP_MODE_NONE);
 if(btResult("init")!=ESP_OK)return ESP_FAIL;
 btStatus=ESP_BT_CONTROLLER_STATUS_INITED;return ESP_OK;
}
esp_err_t esp_bt_controller_enable(esp_bt_mode_t mode){
 assert(btStatus==ESP_BT_CONTROLLER_STATUS_INITED && mode==ESP_BT_MODE_BLE);
 if(btResult("enable")!=ESP_OK)return ESP_FAIL;
 btStatus=ESP_BT_CONTROLLER_STATUS_ENABLED;return ESP_OK;
}
esp_err_t esp_bt_controller_disable(){
 assert(btStatus==ESP_BT_CONTROLLER_STATUS_ENABLED);
 if(btResult("disable")!=ESP_OK)return ESP_FAIL;
 btStatus=ESP_BT_CONTROLLER_STATUS_INITED;return ESP_OK;
}
esp_err_t esp_bt_controller_deinit(){
 assert(btStatus==ESP_BT_CONTROLLER_STATUS_INITED);
 if(btResult("deinit")!=ESP_OK)return ESP_FAIL;
 btStatus=ESP_BT_CONTROLLER_STATUS_IDLE;return ESP_OK;
}
esp_err_t esp_vhci_host_register_callback(const esp_vhci_host_callback_t* cb){btCallbacks=cb;return btResult("register");}
bool esp_vhci_host_check_send_available(){return true;}
void esp_vhci_host_send_packet(uint8_t* data,uint16_t size){assert(data && size);}
namespace RiscDiagnostics {uint64_t monotonicUs(){return uint64_t(now.load());}}
int main(int argc,char** argv){
 assert(argc==2);const std::string test=argv[1];
 Hardware h{};h.owner=[](){return std::this_thread::get_id()==ownerThread;};
 h.radioIdle=Async::idle;h.radioState=Async::state;h.radioLeave=Async::leave;
 h.hciOpen=NativeHci::open;h.hciClose=NativeHci::close;h.hciIdle=NativeHci::idle;h.hciSafe=NativeHci::safe;
 h.hciSend=NativeHci::send;h.hciReceive=NativeHci::receive;
 h.radioIqReady=[](){return true;};h.radioIqPrepare=[](){return true;};h.radioIqCleanup=[](){return true;};
 assert(Async::allocateBuffers() && Async::provisioned());
 resourceOwner.configure(h.owner,Async::tryShared,Async::endShared,Async::sharedPhaseReady,Async::sharedReady);
 auto table=*Async::api();table.tryShared=[](){++leaseTakes;return resourceOwner.enter();};
 table.endShared=[](){++leaseEnds;resourceOwner.leave();};table.sharedReady=[](){return resourceOwner.ready();};h.radioAsync=&table;
 Port port(h);auto& radio=port.radios_[0];radio.port=&port;auto& hci=port.hci_;hci.port=&port;port.iq_.port=&port;
 assert(Port::radioClaim(&radio,&radio.token));
 risc_radio_request_v1 request{};request.struct_size=sizeof(request);request.kind=RISC_RADIO_REQUEST_JOIN;
 strcpy(request.ssid,"Synthetic AP");strcpy(request.password,"synthetic-password");
 uint32_t operation=0;uint64_t hciToken=0;
 auto begin=[&](){assert(Port::radioAsyncBegin(&radio,radio.token,&request,&operation)==RISC_RADIO_ACCEPTED && operation);};
 auto poll=[&](uint32_t expected){risc_radio_progress_v1 progress{};progress.struct_size=sizeof(progress);const auto r=Port::radioAsyncPoll(&radio,radio.token,operation,&progress);assert(progress.phase==expected);return r;};
 auto cancel=[&](){assert(Port::radioAsyncCancel(&radio,radio.token,operation)==RISC_RADIO_PENDING);};
 auto finish=[&](){cancel();runStep();assert(poll(RISC_RADIO_IDLE)==RISC_RADIO_QUIESCENT);};
 auto open=[&](){requireLease=true;const bool result=Port::hciOpen(&hci,0,&hciToken);requireLease=false;return result;};
 auto close=[&](){requireLease=true;const bool result=Port::hciClose(&hci,hciToken);requireLease=false;return result;};
 auto refused=[&](){const auto before=btCalls;uint64_t token=99;assert(!Port::hciOpen(&hci,0,&token) && !token && btCalls==before);};
 auto connect=[&](){runStep();assert(poll(RISC_RADIO_JOINING)==RISC_RADIO_PENDING);associated=true;netif->up=true;ip.ip.addr=0x01020304;runStep();assert(poll(RISC_RADIO_CONNECTED)==RISC_RADIO_PENDING);};
 sdkBoundary=wifiBoundary;
 if(test=="idle-iq-owner"){
  uint64_t iq=0;assert(Port::radioIqClaim(&port.iq_,&iq) && iq);refused();assert(!leaseTakes);assert(Port::radioIqRelease(&port.iq_,iq));
  const auto takes=leaseTakes;std::thread stranger([&](){refused();});stranger.join();assert(leaseTakes==takes);
  assert(open());const auto token=hciToken;const auto before=btCalls;
  assert(!Port::hciClose(&hci,token+1) && btCalls==before);
  std::thread other([&](){assert(!Port::hciClose(&hci,token));});other.join();assert(btCalls==before);
  assert(!Port::radioIqClaim(&port.iq_,&iq) && !iq);assert(close());
  assert(Port::radioIqClaim(&port.iq_,&iq));assert(Port::radioIqRelease(&port.iq_,iq));
 }else if(test=="token-exhausted"){
  port.serial_=UINT64_MAX;refused();assert(leaseTakes==1 && leaseEnds==1 && !Async::sharedLease.load());
 }else if(test=="malformed-native-table"){
  const auto take=table.tryShared;const auto end=table.endShared;
  table.tryShared=nullptr;refused();assert(!leaseTakes && !leaseEnds);
  table.tryShared=take;table.endShared=nullptr;refused();assert(!leaseTakes && !leaseEnds);
  table.endShared=end;table.sharedReady=nullptr;refused();assert(leaseTakes==1 && leaseEnds==1);
  table.sharedReady=[](){return resourceOwner.ready();};assert(open() && close());
 }else if(test=="queued-nested"){
  uint32_t service=0;assert(Port::radioServiceBegin(&radio,radio.token,&service)==RISC_RADIO_ACCEPTED);
  assert(open());begin();const auto before=btCalls;assert(!close() && btCalls==before && hci.token && !hci.closing);
  assert(Port::radioServiceEnd(&radio,radio.token,service)==RISC_RADIO_ACCEPTED);
  runStep();assert(close());finish();
  assert(Port::radioServiceBegin(&radio,radio.token,&service)==RISC_RADIO_ACCEPTED);
  begin();refused();runStep();assert(calls.size()>0 && Async::phase.load()==RISC_RADIO_QUEUED);
  assert(Port::radioServiceEnd(&radio,radio.token,service)==RISC_RADIO_ACCEPTED);runStep();finish();
 }else if(test=="connected-nested"){
  begin();connect();uint32_t service=0;assert(Port::radioServiceBegin(&radio,radio.token,&service)==RISC_RADIO_ACCEPTED);
  assert(!Async::sharedReady() && resourceOwner.ready());assert(open() && close() && resourceOwner.heldReady());
  assert(Port::radioServiceEnd(&radio,radio.token,service)==RISC_RADIO_ACCEPTED);finish();
 }else if(test=="phases"){
  begin();refused();runStep();assert(poll(RISC_RADIO_JOINING)==RISC_RADIO_PENDING);assert(open() && close());
  associated=true;netif->up=true;ip.ip.addr=0x01020304;runStep();assert(poll(RISC_RADIO_CONNECTED)==RISC_RADIO_PENDING);assert(open() && close());finish();
  request={};request.struct_size=sizeof(request);request.kind=RISC_RADIO_REQUEST_SCAN;begin();runStep();
  assert(poll(RISC_RADIO_SCANNING)==RISC_RADIO_PENDING);assert(open() && close());emitScan();runStep();
  assert(poll(RISC_RADIO_RESULTS)==RISC_RADIO_PENDING);assert(open() && close());finish();
 }else if(test=="cancel-nested"){
  begin();connect();assert(open());uint32_t service=0;assert(Port::radioServiceBegin(&radio,radio.token,&service)==RISC_RADIO_ACCEPTED);
  cancel();const auto before=btCalls;assert(!close() && btCalls==before && !hci.closing && hci.token);
  assert(Port::radioServiceEnd(&radio,radio.token,service)==RISC_RADIO_ACCEPTED);runStep();assert(poll(RISC_RADIO_IDLE)==RISC_RADIO_QUIESCENT);assert(close());
 }else if(test.rfind("worker-",0)==0){
  const bool opening=test.find("-open")!=std::string::npos;
  const bool starting=test.find("-start")!=std::string::npos,stopping=test.find("-stop")!=std::string::npos;
  if(!opening)assert(open());
  begin();
  if(!starting)connect();
  if(stopping)cancel();
  gated=starting?"wifi_init":stopping?"stop":"ap_info";
  std::thread worker([](){Async::step();});waitEntered();const auto before=btCalls;
  if(opening)refused();else assert(!close() && btCalls==before && hci.token && !hci.closing);
  unblock();worker.join();gated.clear();
  if(stopping)assert(poll(RISC_RADIO_IDLE)==RISC_RADIO_QUIESCENT);
  if(opening)assert(open());
  assert(close());
  if(!stopping)finish();
 }else if(test=="retained-wifi"){
  begin();connect();assert(open());failure="stop";cancel();runStep();assert(poll(RISC_RADIO_CLEANUP_FAILED)==RISC_RADIO_RETAINED);
  const auto before=btCalls;assert(!close() && btCalls==before && hci.token && !hci.closing && !port.providerStorageSafe());
  puts("CpuPort + native HCI + native Wi-Fi worker: retained-wifi PASS");return 0;
 }else if(test.rfind("open:",0)==0 || test.rfind("rollback:",0)==0 || test.rfind("close:",0)==0){
  begin();connect();
  if(test.rfind("close:",0)==0){assert(open());btFailure=test.substr(6);assert(!close() && hci.token && hci.closing);}
  else {btFailure=test.substr(test.find(':')+1);if(test.rfind("rollback:",0)==0){btFailure2=btFailure;btFailure="register";}
   assert(!open());
  }
  assert(Async::sharedLease.load()==0);const auto before=calls.size();runStep();assert(calls.size()>before);
  if(btFailure=="init"){
   assert(hci.token && hci.closing && NativeHci::initUncertain && !port.providerStorageSafe());
   btFailure.clear();const auto previous=btCalls;assert(!close() && !close() && btCalls==previous && hci.token && hci.closing);
   puts("CpuPort + native HCI + native Wi-Fi worker: uncertain init retained/retry PASS");return 0;
  }
  btFailure.clear();btFailure2.clear();if(hci.token){assert(hci.closing && !port.providerStorageSafe());assert(close());}
  assert(!hci.token && !hci.closing && port.providerStorageSafe());finish();
 }else if(test=="concurrent"){
  begin();connect();probeWorker=false;std::atomic<bool> stop{false};
  std::thread worker([&](){while(!stop.load()){Async::step();std::this_thread::yield();}});
  for(unsigned cycle=0;cycle<100;++cycle){
   const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
   while(!open()){assert(!hci.token && !hci.closing);assert(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}
   while(!close()){assert(hci.token && !hci.closing);assert(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}
  }
  stop.store(true);worker.join();finish();
 }else assert(false);
 assert(!hci.token && NativeHci::idle() && NativeHci::safe() && !Async::sharedLease.load());
 assert(btAllocations==btFrees);assert(Port::radioRelease(&radio,radio.token));assert(port.quiescent());
 printf("CpuPort + native HCI + native Wi-Fi worker: %s PASS\n",test.c_str());
}
