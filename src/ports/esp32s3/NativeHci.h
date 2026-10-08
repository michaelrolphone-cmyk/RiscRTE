#pragma once
// ESP-IDF 4.4 ESP32-S3 VHCI transport. Callback code and locks live in firmware,
// never in an ELF. The only queue allocation is bounded and owned by this port.
#include "HciBounds.h"
#include "diagnostics/StageLog.h"
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
namespace RiscCpu { namespace NativeHci {
// Keep the original four-maximum-packet memory budget, but charge each packet
// only for its actual bytes. Small advertising reports must not exhaust four
// large ACL slots while the cooperative owner is briefly drawing a frame.
constexpr size_t HeaderBytes=3;
constexpr size_t QueueBytes=HciBounds::QueueDepth*(HciBounds::MaxPayload+HeaderBytes);
struct Buffers { uint8_t rx[QueueBytes];uint8_t tx[HciBounds::MaxPayload+1]; };
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static Buffers* buffers=nullptr;
static size_t head=0,used=0;
static bool accepting=false,fault=false,initialized=false,enabled=false,initUncertain=false;
inline bool sdk(const char* step,esp_err_t code){
  (void)step;
  if(code==ESP_OK)return true;
  RISC_STAGE_LOG("radio bluetooth failure step=%s code=%d",step,int(code));return false;
}
#if RISC_STAGE_LOGS
// Callback only records a small reason under the existing lock. The owner
// reports it once; no formatting or diagnostic transport runs in the callback.
static uint8_t faultReason=0,reportedFault=0;
#endif
inline bool safe(){
  portENTER_CRITICAL(&mux);const bool result=!fault && !initUncertain;portEXIT_CRITICAL(&mux);
#if RISC_STAGE_LOGS
  portENTER_CRITICAL(&mux);const uint8_t reason=faultReason;portEXIT_CRITICAL(&mux);
  if(reason && reportedFault!=reason){reportedFault=reason;RISC_STAGE_LOG("radio bluetooth stream result=failed reason=%s",reason==2?"receive-queue-full":"malformed-packet");}
#endif
  return result && esp_bt_controller_get_status()==(enabled?ESP_BT_CONTROLLER_STATUS_ENABLED:initialized?ESP_BT_CONTROLLER_STATUS_INITED:ESP_BT_CONTROLLER_STATUS_IDLE);
}
inline bool idle(){return !buffers && !initialized && !enabled && !initUncertain && esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_IDLE;}
inline void sendAvailable(){} // The owner polls readiness for at most 20ms.
// Caller holds mux. Each copy spans at most two contiguous ring segments.
inline void queueWrite(size_t at,const uint8_t* data,size_t length){
  const size_t first=length<QueueBytes-at?length:QueueBytes-at;
  memcpy(buffers->rx+at,data,first);
  if(length>first)memcpy(buffers->rx,data+first,length-first);
}
inline void queueRead(size_t at,uint8_t* data,size_t length){
  const size_t first=length<QueueBytes-at?length:QueueBytes-at;
  memcpy(data,buffers->rx+at,first);memset(buffers->rx+at,0,first);
  if(length>first){memcpy(data+first,buffers->rx,length-first);memset(buffers->rx,0,length-first);}
}
inline int received(uint8_t* data,uint16_t length){
  portENTER_CRITICAL(&mux);
  if(!accepting || !buffers){portEXIT_CRITICAL(&mux);return -1;}
  const bool valid=data && length>=1 && HciBounds::rx(data[0],data+1,length-1);
  const bool full=valid && size_t(length-1)+HeaderBytes>QueueBytes-used;
  if(!valid || full){
#if RISC_STAGE_LOGS
    faultReason=full?2:1;
#endif
    fault=true;accepting=false;portEXIT_CRITICAL(&mux);return -1;
  }
  const uint16_t size=length-1;
  const uint8_t header[]={data[0],uint8_t(size),uint8_t(size>>8)};
  const size_t tail=(head+used)%QueueBytes;
  queueWrite(tail,header,HeaderBytes);
  queueWrite((tail+HeaderBytes)%QueueBytes,data+1,size);
  used+=HeaderBytes+size;portEXIT_CRITICAL(&mux);return 0;
}
static const esp_vhci_host_callback_t callbacks={sendAvailable,received};
inline bool close(){
  // First prevent new queue writes. The native callbacks remain mapped forever.
  portENTER_CRITICAL(&mux);accepting=false;portEXIT_CRITICAL(&mux);
  if(initUncertain){RISC_STAGE_LOG("radio bluetooth close result=retained reason=uncertain-init");return false;}
  if(enabled){
    if(esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_ENABLED){RISC_STAGE_LOG("radio bluetooth close result=retained reason=unexpected-enabled-state");return false;}
    if(!sdk("controller-disable",esp_bt_controller_disable()))return false;
    enabled=false;
  }
  if(initialized){
    if(esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_INITED){RISC_STAGE_LOG("radio bluetooth close result=retained reason=unexpected-initialized-state");return false;}
    if(!sdk("controller-deinit",esp_bt_controller_deinit()))return false;
    initialized=false;
  }
  if(esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_IDLE){RISC_STAGE_LOG("radio bluetooth close result=retained reason=controller-not-idle");return false;}
  // No callback may retain this pointer: the entire copy above uses the lock.
  portENTER_CRITICAL(&mux);auto* old=buffers;buffers=nullptr;head=used=0;fault=false;portEXIT_CRITICAL(&mux);
  if(old){memset(old,0,sizeof(*old));heap_caps_free(old);RISC_STAGE_LOG("radio bluetooth close result=ok state=off");}
#if RISC_STAGE_LOGS
  faultReason=reportedFault=0;
#endif
  return true;
}
inline bool open(){
  if(!idle()){RISC_STAGE_LOG("radio bluetooth open result=rejected reason=not-idle");return false;}
  RISC_STAGE_LOG("radio bluetooth open begin");
  auto* allocated=static_cast<Buffers*>(heap_caps_calloc(1,sizeof(Buffers),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  if(!allocated){RISC_STAGE_LOG("radio bluetooth open result=failed step=packet-buffers reason=out-of-memory");return false;}
  portENTER_CRITICAL(&mux);buffers=allocated;head=used=0;fault=false;accepting=false;portEXIT_CRITICAL(&mux);
  esp_bt_controller_config_t config=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  config.bluetooth_mode=ESP_BT_MODE_BLE;
  // Avoid IDF's unbounded modem-wakeup semaphore inside VHCI send/disable.
  // Runtime Light/Deep entry remains forbidden until full controller cleanup.
  config.sleep_mode=ESP_BT_SLEEP_MODE_NONE;
  if(!sdk("controller-init",esp_bt_controller_init(&config))){
    // IDF 4.4 has early init failures after PHY/power setup but before status
    // becomes INITED. IDLE alone cannot prove rollback; retain until restart.
    initUncertain=true;RISC_STAGE_LOG("radio bluetooth open result=retained reason=uncertain-init");return false;
  }
  initialized=true;
  if(!sdk("controller-enable",esp_bt_controller_enable(ESP_BT_MODE_BLE)))return false;
  enabled=true;
  portENTER_CRITICAL(&mux);accepting=true;portEXIT_CRITICAL(&mux);
  if(!sdk("register-callback",esp_vhci_host_register_callback(&callbacks)))return false;
  const bool ok=safe() && esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_ENABLED;
  RISC_STAGE_LOG("radio bluetooth open result=%s state=%s",ok?"ok":"failed",ok?"on":"unconfirmed");return ok;
}
inline bool healthy(){return enabled && safe() && esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_ENABLED;}
inline bool send(uint8_t type,const uint8_t* data,size_t length,uint32_t ms){
  if(!healthy() || !buffers || ms>HciBounds::MaxWaitMs || !HciBounds::tx(type,data,length))return false;
  const int64_t deadline=esp_timer_get_time()+int64_t(ms)*1000;
  while(!esp_vhci_host_check_send_available()){
    if(!healthy() || esp_timer_get_time()>=deadline)return false;
    vTaskDelay(1);
  }
  // SDK consumes this bounded firmware-owned copy synchronously. Never pass an
  // app/ELF pointer to the controller or send under a critical section.
  buffers->tx[0]=type;memcpy(buffers->tx+1,data,length);
  esp_vhci_host_send_packet(buffers->tx,uint16_t(length+1));
  memset(buffers->tx,0,length+1);return healthy();
}
inline bool receive(uint8_t* type,uint8_t* data,size_t capacity,size_t* length,uint32_t ms){
  if(length)*length=0;
  if(type)*type=0;
  if(!type || !data || !length || capacity<HciBounds::MaxPayload || ms>HciBounds::MaxWaitMs)return false;
  const int64_t deadline=esp_timer_get_time()+int64_t(ms)*1000;
  for(;;){
    if(!healthy())return false;
    portENTER_CRITICAL(&mux);
    if(fault || !accepting || !buffers){portEXIT_CRITICAL(&mux);return false;}
    if(used){uint8_t header[HeaderBytes];queueRead(head,header,HeaderBytes);
      *type=header[0];*length=size_t(header[1])+(size_t(header[2])<<8);
      queueRead((head+HeaderBytes)%QueueBytes,data,*length);
      const size_t consumed=HeaderBytes+*length;head=(head+consumed)%QueueBytes;used-=consumed;
      portEXIT_CRITICAL(&mux);return true;}
    portEXIT_CRITICAL(&mux);
    if(esp_timer_get_time()>=deadline)return true; // Healthy empty poll.
    vTaskDelay(1);
  }
}
}}
