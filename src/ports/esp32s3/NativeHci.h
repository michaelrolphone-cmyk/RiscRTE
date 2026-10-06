#pragma once
// ESP-IDF 4.4 ESP32-S3 VHCI transport. Callback code and locks live in firmware,
// never in an ELF. The only queue allocation is bounded and owned by this port.
#include "HciBounds.h"
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
namespace RiscCpu { namespace NativeHci {
struct Packet { uint16_t size;uint8_t type,data[HciBounds::MaxPayload]; };
struct Buffers { Packet rx[HciBounds::QueueDepth];uint8_t tx[HciBounds::MaxPayload+1]; };
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static Buffers* buffers=nullptr;
static unsigned head=0,count=0;
static bool accepting=false,fault=false,initialized=false,enabled=false,initUncertain=false;
inline bool safe(){
  portENTER_CRITICAL(&mux);const bool result=!fault && !initUncertain;portEXIT_CRITICAL(&mux);
  return result && esp_bt_controller_get_status()==(enabled?ESP_BT_CONTROLLER_STATUS_ENABLED:initialized?ESP_BT_CONTROLLER_STATUS_INITED:ESP_BT_CONTROLLER_STATUS_IDLE);
}
inline bool idle(){return !buffers && !initialized && !enabled && !initUncertain && esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_IDLE;}
inline void sendAvailable(){} // The owner polls readiness for at most 20ms.
inline int received(uint8_t* data,uint16_t length){
  portENTER_CRITICAL(&mux);
  if(!accepting || !buffers){portEXIT_CRITICAL(&mux);return -1;}
  if(!data || length<1 || !HciBounds::rx(data[0],data+1,length-1) || count==HciBounds::QueueDepth){
    fault=true;accepting=false;portEXIT_CRITICAL(&mux);return -1;
  }
  auto& p=buffers->rx[(head+count)%HciBounds::QueueDepth];p.type=data[0];p.size=length-1;
  memcpy(p.data,data+1,p.size);++count;portEXIT_CRITICAL(&mux);return 0;
}
static const esp_vhci_host_callback_t callbacks={sendAvailable,received};
inline bool close(){
  // First prevent new queue writes. The native callbacks remain mapped forever.
  portENTER_CRITICAL(&mux);accepting=false;portEXIT_CRITICAL(&mux);
  if(initUncertain)return false;
  if(enabled){
    if(esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_ENABLED || esp_bt_controller_disable()!=ESP_OK)return false;
    enabled=false;
  }
  if(initialized){
    if(esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_INITED || esp_bt_controller_deinit()!=ESP_OK)return false;
    initialized=false;
  }
  if(esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_IDLE)return false;
  // No callback may retain this pointer: the entire copy above uses the lock.
  portENTER_CRITICAL(&mux);auto* old=buffers;buffers=nullptr;head=count=0;fault=false;portEXIT_CRITICAL(&mux);
  if(old){memset(old,0,sizeof(*old));heap_caps_free(old);}return true;
}
inline bool open(){
  if(!idle())return false;
  auto* allocated=static_cast<Buffers*>(heap_caps_calloc(1,sizeof(Buffers),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  if(!allocated)return false;
  portENTER_CRITICAL(&mux);buffers=allocated;head=count=0;fault=false;accepting=false;portEXIT_CRITICAL(&mux);
  esp_bt_controller_config_t config=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  config.bluetooth_mode=ESP_BT_MODE_BLE;
  // Avoid IDF's unbounded modem-wakeup semaphore inside VHCI send/disable.
  // Runtime Light/Deep entry remains forbidden until full controller cleanup.
  config.sleep_mode=ESP_BT_SLEEP_MODE_NONE;
  if(esp_bt_controller_init(&config)!=ESP_OK){
    // IDF 4.4 has early init failures after PHY/power setup but before status
    // becomes INITED. IDLE alone cannot prove rollback; retain until restart.
    initUncertain=true;return false;
  }
  initialized=true;
  if(esp_bt_controller_enable(ESP_BT_MODE_BLE)!=ESP_OK)return false;
  enabled=true;
  portENTER_CRITICAL(&mux);accepting=true;portEXIT_CRITICAL(&mux);
  if(esp_vhci_host_register_callback(&callbacks)!=ESP_OK)return false;
  return safe() && esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_ENABLED;
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
    if(count){const auto& p=buffers->rx[head];*type=p.type;*length=p.size;memcpy(data,p.data,p.size);
      memset(&buffers->rx[head],0,sizeof(Packet));head=(head+1)%HciBounds::QueueDepth;--count;
      portEXIT_CRITICAL(&mux);return true;}
    portEXIT_CRITICAL(&mux);
    if(esp_timer_get_time()>=deadline)return true; // Healthy empty poll.
    vTaskDelay(1);
  }
}
}}
