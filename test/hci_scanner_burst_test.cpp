#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include "ports/esp32s3/NativeHci.h"
#include "RiscBluetoothHostV1.h"
#include "RiscBluetoothSensorsV1.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace RiscCpu;
extern "C" const risc_driver_v2* production_hci_get(uint32_t);
extern "C" const risc_driver_v2* production_scanner_get(uint32_t);
static esp_bt_controller_status_t sdkStatus=ESP_BT_CONTROLLER_STATUS_IDLE;
static const esp_vhci_host_callback_t* callback;
static uint64_t ticks;
static bool failDisable,burstOnStart;
static unsigned initCount,sendCount,acceptedReports;
static size_t allocatedBytes;
void enterCritical(portMUX_TYPE* p){assert(!*p);*p=1;}
void exitCritical(portMUX_TYPE* p){assert(*p==1);*p=0;}
void* heap_caps_calloc(size_t n,size_t size,unsigned flags){assert(flags==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));allocatedBytes=n*size;return calloc(n,size);}
void heap_caps_free(void* p){for(size_t i=0;i<allocatedBytes;++i)assert(!static_cast<uint8_t*>(p)[i]);free(p);}
int64_t esp_timer_get_time(){return int64_t(ticks)*1000;}
void vTaskDelay(unsigned n){ticks+=n;}
esp_bt_controller_status_t esp_bt_controller_get_status(){return sdkStatus;}
esp_err_t esp_bt_controller_init(esp_bt_controller_config_t*){assert(sdkStatus==ESP_BT_CONTROLLER_STATUS_IDLE);++initCount;sdkStatus=ESP_BT_CONTROLLER_STATUS_INITED;return ESP_OK;}
esp_err_t esp_bt_controller_enable(esp_bt_mode_t){sdkStatus=ESP_BT_CONTROLLER_STATUS_ENABLED;return ESP_OK;}
esp_err_t esp_bt_controller_disable(){if(failDisable)return ESP_FAIL;sdkStatus=ESP_BT_CONTROLLER_STATUS_INITED;return ESP_OK;}
esp_err_t esp_bt_controller_deinit(){sdkStatus=ESP_BT_CONTROLLER_STATUS_IDLE;return ESP_OK;}
esp_err_t esp_vhci_host_register_callback(const esp_vhci_host_callback_t* p){callback=p;return ESP_OK;}
bool esp_vhci_host_check_send_available(){return true;}
static int advertisement(unsigned identity){
 uint8_t report[]={4,0x3e,12,2,1,0,0,uint8_t(identity),0,0,0,0,0,0,uint8_t(-42)};
 int rc=callback->notify_host_recv(report,sizeof(report));if(rc==0)++acceptedReports;return rc;
}
void esp_vhci_host_send_packet(uint8_t* p,uint16_t n){
 assert(n>=4 && p[0]==1);++sendCount;
 uint8_t complete[]={4,0x0e,4,1,p[1],p[2],0};
 assert(callback->notify_host_recv(complete,sizeof(complete))==0);
 if(p[1]==0x0c && p[2]==0x20 && p[4]==1 && burstOnStart)
  for(unsigned i=0;i<32;++i)(void)advertisement(i);
}
int main(){
 Hardware hardware{};hardware.owner=[](){return true;};
 hardware.hciOpen=NativeHci::open;hardware.hciClose=NativeHci::close;
 hardware.hciSend=NativeHci::send;hardware.hciReceive=NativeHci::receive;
 hardware.hciIdle=NativeHci::idle;hardware.hciSafe=NativeHci::safe;
 Port port(hardware);port.hci_.port=&port;
 const risc_hci_controller_status_v1 native={{1,sizeof(native),&port.hci_,Port::hciOpen,Port::hciSend,Port::hciReceive,Port::hciClose},Port::hciStatus};
 const risc_hw_radio_v1 config={sizeof(config),0,1};
 const risc_hardware_device_v1 device={1,sizeof(device),16,"espressif,esp32s3-ble","unspecified","radio.integrated",1,sizeof(config),&config};
 const risc_provider_dependency_v1 hciDependencies[]={{"hardware.device",1,&device},{"platform.hci.controller",1,&native}};
 const auto* controller=production_hci_get(2);assert(controller && controller->start(hciDependencies,2));
 const auto* host=static_cast<const portable_bluetooth_host_v1*>(controller->capability);
 const risc_platform_clock_api_v1 clock={1,sizeof(clock),nullptr,[](void*)->uint64_t{return ticks;},[](void*,uint32_t ms){ticks+=ms;}};
 const risc_provider_dependency_v1 scanDependencies[]={{"bluetooth.hci",1,host},{"platform.clock",1,&clock}};
 const auto* scanner=production_scanner_get(2);assert(scanner && scanner->start(scanDependencies,2));
 const auto* sensor=static_cast<const risc_bluetooth_sensors_v1*>(scanner->capability);
 // Reuse the same retained provider mappings, alternating initial controller
 // intent and reports arriving with the enable acknowledgement or after it.
 for(unsigned cycle=0;cycle<20;++cycle){
  const bool wasOn=cycle%2;burstOnStart=cycle%3!=2;acceptedReports=0;
  assert(host->controls.set_enabled(nullptr,wasOn));uint64_t token=0;
  const unsigned before=sendCount;
  assert(sensor->open(nullptr,&token) && token);
  risc_ble_sensor_status_v1 state{};state.struct_size=sizeof(state);
  for(unsigned i=0;i<10;++i){assert(sensor->poll(nullptr,token,6));ticks+=20;assert(sensor->status(nullptr,&state));if(state.state==2)break;}
  assert(state.state==2 && sendCount-before==5);
  if(!burstOnStart)for(unsigned i=0;i<32;++i)assert(advertisement(i)==0);
  assert(acceptedReports==32);
  for(unsigned i=0;i<6;++i)assert(sensor->poll(nullptr,token,6));
  assert(sensor->status(nullptr,&state) && state.count==32 && state.state==2 && !state.malformed && !state.dropped);
  for(unsigned i=0;i<32;++i){risc_ble_sensor_device_v1 value{};assert(sensor->device(nullptr,i,&value));assert(value.address[0]==i && value.rssi==-42 && value.reports==1);}
  if(cycle==19){
   unsigned queued=0;while(queued<1024 && advertisement(queued)==0)++queued;
   assert(queued==242 && !NativeHci::safe() && !port.providerStorageSafe());
   assert(!sensor->poll(nullptr,token,6));
   assert(sensor->status(nullptr,&state) && state.state==4 && state.cleanup_pending);
  }
  // A real SDK cleanup failure retains both leases and denies app exit.
  if(cycle==5 || cycle==19){failDisable=true;assert(!sensor->close(nullptr,token));assert(!scanner->quiesce());assert(!port.appExitSafe());failDisable=false;}
  assert(sensor->close(nullptr,token));uint8_t actual=99;
  assert(host->controls.status(nullptr,&actual) && actual==unsigned(wasOn));
  assert(port.appExitSafe());
 }
 assert(scanner->quiesce());scanner->stop();assert(controller->quiesce());controller->stop();
 assert(port.quiescent() && NativeHci::idle());
 printf("Production BLE scanner + persistent HCI provider + CpuPort + NativeHci: 20 startup/burst/close cycles, 640 exact reports, true overflow and retained cleanup PASS (controller inits=%u)\n",initCount);
}
