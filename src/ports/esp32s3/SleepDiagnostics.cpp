#include "SleepDiagnostics.h"
#if RISC_SLEEP_DIAGNOSTICS
#include "diagnostics/Journal.h"
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_sleep.h>
#include <esp_system.h>
namespace RiscDiagnostics {
namespace {
// No flash/NVS writes. RTC no-init may survive deep/software/watchdog resets;
// every boot validates it. Cold/brownout/external/unknown reset discards it.
RTC_NOINIT_ATTR Journal journal;
Replay replay;
TaskHandle_t owner=nullptr;
bool ours(){return owner && owner==xTaskGetCurrentTaskHandle();}
// Must remain the sole HWCDC ring producer: no setDebugOutput or concurrent
// Serial writer. Pinned HWCDC can underflow a zero timeout without capacity.
struct Transport {
  bool connected(){return bool(Serial);}
  size_t writable(){int n=Serial.availableForWrite();return n>0?size_t(n):0;}
  size_t write(const uint8_t* p,size_t n){return Serial.write(p,n);}
} transport;
}
void start(){
  owner=xTaskGetCurrentTaskHandle();Serial.setTxTimeoutMs(0);
  const auto reset=esp_reset_reason();
  const bool retain=reset==ESP_RST_SW || reset==ESP_RST_DEEPSLEEP || reset==ESP_RST_PANIC ||
    reset==ESP_RST_INT_WDT || reset==ESP_RST_TASK_WDT || reset==ESP_RST_WDT;
  begin(journal,retain,millis(),int32_t(reset),uint32_t(esp_sleep_get_wakeup_cause()));
}
void poll(){
  if(!ours())return;
  if(!transport.connected()){replay.disconnect();return;}
  for(unsigned i=0;i<16 && Serial.available()>0;++i){int c=Serial.read();if(c<0)break;replay.input(char(c),journal);}
  replay.poll(transport);
}
void line(const char* text){
  if(!ours() || !text)return;
  message(journal,millis(),text);
  // Live output is best effort; replay owns the wire while active. One small
  // owner-task write cannot wait for a missing host or fill the HWCDC TX ring.
  if(replay.active() || !transport.connected())return;
  char bytes[256];size_t n=0;
  while(n<sizeof(bytes)-1 && text[n]){bytes[n]=text[n];++n;}
  bytes[n++]='\n';
  if(transport.writable()>=n)transport.write(reinterpret_cast<const uint8_t*>(bytes),n);
}
void lightEnter(){if(ours())event(journal,millis(),LightEnter);}
void lightReturn(int32_t result,uint32_t cause){if(ours())event(journal,millis(),LightReturn,result,cause);}
void deepEnter(){if(ours())event(journal,millis(),DeepEnter);}
}
#endif
