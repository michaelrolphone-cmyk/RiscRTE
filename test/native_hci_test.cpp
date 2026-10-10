#include "ports/esp32s3/NativeHci.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#define RADIO_STAGE_SAFE() assert(!RiscCpu::NativeHci::mux)
#include "radio_stage_capture.h"
using namespace RiscCpu;
static esp_bt_controller_status_t status=ESP_BT_CONTROLLER_STATUS_IDLE;
static const esp_vhci_host_callback_t* cb=nullptr;
static std::string failure;
static esp_err_t failureResult=ESP_FAIL;
static unsigned allocations=0,frees=0,initializations=0,enables=0,disables=0,deinitializations=0,sends=0;
static int64_t now=0;
static bool ready=true;
static std::vector<uint8_t> sent;
static uint8_t event[]={4,0x0e,1,42};
void enterCritical(portMUX_TYPE* p){assert(!*p);*p=1;}
void exitCritical(portMUX_TYPE* p){assert(*p==1);*p=0;}
void* heap_caps_calloc(size_t n,size_t size,unsigned flags){assert(flags==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));if(failure=="alloc")return nullptr;++allocations;return calloc(n,size);}
void heap_caps_free(void* p){assert(p && !NativeHci::mux);++frees;free(p);}
int64_t esp_timer_get_time(){return now;}
void vTaskDelay(unsigned n){assert(n==1 && !NativeHci::mux);now+=1000;}
esp_bt_controller_status_t esp_bt_controller_get_status(){return status;}
esp_err_t esp_bt_controller_init(esp_bt_controller_config_t* c){assert(status==ESP_BT_CONTROLLER_STATUS_IDLE && c->bluetooth_mode==ESP_BT_MODE_BLE && c->sleep_mode==ESP_BT_SLEEP_MODE_NONE);++initializations;if(failure=="init")return ESP_FAIL;status=ESP_BT_CONTROLLER_STATUS_INITED;return ESP_OK;}
esp_err_t esp_bt_controller_enable(esp_bt_mode_t m){assert(status==ESP_BT_CONTROLLER_STATUS_INITED && m==ESP_BT_MODE_BLE);++enables;if(failure=="enable")return failureResult;status=ESP_BT_CONTROLLER_STATUS_ENABLED;return ESP_OK;}
esp_err_t esp_bt_controller_disable(){assert(status==ESP_BT_CONTROLLER_STATUS_ENABLED && !NativeHci::mux);++disables;if(cb)assert(cb->notify_host_recv(event,sizeof(event))==-1);if(failure=="disable")return ESP_FAIL;status=ESP_BT_CONTROLLER_STATUS_INITED;return ESP_OK;}
esp_err_t esp_bt_controller_deinit(){assert(status==ESP_BT_CONTROLLER_STATUS_INITED && !NativeHci::mux);++deinitializations;if(failure=="deinit")return ESP_FAIL;status=ESP_BT_CONTROLLER_STATUS_IDLE;return ESP_OK;}
esp_err_t esp_vhci_host_register_callback(const esp_vhci_host_callback_t* c){assert(status==ESP_BT_CONTROLLER_STATUS_ENABLED && c);cb=c;return failure=="register"?ESP_FAIL:ESP_OK;}
bool esp_vhci_host_check_send_available(){assert(!NativeHci::mux);return ready;}
void esp_vhci_host_send_packet(uint8_t* p,uint16_t n){assert(!NativeHci::mux && ready && p==NativeHci::buffers->tx);++sends;sent.assign(p,p+n);if(failure=="send-fault")cb->notify_host_recv(nullptr,0);}
static void clean(){failure.clear();assert(NativeHci::close() && NativeHci::idle() && NativeHci::safe());assert(allocations==frees);if(cb)assert(cb->notify_host_recv(event,sizeof(event))==-1);}
int main(){
 assert(NativeHci::idle() && NativeHci::safe());
 status=ESP_BT_CONTROLLER_STATUS_ENABLED;assert(!NativeHci::open() && !NativeHci::close() && !allocations && !disables);status=ESP_BT_CONTROLLER_STATUS_IDLE;
 failure="alloc";assert(!NativeHci::open() && NativeHci::idle() && !initializations);clean();
 for(const char* phase:{"enable","register"}){failure=phase;assert(!NativeHci::open() && !NativeHci::idle());clean();}
 assert(NativeHci::open() && NativeHci::safe() && !NativeHci::idle());assert(!NativeHci::open());
 const unsigned starts=initializations;uint8_t type=9,out[1028]{};size_t n=999;
 assert(NativeHci::receive(&type,out,sizeof(out),&n,0) && !type && !n && now==0);
 assert(NativeHci::receive(&type,out,sizeof(out),&n,20) && !n && now==20000);
 assert(!NativeHci::receive(&type,out,1027,&n,0) && !n);
 assert(!NativeHci::receive(&type,out,sizeof(out),&n,21) && !n);
 uint8_t command[]={3,12,0};assert(!NativeHci::send(4,command,3,0));assert(!NativeHci::send(1,command,2,0));
 assert(!NativeHci::send(1,command,3,21));assert(NativeHci::send(1,command,3,0));assert(sent==std::vector<uint8_t>({1,3,12,0}));
 for(unsigned i=0;i<4;++i)assert(cb->notify_host_recv(event,sizeof(event))==0);
 for(unsigned i=0;i<4;++i){assert(NativeHci::receive(&type,out,sizeof(out),&n,0) && type==4 && n==3 && out[2]==42);}
 assert(NativeHci::receive(&type,out,sizeof(out),&n,0) && !n);
 ready=false;const auto start=now;assert(!NativeHci::send(1,command,3,20) && now-start==20000);ready=true;
 assert(initializations==starts); // No send/receive ever auto-initializes.
 for(const char* phase:{"disable","deinit"}){failure=phase;assert(!NativeHci::close() && !NativeHci::idle());clean();assert(NativeHci::open());}
 // A malformed event or queue loss is terminal for this stream, not silent loss.
 assert(cb->notify_host_recv(nullptr,0)==-1 && !NativeHci::safe());assert(!NativeHci::receive(&type,out,sizeof(out),&n,0));clean();
 uint8_t acl[1029]={2,1,0,0,4};
 assert(NativeHci::open());for(unsigned i=0;i<4;++i)assert(cb->notify_host_recv(acl,sizeof(acl))==0);
 assert(cb->notify_host_recv(event,sizeof(event))==-1 && !NativeHci::safe());clean();
 assert(NativeHci::open());failure="send-fault";assert(!NativeHci::send(1,command,3,0));clean();
 assert(NativeHci::open());status=ESP_BT_CONTROLLER_STATUS_IDLE;assert(!NativeHci::safe() && !NativeHci::close());status=ESP_BT_CONTROLLER_STATUS_ENABLED;clean();
 failure="enable";failureResult=0x3131;assert(!NativeHci::open());clean();failureResult=ESP_FAIL;
 // SDK early-init failures can own PHY resources even while reporting IDLE.
 failure="init";assert(!NativeHci::open() && status==ESP_BT_CONTROLLER_STATUS_IDLE && !NativeHci::idle() && !NativeHci::safe());
 failure.clear();assert(!NativeHci::close() && NativeHci::buffers && !NativeHci::open());
#if RISC_STAGE_LOGS
 assert(stageHas("failure step=controller-enable code=12593"));
 for(const char* step:{"controller-init","controller-enable","register-callback","controller-disable","controller-deinit"})
  assert(stageHas((std::string("failure step=")+step+" code=-1").c_str()));
 assert(stageHas("radio bluetooth open result=ok state=on"));
 assert(stageHas("radio bluetooth close result=ok state=off"));
 assert(stageHas("reason=out-of-memory")&&stageHas("reason=receive-queue-full")&&stageHas("reason=malformed-packet"));
 assert(stageHas("result=retained reason=uncertain-init"));
 assert(stageCount("reason=receive-queue-full")==1);
#else
 assert(stageLines.empty());
#endif
 // Test process ends; real firmware retains this last allocation until reset.
 puts("Native HCI: exact SDK lifecycle, bounded copied RX/TX, deadline, overflow, stale callback and retained uncertainty PASS");
}
