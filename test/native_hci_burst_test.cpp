#include "ports/esp32s3/NativeHci.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>
using namespace RiscCpu;
static esp_bt_controller_status_t status=ESP_BT_CONTROLLER_STATUS_IDLE;
static const esp_vhci_host_callback_t* callback;
static bool refuseClose;
static size_t allocatedBytes;
void enterCritical(portMUX_TYPE* p){assert(!*p);*p=1;}
void exitCritical(portMUX_TYPE* p){assert(*p==1);*p=0;}
void* heap_caps_calloc(size_t n,size_t size,unsigned flags){assert(flags==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));allocatedBytes=n*size;return calloc(n,size);}
void heap_caps_free(void* p){for(size_t i=0;i<allocatedBytes;++i)assert(!static_cast<uint8_t*>(p)[i]);free(p);}
int64_t esp_timer_get_time(){return 0;}
void vTaskDelay(unsigned){assert(false);}
esp_bt_controller_status_t esp_bt_controller_get_status(){return status;}
esp_err_t esp_bt_controller_init(esp_bt_controller_config_t*){status=ESP_BT_CONTROLLER_STATUS_INITED;return ESP_OK;}
esp_err_t esp_bt_controller_enable(esp_bt_mode_t){status=ESP_BT_CONTROLLER_STATUS_ENABLED;return ESP_OK;}
esp_err_t esp_bt_controller_disable(){if(refuseClose)return ESP_FAIL;status=ESP_BT_CONTROLLER_STATUS_INITED;return ESP_OK;}
esp_err_t esp_bt_controller_deinit(){status=ESP_BT_CONTROLLER_STATUS_IDLE;return ESP_OK;}
esp_err_t esp_vhci_host_register_callback(const esp_vhci_host_callback_t* p){callback=p;return ESP_OK;}
bool esp_vhci_host_check_send_available(){return true;}
void esp_vhci_host_send_packet(uint8_t*,uint16_t){}
static std::vector<uint8_t> packet(size_t payload,unsigned sequence){
 assert(payload>=4 && payload<=1028);
 const uint8_t type=payload>257?2:4;
 std::vector<uint8_t> out(payload+1);out[0]=type;
 for(size_t i=1;i<out.size();++i)out[i]=uint8_t(sequence+i);
 if(type==4){out[1]=0x3e;out[2]=uint8_t(payload-2);}
 else {out[1]=1;out[2]=0;out[3]=uint8_t(payload-4);out[4]=uint8_t((payload-4)>>8);}
 return out;
}
static void take(const std::vector<uint8_t>& expected){
 uint8_t type=0,data[1028]{};size_t size=0;
 assert(NativeHci::receive(&type,data,sizeof(data),&size,0));
 assert(type==expected[0] && size+1==expected.size() && !memcmp(data,expected.data()+1,size));
}
int main(){
 assert(NativeHci::open());
 // A legacy four-slot queue rejects the fifth ordinary short advertisement.
 std::deque<std::vector<uint8_t>> expected;
 for(unsigned i=0;i<32;++i){expected.push_back(packet(43,i));assert(callback->notify_host_recv(expected.back().data(),expected.back().size())==0);}
 for(const auto& p:expected)take(p);
 expected.clear();
 assert(allocatedBytes<=5158); // Former four maximum-sized packets plus TX.
 // Mixed sizes repeatedly wrap payload boundaries.
 for(unsigned i=0;i<600;++i){
  const size_t sizes[]={1028,257,6,19,73};auto p=packet(sizes[i%5],i);
  assert(callback->notify_host_recv(p.data(),p.size())==0);expected.push_back(p);
  if(expected.size()==3){take(expected.front());expected.pop_front();}
 }
 for(const auto& p:expected)take(p);
 assert(NativeHci::close() && NativeHci::idle());
 // Advance the empty ring to offsets 4,122 and 4,123, so the next three-byte
 // metadata record splits at each possible position across the ring end.
 for(size_t last : {1026u,1027u}){
  assert(NativeHci::open());
  for(size_t size : {size_t(1028),size_t(1028),size_t(1028),last}){
   auto p=packet(size,unsigned(size));assert(callback->notify_host_recv(p.data(),p.size())==0);take(p);
  }
  auto p=packet(6,unsigned(last));assert(callback->notify_host_recv(p.data(),p.size())==0);take(p);
  assert(NativeHci::close());
 }
 // Four maximal ACL packets still fit; no packet is truncated or discarded.
 assert(NativeHci::open());auto largest=packet(1028,9);
 for(unsigned i=0;i<4;++i)assert(callback->notify_host_recv(largest.data(),largest.size())==0);
 for(unsigned i=0;i<4;++i)take(largest);
 assert(NativeHci::close());
 // True byte-capacity exhaustion remains terminal and retains failed cleanup.
 assert(NativeHci::open());
 for(unsigned i=0;i<4;++i)assert(callback->notify_host_recv(largest.data(),largest.size())==0);
 auto extra=packet(6,1);assert(callback->notify_host_recv(extra.data(),extra.size())==-1);
 assert(!NativeHci::safe());refuseClose=true;assert(!NativeHci::close() && !NativeHci::idle());
 refuseClose=false;assert(NativeHci::close() && NativeHci::idle());
 for(auto malformed: {std::vector<uint8_t>{4,0x3e,2,2},std::vector<uint8_t>{2,1,0,4,0,1},std::vector<uint8_t>{1,0,0}}){
  assert(NativeHci::open());assert(callback->notify_host_recv(malformed.data(),malformed.size())==-1);
  assert(!NativeHci::safe());assert(NativeHci::close());
 }
 assert(callback->notify_host_recv(extra.data(),extra.size())==-1);
 puts("Native HCI burst: 32 short reports, mixed FIFO wraparound, maximum ACL, malformed lengths and retained true overflow PASS");
}
