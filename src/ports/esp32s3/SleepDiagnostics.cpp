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
#include <cstdio>
#if RISC_STAGE_LOGS
#include <esp_timer.h>
#include <cstdarg>
#endif
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
// Trusted native composition hook. The borrowed line is valid only during this
// owner-task call. It must not allocate, block, mutate Runtime or recurse.
extern "C" void risc_native_diagnostic_observer(const char*) __attribute__((weak));
#include <cstring>
extern "C" int32_t risc_native_diagnostic_read(uint32_t,char*,uint32_t,uint32_t*,uint64_t*,uint32_t*) __attribute__((weak));
// Runs after output has released its guard, even when USB drops the line.
// Bounded native-only storage work; no Runtime, provider or diagnostic calls.
extern "C" void risc_native_diagnostic_drain(void) __attribute__((weak));
#endif
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
bool outputting=false,unfinishedLine=false;
#if RISC_STAGE_LOGS
uint32_t lostLines=0,truncatedLines=0,reportedLost=0,reportedTruncated=0;
void noteLost(){if(lostLines!=UINT32_MAX)++lostLines;}
void noteTruncation(){if(truncatedLines!=UINT32_MAX)++truncatedLines;}
#else
void noteLost(){}
void noteTruncation(){}
#endif
struct OutputGuard {
  OutputGuard(){outputting=true;}
  ~OutputGuard(){outputting=false;}
};
bool ours(){return owner && owner==xTaskGetCurrentTaskHandle();}
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
bool draining=false;
char sourceContext;
struct NativeStorageDrain {
  ~NativeStorageDrain(){
    if(!risc_native_diagnostic_drain)return;
    draining=true;risc_native_diagnostic_drain();draining=false;
  }
};
int32_t readSource(void* context,uint32_t slot,char* out,uint32_t capacity,
                   uint32_t* written,uint64_t* sequence,uint32_t* revision){
  const auto clear=[&](){
    if(out && capacity)std::memset(out,0,capacity<=RISC_DIAGNOSTIC_SOURCE_TEXT_MAX?capacity:1);
    if(written)*written=0;
    if(sequence)*sequence=0;
    if(revision)*revision=0;
  };
  clear();
  if(!ours() || outputting || context!=&sourceContext || !out || !capacity ||
     capacity>RISC_DIAGNOSTIC_SOURCE_TEXT_MAX || !written || !sequence || !revision ||
     slot>=RISC_DIAGNOSTIC_SOURCE_MAX_SLOTS || !risc_native_diagnostic_read)
    return RISC_DIAGNOSTIC_SOURCE_INVALID;
  OutputGuard guard;
  const int32_t status=risc_native_diagnostic_read(slot,out,capacity,written,sequence,revision);
  if(status==RISC_DIAGNOSTIC_SOURCE_RECORD && *written<capacity && *revision &&
     out[*written]=='\0' && std::strlen(out)==*written)return status;
  clear();
  return status==RISC_DIAGNOSTIC_SOURCE_ABSENT?status:RISC_DIAGNOSTIC_SOURCE_INVALID;
}
#endif
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
#if RISC_STAGE_LOGS
    prepareSerial(); // end() freed the previous diagnostic TX allocation.
#endif
    Serial.begin(115200);Serial.setTxTimeoutMs(0);
    // Pinned begin() returns void. Missing RX queue returns -1; missing TX
    // queue/mutex returns zero. Interrupt allocation failure calls end(). A
    // fresh sole-producer TX ring is empty, irrespective of host connection.
    return Serial.available()>=0 && Serial.availableForWrite()>0;
  }
#endif
} transport;
bool finishLine(){
  if(!unfinishedLine)return true;
  if(!transport.writable())return false;
  const uint8_t newline='\n';
  if(transport.write(&newline,1)!=1)return false;
  unfinishedLine=false;return true;
}
bool reportLoss(){
#if RISC_STAGE_LOGS
  if(lostLines==reportedLost && truncatedLines==reportedTruncated)return false;
  const auto lost=lostLines,truncated=truncatedLines;
  char text[64];
  const int n=std::snprintf(text,sizeof(text),"RTE_LOG lost=%lu truncated=%lu\n",
                          (unsigned long)lost,(unsigned long)truncated);
  if(n<=0 || size_t(n)>=sizeof(text) || transport.writable()<size_t(n))return true;
  size_t sent=0;
  while(sent<size_t(n)){
    size_t count=size_t(n)-sent;
    const auto available=transport.writable();if(count>available)count=available;
    if(!count)break;
    size_t written=transport.write(reinterpret_cast<const uint8_t*>(text)+sent,count);
    if(written>count)written=count;
    if(!written)break;
    sent+=written;
  }
  unfinishedLine=sent>0 && sent<size_t(n);
  if(sent==size_t(n)){reportedLost=lost;reportedTruncated=truncated;}
  return true;
#else
  return false;
#endif
}
}
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
const risc_diagnostic_source_api_v1* nativeSource(){
  static const risc_diagnostic_source_api_v1 source={RISC_DIAGNOSTIC_SOURCE_API_V1,sizeof(source),&sourceContext,readSource};
  return risc_native_diagnostic_read?&source:nullptr;
}
#endif
#if RISC_STAGE_LOGS && RISC_HWCDC_SERIAL
bool prepareSerial(){
  // Reuse the pinned driver's own bounded ring for startup bursts. Allocation
  // failure preserves begin()'s ordinary 256-byte fallback, never a host wait.
  return Serial.setTxBufferSize(8192)==8192;
}
#endif
void start(){
  owner=xTaskGetCurrentTaskHandle();Serial.setTxTimeoutMs(0);
  outputting=unfinishedLine=false;
#if RISC_STAGE_LOGS
  lostLines=truncatedLines=reportedLost=reportedTruncated=0;
#endif
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
  if(!ours() || outputting)return;
  OutputGuard guard;
#if RISC_HWCDC_SLEEP_RECOVERY
  if(!recovery.poll(transport))return;
#endif
  if(!transport.connected()){
#if RISC_SLEEP_DIAGNOSTICS
    replay.disconnect();
#endif
#if RISC_PERFORMANCE_TRACE
    performanceReplay.disconnect();
#endif
    return;
  }
  // A zero write may truncate a best-effort live line. Terminate it before a
  // command dump or another line; never retain or retry its abandoned payload.
  if(unfinishedLine){finishLine();return;}
#if RISC_PERFORMANCE_TRACE
  if(!performanceReplay.active())
#endif
#if RISC_SLEEP_DIAGNOSTICS
  if(!replay.active())
#endif
  if(reportLoss())return;
#if RISC_SLEEP_DIAGNOSTICS || RISC_PERFORMANCE_TRACE
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
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
  if(draining){noteLost();return;}
#endif
  if(outputting){noteLost();return;}
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
  NativeStorageDrain drain;
#endif
  OutputGuard guard;
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
  if(risc_native_diagnostic_observer)risc_native_diagnostic_observer(text);
#endif
#if RISC_SLEEP_DIAGNOSTICS
  message(journal,millis(),text);
  if(replay.active()){noteLost();return;}
#endif
#if RISC_PERFORMANCE_TRACE
  if(performanceReplay.active()){noteLost();return;}
#endif
#if RISC_HWCDC_SLEEP_RECOVERY
  if(!recovery.ready()){noteLost();return;}
#endif
  // Live output is best effort, at most 256 bytes. No wait for a missing host
  // or unavailable capacity. Each owner write is at most 64 bytes.
  // HWCDC can accept bytes into its preallocated ring before the host opens
  // CDC. Keep driving its connection probe, but do not discard stage-build
  // startup text just because that handshake has not completed. The full-line
  // and per-write capacity checks below prevent HWCDC's disconnected FIFO
  // path from evicting older bytes. No additional queue or host wait is used.
  const bool connected=transport.connected();
#if !(RISC_STAGE_LOGS && RISC_HWCDC_SERIAL)
  if(!connected){noteLost();return;}
#else
  (void)connected;
#endif
  char bytes[256];size_t n=0;
  while(n<sizeof(bytes)-1 && text[n]){bytes[n]=text[n];++n;}
  if(n==sizeof(bytes)-1 && text[n])noteTruncation();
  bytes[n++]='\n';
  if(!finishLine() || transport.writable()<n){noteLost();return;}
  size_t offset=0;
  while(offset<n){
    size_t count=n-offset;if(count>64)count=64;
    const size_t available=transport.writable();if(count>available)count=available;
    if(!count){if(offset)noteTruncation();else noteLost();return;}
    size_t written=transport.write(reinterpret_cast<const uint8_t*>(bytes)+offset,count);
    if(written>count)written=count;
    if(!written){if(offset)noteTruncation();else noteLost();return;}
    offset+=written;unfinishedLine=offset<n;
  }
}
#if RISC_STAGE_LOGS
uint64_t monotonicUs(){return ours()?uint64_t(esp_timer_get_time()):0;}
void timestamped(const char* format,...){
  if(!ours() || !format)return;
  if(outputting){noteLost();return;}
  // Timestamp the actual call boundary, before formatting and best-effort USB.
  const auto us=esp_timer_get_time();
  char text[256];
  const int prefix=std::snprintf(text,sizeof(text),"RTE_STAGE us=%llu ",
                               static_cast<unsigned long long>(us));
  if(prefix<0 || size_t(prefix)>=sizeof(text))return;
  va_list args;va_start(args,format);
  const int n=std::vsnprintf(text+prefix,sizeof(text)-size_t(prefix),format,args);
  va_end(args);
  if(n<0)return;
  if(size_t(n)>=sizeof(text)-size_t(prefix)){
    noteTruncation();
    text[sizeof(text)-4]='.';text[sizeof(text)-3]='.';text[sizeof(text)-2]='.';
  }
  // One statement stays one line even if a diagnostic subject contains CR/LF.
  for(char* p=text;*p;++p)if(*p=='\r' || *p=='\n')*p=' ';
  line(text);
}
#endif
void lightEnter(){
#if RISC_SLEEP_DIAGNOSTICS
  if(ours())event(journal,millis(),LightEnter);
#endif
}
void lightReturn(int32_t result,uint32_t cause){
  if(!ours())return;
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER && !RISC_SLEEP_DIAGNOSTICS && !RISC_HWCDC_SLEEP_RECOVERY
  (void)result;
#endif
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
