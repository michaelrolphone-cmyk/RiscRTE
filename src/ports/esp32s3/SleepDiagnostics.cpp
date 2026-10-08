#include "SleepDiagnostics.h"
#if RISC_DIAGNOSTIC_ADAPTER
#if RISC_PERFORMANCE_TRACE
#include "diagnostics/Performance.h"
#endif
#if RISC_SLEEP_DIAGNOSTICS
#include "diagnostics/Journal.h"
#include <esp_attr.h>
#include <esp_system.h>
#endif
#if RISC_HWCDC_SLEEP_RECOVERY
#include "diagnostics/UsbSleepRecovery.h"
#endif
#include <Arduino.h>
#include <esp_sleep.h>
namespace RiscDiagnostics {
namespace {
#if RISC_SLEEP_DIAGNOSTICS
// No flash/NVS writes. RTC no-init may survive deep/software/watchdog resets;
// every boot validates it. Cold/brownout/external/unknown reset discards it.
RTC_NOINIT_ATTR Journal journal;
Replay replay;
#endif
#if RISC_HWCDC_SLEEP_RECOVERY
UsbSleepRecovery recovery;
#endif
#if RISC_PERFORMANCE_TRACE
RiscPerf::Replay performanceReplay;
#endif
TaskHandle_t owner=nullptr;
bool ours(){return owner && owner==xTaskGetCurrentTaskHandle();}
// Must remain the sole HWCDC ring producer: no setDebugOutput or concurrent
// Serial writer. Pinned HWCDC can underflow a zero timeout without capacity.
struct Transport {
  bool connected(){return bool(Serial);}
  size_t writable(){int n=Serial.availableForWrite();return n>0?size_t(n):0;}
  size_t write(const uint8_t* p,size_t n){return Serial.write(p,n);}
#if RISC_HWCDC_SLEEP_RECOVERY
  uint32_t nowMs(){return millis();}
  void end(){Serial.end();}
  bool begin(){
    Serial.begin(115200);Serial.setTxTimeoutMs(0);
    // Pinned begin() returns void. Missing RX queue returns -1; missing TX
    // queue/mutex returns zero. Interrupt allocation failure calls end(). A
    // fresh sole-producer TX ring is empty, irrespective of host connection.
    return Serial.available()>=0 && Serial.availableForWrite()>0;
  }
#endif
} transport;
}
void start(){
  owner=xTaskGetCurrentTaskHandle();Serial.setTxTimeoutMs(0);
#if RISC_PERFORMANCE_TRACE
  performanceReplay.disconnect();
#endif
#if RISC_HWCDC_SLEEP_RECOVERY
  recovery.reset();
#endif
#if RISC_SLEEP_DIAGNOSTICS
  replay.disconnect();
  const auto reset=esp_reset_reason();
  const bool retain=reset==ESP_RST_SW || reset==ESP_RST_DEEPSLEEP || reset==ESP_RST_PANIC ||
    reset==ESP_RST_INT_WDT || reset==ESP_RST_TASK_WDT || reset==ESP_RST_WDT;
  begin(journal,retain,millis(),int32_t(reset),uint32_t(esp_sleep_get_wakeup_cause()));
#endif
}
void poll(){
  if(!ours())return;
#if RISC_HWCDC_SLEEP_RECOVERY
  if(!recovery.poll(transport))return;
#endif
#if RISC_SLEEP_DIAGNOSTICS || RISC_PERFORMANCE_TRACE
  if(!transport.connected()){
#if RISC_SLEEP_DIAGNOSTICS
    replay.disconnect();
#endif
#if RISC_PERFORMANCE_TRACE
    performanceReplay.disconnect();
#endif
    return;
  }
  for(unsigned i=0;i<16 && Serial.available()>0;++i){
    int c=Serial.read();if(c<0)break;
#if RISC_SLEEP_DIAGNOSTICS
#if RISC_PERFORMANCE_TRACE
    if(!performanceReplay.active())
#endif
      replay.input(char(c),journal);
#endif
#if RISC_PERFORMANCE_TRACE
#if RISC_SLEEP_DIAGNOSTICS
    if(!replay.active())
#endif
      performanceReplay.input(char(c));
#endif
#if RISC_SLEEP_DIAGNOSTICS && RISC_PERFORMANCE_TRACE
    // The winning command's newline must also discard the other parser's
    // prefix; otherwise the next request would concatenate two commands.
    if(replay.active())performanceReplay.disconnect();
    else if(performanceReplay.active())replay.disconnect();
#endif
  }
#if RISC_PERFORMANCE_TRACE
  if(performanceReplay.active()){performanceReplay.poll(transport);return;}
#endif
#if RISC_SLEEP_DIAGNOSTICS
  replay.poll(transport);
#endif
#endif
}
void line(const char* text){
  if(!ours() || !text)return;
#if RISC_SLEEP_DIAGNOSTICS
  message(journal,millis(),text);
  if(replay.active())return;
#endif
#if RISC_PERFORMANCE_TRACE
  if(performanceReplay.active())return;
#endif
#if RISC_HWCDC_SLEEP_RECOVERY
  if(!recovery.ready())return;
#endif
  // Live output is best effort. One small owner-task write cannot wait for a
  // missing host or fill the HWCDC TX ring. Recovery never waits for a host.
  if(!transport.connected())return;
  char bytes[256];size_t n=0;
  while(n<sizeof(bytes)-1 && text[n]){bytes[n]=text[n];++n;}
  bytes[n++]='\n';
  if(transport.writable()>=n)transport.write(reinterpret_cast<const uint8_t*>(bytes),n);
}
void lightEnter(){
#if RISC_SLEEP_DIAGNOSTICS
  if(ours())event(journal,millis(),LightEnter);
#endif
}
void lightReturn(int32_t result,uint32_t cause){
  if(!ours())return;
#if RISC_SLEEP_DIAGNOSTICS
  event(journal,millis(),LightReturn,result,cause);
#else
  (void)cause;
#endif
#if RISC_HWCDC_SLEEP_RECOVERY
  if(result==ESP_OK){
#if RISC_SLEEP_DIAGNOSTICS
    replay.disconnect();
#endif
#if RISC_PERFORMANCE_TRACE
    performanceReplay.disconnect();
#endif
    recovery.request(); // RAM only; wake-source cleanup runs before any USB I/O.
  }
#endif
}
void deepEnter(){
#if RISC_SLEEP_DIAGNOSTICS
  if(ours())event(journal,millis(),DeepEnter);
#endif
}
}
#endif
