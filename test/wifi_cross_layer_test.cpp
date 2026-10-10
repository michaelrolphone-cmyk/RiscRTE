/* SDK-only deterministic boundaries: real app/adapter/provider/CPU/native code. */
#include <memory>
#include "bootstrap/Json.h"
#define private public
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <esp_bt.h>
#define heap_caps_calloc wifi_heap_caps_calloc
#define heap_caps_free wifi_heap_caps_free
#include "native_wifi_sdk_fixture.inc"
#undef heap_caps_calloc
#undef heap_caps_free
void* heap_caps_calloc(size_t,size_t,unsigned);
void heap_caps_free(void*);
#include "ports/esp32s3/NativeHci.h"
#include "twatch_bluetooth.h"
#include "WifiApi.h"
#include <unistd.h>
extern "C" const risc_driver_v2* production_wifi_get(uint32_t);
extern "C" const risc_driver_v2* production_hci_get(uint32_t);
extern "C" void cross_app_begin(const void*,const void*);
extern "C" void cross_app_connect();
extern "C" void cross_app_scan();
extern "C" void cross_app_close();
extern "C" void cross_app_reopen();
extern "C" bool cross_app_retained();
extern "C" void cross_app_broadcast_fail();
extern "C" void cross_app_clock_sample();
extern "C" void cross_app_policy_error(unsigned);
extern "C" unsigned cross_app_policy_reads();
extern "C" const char *cross_app_result_message();
namespace RiscDiagnostics { uint64_t monotonicUs(){return uint64_t(now);} }
static RiscCpu::Port* cpu;
static RiscBoot::Runtime* runtime;
static esp_bt_controller_status_t btStatus=ESP_BT_CONTROLLER_STATUS_IDLE;
static const esp_vhci_host_callback_t* btCallback;
static bool btFailDisable;
static unsigned btInits;
static size_t btAllocation;
void enterCritical(portMUX_TYPE* p){assert(!*p);*p=1;}
void exitCritical(portMUX_TYPE* p){assert(*p==1);*p=0;}
void* heap_caps_calloc(size_t n,size_t bytes,unsigned flags){assert(flags==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));btAllocation=n*bytes;return calloc(n,bytes);}
void heap_caps_free(void* p){assert(zero(p,btAllocation));free(p);btAllocation=0;}
void vTaskDelay(unsigned n){now+=n*1000;}
esp_bt_controller_status_t esp_bt_controller_get_status(){return btStatus;}
esp_err_t esp_bt_controller_init(esp_bt_controller_config_t*){assert(btStatus==ESP_BT_CONTROLLER_STATUS_IDLE);++btInits;btStatus=ESP_BT_CONTROLLER_STATUS_INITED;return ESP_OK;}
esp_err_t esp_bt_controller_enable(esp_bt_mode_t){btStatus=ESP_BT_CONTROLLER_STATUS_ENABLED;return ESP_OK;}
esp_err_t esp_bt_controller_disable(){if(btFailDisable)return ESP_FAIL;btStatus=ESP_BT_CONTROLLER_STATUS_INITED;return ESP_OK;}
esp_err_t esp_bt_controller_deinit(){btStatus=ESP_BT_CONTROLLER_STATUS_IDLE;return ESP_OK;}
esp_err_t esp_vhci_host_register_callback(const esp_vhci_host_callback_t* p){btCallback=p;return ESP_OK;}
bool esp_vhci_host_check_send_available(){return true;}
void esp_vhci_host_send_packet(uint8_t*,uint16_t){assert(false);}
extern "C" bool cross_layer_storage_safe(){return runtime->providerStorageSafe();}
extern "C" bool cross_layer_activation_safe(){return runtime->promotionSafe();}
int main(int argc,char** argv){
 assert(argc==2);const std::string mode=argv[1];
 RiscCpu::Hardware hw{};hw.owner=[](){return true;};
 hw.radioJoin=RiscCpu::NativeRadio::join;hw.radioState=RiscCpu::NativeRadio::state;hw.radioLeave=RiscCpu::NativeRadio::leave;
 hw.radioAddresses=RiscCpu::NativeRadio::addresses;hw.radioScanStart=RiscCpu::NativeRadio::scanStart;
 hw.radioScanPoll=RiscCpu::NativeRadio::scanPoll;hw.radioScanCancel=RiscCpu::NativeRadio::scanCancel;hw.radioIdle=RiscCpu::NativeRadio::idle;
 hw.hciOpen=RiscCpu::NativeHci::open;hw.hciClose=RiscCpu::NativeHci::close;hw.hciSend=RiscCpu::NativeHci::send;hw.hciReceive=RiscCpu::NativeHci::receive;
 hw.hciIdle=RiscCpu::NativeHci::idle;hw.hciSafe=RiscCpu::NativeHci::safe;
 hw.radioIqReady=[](){return true;};hw.radioIqPrepare=[](){return true;};hw.radioIqCleanup=[](){return true;};
 RiscCpu::Port port(hw);cpu=&port;port.iq_.port=&port;
 RiscBoot::Port callbacks{};callbacks.owner=[](){return true;};
 callbacks.appExitSafe=[](){return cpu->appExitSafe();};callbacks.providerStorageSafe=[](){return cpu->providerStorageSafe();};
 runtime=new RiscBoot::Runtime(callbacks);port.radios_[0].port=&port;port.hci_.port=&port;
 const garden_radio_v1 native={1,sizeof(native),&port.radios_[0],RiscCpu::Port::radioClaim,RiscCpu::Port::radioJoin,RiscCpu::Port::radioState,RiscCpu::Port::radioLeave,RiscCpu::Port::radioRelease,RiscCpu::Port::radioStartAp,RiscCpu::Port::radioStopAp,RiscCpu::Port::radioAddresses,RiscCpu::Port::radioScanStart,RiscCpu::Port::radioScanPoll,RiscCpu::Port::radioScanCancel};
 const risc_hci_controller_status_v1 nativeHci={{1,sizeof(nativeHci),&port.hci_,RiscCpu::Port::hciOpen,RiscCpu::Port::hciSend,RiscCpu::Port::hciReceive,RiscCpu::Port::hciClose},RiscCpu::Port::hciStatus};
 const risc_hw_radio_v1 config={sizeof(config),0,1};
 const risc_hardware_device_v1 wifiDevice={1,sizeof(wifiDevice),15,"espressif,esp32s3-wifi","unspecified","radio.integrated",1,sizeof(config),&config};
 const risc_hardware_device_v1 bleDevice={1,sizeof(bleDevice),16,"espressif,esp32s3-ble","unspecified","radio.integrated",1,sizeof(config),&config};
 const risc_provider_dependency_v1 wifiDeps[]={{"hardware.device",1,&wifiDevice},{"platform.radio",1,&native}};
 const risc_provider_dependency_v1 bleDeps[]={{"hardware.device",1,&bleDevice},{"platform.hci.controller",1,&nativeHci}};
 const auto* wp=production_wifi_get(2);const auto* bp=production_hci_get(2);
 assert(wp->start(wifiDeps,2)&&bp->start(bleDeps,2));
 const auto* w=static_cast<const wifi_api_v1*>(wp->capability);const auto* b=static_cast<const portable_bluetooth_host_v1*>(bp->capability);
 assert(RiscCpu::NativeRadio::idle()&&RiscCpu::NativeHci::idle()&&calls.empty()&&!btInits);
 cross_app_begin(w,b);assert(!cross_app_retained()&&RiscCpu::NativeRadio::idle());
 if(mode=="cold-navigation"){
  for(unsigned cycle=0;cycle<12;++cycle){
   assert(b->controls.set_enabled(nullptr,cycle%2));assert(cpu->appExitSafe());
   cross_app_connect();assert(w->status(nullptr)==WIFI_LINK_JOINING&&!cpu->appExitSafe());
   assert(port.providerStorageSafe());cross_app_clock_sample();
   cross_app_scan();assert(scanAllocation&&port.radios_[0].scanning);
   cross_app_close();assert(cpu->appExitSafe()&&RiscCpu::NativeRadio::idle());
   assert(port.radios_[0].token);cross_app_reopen();
  }
 }else if(mode=="sdk-init-failure"){
  failure="wifi_init";cross_app_connect();assert(!cross_app_retained()&&RiscCpu::NativeRadio::idle()&&cpu->appExitSafe());
  failure.clear();cross_app_connect();assert(w->status(nullptr)==WIFI_LINK_JOINING);
 }else if(mode=="allocation-failure"){
  allocationFails=true;cross_app_scan();assert(!cross_app_retained()&&RiscCpu::NativeRadio::idle()&&cpu->appExitSafe());
  allocationFails=false;cross_app_scan();assert(scanAllocation&&port.radios_[0].scanning);
 }else if(mode=="iq-lease"){
  uint64_t token=0;assert(RiscCpu::Port::radioIqClaim(&port.iq_,&token)&&token);
  assert(!runtime->providerStorageSafe()&&!runtime->promotionSafe());
  cross_app_connect();assert(!cross_app_retained()&&RiscCpu::NativeRadio::idle()&&!connects);
  assert(RiscCpu::Port::radioIqRelease(&port.iq_,token));cross_app_connect();assert(connects==1);
 }else if(mode=="cleanup-failure"){
  cross_app_connect();failure="stop";cross_app_scan();assert(cross_app_retained()&&!cpu->appExitSafe()&&!cpu->providerStorageSafe());
 }else if(mode=="telemetry-failure"){
  cross_app_broadcast_fail();cross_app_connect();assert(cross_app_retained()&&RiscCpu::NativeRadio::idle());
 }else if(mode=="bluetooth-fault"){
  assert(b->controls.set_enabled(nullptr,true));uint8_t malformed[]={4,0x0e,2,0};
  assert(btCallback->notify_host_recv(malformed,sizeof(malformed))!=0);assert(!cpu->providerStorageSafe()&&!cpu->appExitSafe()&&!runtime->providerStorageSafe()&&!runtime->promotionSafe());
  // Healthy idle Wi-Fi itself remains idle. Global retention is separately
  // observable and a failed BT close must keep its exact owner/token.
  btFailDisable=true;assert(!b->controls.set_enabled(nullptr,false));assert(port.hci_.token&&port.hci_.closing);
  btFailDisable=false;assert(b->controls.set_enabled(nullptr,false));assert(cpu->providerStorageSafe()&&cpu->appExitSafe());
  cross_app_connect();assert(w->status(nullptr)==WIFI_LINK_JOINING);
 }else if(mode=="policy-io" || mode=="policy-corrupt" || mode=="policy-off"){
  cross_app_policy_error(mode=="policy-off"?2:mode=="policy-corrupt"?1:0);
  const unsigned reads=cross_app_policy_reads();cross_app_connect();assert(cross_app_policy_reads()==reads+1);
  assert(!cross_app_retained()&&RiscCpu::NativeRadio::idle()&&!connects);
  fprintf(stderr,"policy connect message: %s\n",cross_app_result_message());
  assert(!strcmp(cross_app_result_message(),mode=="policy-off"?"Wi-Fi is off in Quick Controls":"Radio settings unavailable; retry"));
  cross_app_scan();assert(cross_app_policy_reads()==reads+2);assert(!cross_app_retained()&&RiscCpu::NativeRadio::idle()&&!scanAllocation);
  assert(!strcmp(cross_app_result_message(),mode=="policy-off"?"Wi-Fi is off in Quick Controls":"Radio settings unavailable; retry"));
 }else if(mode=="prior-wifi-lease"){
  assert(w->connect(nullptr,"prior-owner",""));assert(!cpu->appExitSafe()&&!runtime->promotionSafe());
  cross_app_connect();assert(!cross_app_retained()&&w->status(nullptr)==WIFI_LINK_JOINING&&connects==2);
 }else assert(false);
 if(!cross_app_retained()){
  cross_app_close();assert(wp->quiesce()&&bp->quiesce());wp->stop();bp->stop();assert(cpu->quiescent());
 }
 for(const auto& line:stageLines)puts(line.c_str());
 printf("Wi-Fi production app/adapter + provider + CpuPort + native backend: %s PASS (Wi-Fi SDK connects=%d, BT inits=%u, retained=%u)\n",mode.c_str(),connects,btInits,unsigned(cross_app_retained()));
 fflush(stdout);_Exit(0);
}
