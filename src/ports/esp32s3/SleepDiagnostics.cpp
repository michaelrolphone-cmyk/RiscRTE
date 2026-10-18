#include "SleepDiagnostics.h"
#if RISC_DIAGNOSTIC_ADAPTER
#define RISC_NATIVE_LOG_RETENTION (RISC_NATIVE_ESP_IDF && RISC_STAGE_LOGS && RISC_HWCDC_SERIAL)
#if RISC_NATIVE_LOG_RETENTION
#include "diagnostics/LiveLogQueue.h"
#endif
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
#include "NativePlatform.h"
#if RISC_NATIVE_SERIAL_FILES
#include "NativeSerialFiles.h"
#endif
#include <esp_sleep.h>
#if RISC_NATIVE_ESP_IDF && RISC_HWCDC_SLEEP_RECOVERY
#include <esp_idf_version.h>
#include <driver/usb_serial_jtag.h>
#endif
#if RISC_ENABLE_USB_PHY || (RISC_NATIVE_ESP_IDF && RISC_HWCDC_SLEEP_RECOVERY)
#include <hal/usb_serial_jtag_ll.h>
#include <soc/rtc_cntl_struct.h>
#if ESP_IDF_VERSION_MAJOR >= 6
#include <esp_private/periph_ctrl.h>
#endif
#endif
#include <cstdio>
#if RISC_NATIVE_ESP_IDF && (RISC_SLEEP_DIAGNOSTICS || RISC_PERFORMANCE_TRACE || RISC_NATIVE_LOG_RETENTION)
#include <esp_attr.h>
#include <new>
#include <type_traits>
#endif
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
extern "C" int32_t risc_native_diagnostic_read_after(uint64_t,char*,uint32_t,uint32_t*,uint64_t*) __attribute__((weak));
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
#endif
#if RISC_NATIVE_ESP_IDF && (RISC_SLEEP_DIAGNOSTICS || RISC_PERFORMANCE_TRACE)
// Only copied replay/checkpoint payload lives here. All accesses are ordinary
// owner-task calls after start(), never ISR/panic/cache-disabled callbacks.
// The SDK maps, zeros and excludes external BSS from its heap before app_main;
// with this selection it aborts startup if PSRAM cannot be initialized.
struct ReplayWorkspace {
#if RISC_SLEEP_DIAGNOSTICS
  Replay sleep;
  Checkpoint checkpoint;
  SerialStatSnapshot stat{};
#endif
#if RISC_PERFORMANCE_TRACE
  RiscPerf::Replay performance;
#endif
};
static_assert(std::is_trivially_destructible<ReplayWorkspace>::value,
              "Restarted diagnostic storage must not own external resources");
alignas(ReplayWorkspace)
#if CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#if !CONFIG_SPIRAM_BOOT_INIT
#error "External diagnostic workspace requires SDK PSRAM boot initialization"
#endif
EXT_RAM_BSS_ATTR
#endif
unsigned char replayStorage[sizeof(ReplayWorkspace)];
ReplayWorkspace& replayWorkspace(){
  return *std::launder(reinterpret_cast<ReplayWorkspace*>(replayStorage));
}
#if RISC_SLEEP_DIAGNOSTICS
Replay& sleepReplay(){return replayWorkspace().sleep;}
Checkpoint& checkpointState(){return replayWorkspace().checkpoint;}
SerialStatSnapshot& statState(){return replayWorkspace().stat;}
#endif
#if RISC_PERFORMANCE_TRACE
RiscPerf::Replay& perfReplay(){return replayWorkspace().performance;}
#endif
#else
// Preserve legacy placement and static initialization exactly.
#if RISC_SLEEP_DIAGNOSTICS
Replay replay;
Checkpoint lastCheckpoint;
SerialStatSnapshot lastStat{};
Replay& sleepReplay(){return replay;}
Checkpoint& checkpointState(){return lastCheckpoint;}
SerialStatSnapshot& statState(){return lastStat;}
#endif
#if RISC_PERFORMANCE_TRACE
RiscPerf::Replay performanceReplay;
RiscPerf::Replay& perfReplay(){return performanceReplay;}
#endif
#endif
#if RISC_HWCDC_SLEEP_RECOVERY
UsbSleepRecovery recovery;
#endif
#if RISC_NATIVE_LOG_RETENTION
// A native stage build retains startup/transfer diagnostics in a bounded queue.
// No ISR, panic/cache-disabled access, heap allocation or RTC enlargement.
#if CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#if !CONFIG_SPIRAM_BOOT_INIT
#error "External live log queue requires SDK PSRAM boot initialization"
#endif
using NativeLogQueue=LiveLogQueue<32768>;
#else
using NativeLogQueue=LiveLogQueue<4096>;
#endif
static_assert(std::is_trivially_destructible<NativeLogQueue>::value, "Live log storage owns no resources");
alignas(NativeLogQueue)
#if CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
EXT_RAM_BSS_ATTR
#endif
unsigned char liveLogStorage[sizeof(NativeLogQueue)];
NativeLogQueue& liveLogs(){return *std::launder(reinterpret_cast<NativeLogQueue*>(liveLogStorage));}
#endif
TaskHandle_t owner=nullptr;
bool outputting=false,unfinishedLine=false;
#if RISC_NATIVE_SERIAL_FILES
bool serialConnected=false;
#endif
#if RISC_ENABLE_USB_PHY
bool usbFenced=false;
#endif
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
#if RISC_NATIVE_ESP_IDF && RISC_HWCDC_SLEEP_RECOVERY && ESP_IDF_VERSION_MAJOR >= 6
bool nativeSerialModuleEnabled(){
  bool enabled=false;
  PERIPH_RCC_ATOMIC(){enabled=usb_serial_jtag_ll_module_is_enabled();}
  return enabled;
}
bool nativeSerialDetach(){
  // Called only after the same-core Console has released its driver. IDF6
  // uninstall intentionally preserves the pads for stdout, so explicitly
  // detach here to preserve the existing checked 20 ms recovery boundary.
  if(usb_serial_jtag_is_driver_installed())return false;
  PERIPH_RCC_ATOMIC(){
    usb_serial_jtag_ll_enable_bus_clock(true);
    usb_serial_jtag_ll_reset_register();
  }
  usb_serial_jtag_ll_phy_enable_pad(false);
  return nativeSerialModuleEnabled() && !usb_serial_jtag_ll_phy_is_pad_enabled();
}
bool nativeSerialRouted(){
  return nativeSerialModuleEnabled() && RTCCNTL.usb_conf.sw_hw_usb_phy_sel &&
    !RTCCNTL.usb_conf.sw_usb_phy_sel && !USB_SERIAL_JTAG.conf0.phy_sel &&
    !USB_SERIAL_JTAG.conf0.pad_pull_override && USB_SERIAL_JTAG.conf0.dp_pullup &&
    usb_serial_jtag_ll_phy_is_pad_enabled();
}
#endif
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
bool draining=false;
char sourceContext;
struct NativeStorageDrain {
  ~NativeStorageDrain(){
    if(!risc_native_diagnostic_drain)return;
#if RISC_ENABLE_USB_PHY
    if(usbFenced)return;
#endif
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
int32_t readAfter(void* context,uint64_t after,char* out,uint32_t capacity,
                  uint32_t* written,uint64_t* next){
  const auto clear=[&](){
    if(out && capacity)std::memset(out,0,capacity<=RISC_DIAGNOSTIC_SOURCE_TEXT_MAX?capacity:1);
    if(written)*written=0;
    if(next)*next=0;
  };
  clear();
  if(!ours() || outputting || context!=&sourceContext || !out || !capacity ||
     capacity>RISC_DIAGNOSTIC_SOURCE_TEXT_MAX || !written || !next ||
     !risc_native_diagnostic_read_after)return RISC_DIAGNOSTIC_SOURCE_INVALID;
  OutputGuard guard;
  const int32_t status=risc_native_diagnostic_read_after(after,out,capacity,written,next);
  if(status==RISC_DIAGNOSTIC_SOURCE_RECORD && *written && *written<capacity &&
     *next>after && *next-after==*written && out[*written]=='\0' &&
     out[*written-1]=='\n' && std::strlen(out)==*written)return status;
  clear();
  return status==RISC_DIAGNOSTIC_SOURCE_ABSENT?status:RISC_DIAGNOSTIC_SOURCE_INVALID;
}
#endif
// Must remain the sole HWCDC ring producer: no setDebugOutput or concurrent
// Serial writer. Pinned HWCDC can underflow a zero timeout without capacity.
struct Transport {
  bool connected(){return bool(RiscPlatform::console);}
  size_t writable(){int n=RiscPlatform::console.availableForWrite();return n>0?size_t(n):0;}
  size_t write(const uint8_t* p,size_t n){return RiscPlatform::console.write(p,n);}
#if RISC_HWCDC_SLEEP_RECOVERY
  uint32_t nowMs(){return RiscPlatform::millis();}
#if RISC_NATIVE_ESP_IDF && ESP_IDF_VERSION_MAJOR >= 6
  bool detached=false;
#endif
  void end(){
    RiscPlatform::console.end();
#if RISC_NATIVE_ESP_IDF && ESP_IDF_VERSION_MAJOR >= 6
    detached=RiscPlatform::console.available()<0 && RiscPlatform::console.availableForWrite()==0;
    if(detached)detached=nativeSerialDetach();
#endif
  }
  bool begin(){
#if RISC_NATIVE_ESP_IDF && ESP_IDF_VERSION_MAJOR >= 6
    if(!detached || RiscPlatform::console.available()>=0 ||
       RiscPlatform::console.availableForWrite()!=0 || usb_serial_jtag_ll_phy_is_pad_enabled())return false;
    usb_serial_jtag_ll_phy_enable_external(false);
    usb_serial_jtag_ll_phy_disable_pull_override();
    USB_SERIAL_JTAG.conf0.dp_pullup=1;
#endif
#if RISC_STAGE_LOGS
    prepareSerial(); // end() freed the previous diagnostic TX allocation.
#endif
    RiscPlatform::console.begin(115200);RiscPlatform::console.setTxTimeoutMs(0);
    // Pinned begin() returns void. Missing RX queue returns -1; missing TX
    // queue/mutex returns zero. Interrupt allocation failure calls end(). A
    // fresh sole-producer TX ring is empty, irrespective of host connection.
    const bool initialized=RiscPlatform::console.available()>=0 && RiscPlatform::console.availableForWrite()>0;
#if RISC_NATIVE_ESP_IDF && ESP_IDF_VERSION_MAJOR >= 6
    // The official driver enables the selected PHY pads on install. Read back
    // routing as well as allocation readiness before clearing a failure fence.
    if(!initialized || !nativeSerialRouted()){
      RiscPlatform::console.end();
      if(!usb_serial_jtag_is_driver_installed())usb_serial_jtag_ll_phy_enable_pad(false);
      return false;
    }
    detached=false;
#endif
    return initialized;
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
  static const risc_diagnostic_source_api_v1_trace trace={
    {RISC_DIAGNOSTIC_SOURCE_API_V1,sizeof(trace),&sourceContext,readSource},readAfter};
  return risc_native_diagnostic_read?(risc_native_diagnostic_read_after?&trace.base:&source):nullptr;
}
#endif
#if RISC_STAGE_LOGS && RISC_HWCDC_SERIAL
bool prepareSerial(){
  // Reuse the pinned driver's own bounded ring for startup bursts. Allocation
  // failure preserves begin()'s ordinary 256-byte fallback, never a host wait.
  return RiscPlatform::console.setTxBufferSize(8192)==8192;
}
#endif
#if RISC_SLEEP_DIAGNOSTICS
void serialStat(uint32_t sid,uint32_t seq,uint64_t ns,int32_t status,const RiscBoot::StatObservation& decision){
  if(!ours() || xPortInIsrContext())return;
  observeStat(statState(),RiscPlatform::millis(),sid,seq,ns,status,decision);
}
void serialControl(uint32_t session,uint32_t sequence,const char*operation,uint32_t waited,bool deadline,uint32_t state){
  if(!ours() || xPortInIsrContext() || outputting || !operation)return;
  const char*names[]={"HELLO","NS","PATH","STAT","GET","PUT","READ","WRITE","COMMIT","CLOSE","ABORT","MKDIR"};
  uint32_t op=0;for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i)if(!std::strcmp(operation,names[i])){op=i+1;break;}
  const auto ms=RiscPlatform::millis();
  event(journal,ms,deadline?SerialDeadlineIdentity:SerialFirstIdentity,int32_t(session),sequence);
  event(journal,ms,deadline?SerialDeadlineState:SerialFirstState,int32_t(op),state);
  event(journal,ms,deadline?SerialDeadlineWait:SerialFirstWait,int32_t(waited),0);
}
int32_t checkpoint(const char* app,uint64_t invocation,const char* text,uint32_t length){
  if(!ours() || xPortInIsrContext() || outputting)return -1;
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
  if(draining)return -1;
#endif
  // Intentionally permitted while console/PHY is fenced. RAM copy only; no
  // native observer/drain, Serial access, allocation or provider callbacks.
  return captureCheckpoint(checkpointState(),journal.boot,RiscPlatform::millis(),app,invocation,text,length);
}
#endif
void start(){
#if RISC_NATIVE_ESP_IDF && (RISC_SLEEP_DIAGNOSTICS || RISC_PERFORMANCE_TRACE)
  // BSS alone cannot preserve Replay's nonzero snapshot defaults. Construct
  // in-place before publishing the owner, with no heap allocation or fallback.
  owner=nullptr;
  new (replayStorage) ReplayWorkspace;
#endif
#if RISC_NATIVE_LOG_RETENTION
  owner=nullptr;new (liveLogStorage) NativeLogQueue;
#endif
  owner=xTaskGetCurrentTaskHandle();RiscPlatform::console.setTxTimeoutMs(0);
  outputting=unfinishedLine=false;
#if RISC_NATIVE_SERIAL_FILES
  serialConnected=false;
#endif
#if RISC_STAGE_LOGS
  lostLines=truncatedLines=reportedLost=reportedTruncated=0;
#endif
#if RISC_PERFORMANCE_TRACE
  perfReplay().disconnect();
#endif
#if RISC_HWCDC_SLEEP_RECOVERY
  recovery.reset();
#endif
#if RISC_SLEEP_DIAGNOSTICS
  sleepReplay().disconnect();checkpointState()={};statState()={};
  const auto reset=esp_reset_reason();
  const bool retain=reset==ESP_RST_SW || reset==ESP_RST_DEEPSLEEP || reset==ESP_RST_PANIC ||
    reset==ESP_RST_INT_WDT || reset==ESP_RST_TASK_WDT || reset==ESP_RST_WDT;
  begin(journal,retain,RiscPlatform::millis(),int32_t(reset),uint32_t(esp_sleep_get_wakeup_cause()));
#endif
}
#if RISC_ENABLE_USB_PHY
bool usbPhyIdle(){return ours() && !usbFenced;}
bool suspendUsbPhy(){
  if(!ours() || outputting || usbFenced)return false;
#if RISC_NATIVE_SERIAL_FILES
  // A PHY claimant cannot strand an accepted request or file handle. Refusal
  // is temporary; only a settled Runtime checkpoint may cancel/close it.
  if(!RiscCpu::NativeSerialFiles::exitSafe())return false;
#endif
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
  if(draining)return false;
#endif
  // Fence recovery, live output and native storage drains before HWCDC end.
  // The pinned end disables/clears its IRQ and removes RX/TX resources.
  usbFenced=true;OutputGuard guard;
#if RISC_NATIVE_SERIAL_FILES
  RiscCpu::NativeSerialFiles::disconnect();serialConnected=false;
#endif
#if RISC_NATIVE_ESP_IDF && ESP_IDF_VERSION_MAJOR >= 6
  transport.end();
#else
  RiscPlatform::console.end();
#endif
  unfinishedLine=false;
#if RISC_SLEEP_DIAGNOSTICS
  sleepReplay().disconnect();
#endif
#if RISC_PERFORMANCE_TRACE
  perfReplay().disconnect();
#endif
  return ours() && RiscPlatform::console.available()<0 && RiscPlatform::console.availableForWrite()==0
#if RISC_NATIVE_ESP_IDF && ESP_IDF_VERSION_MAJOR >= 6
    && transport.detached
#endif
    ;
}
bool resumeUsbPhy(){
  if(!ours() || outputting || !usbFenced)return false;
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
  if(draining)return false;
#endif
  OutputGuard guard;
  // Only the provider's checked stop/remount reaches this call. HWCDC.begin()
  // recreates its queues and interrupt, but does not reset the fixed-function
  // USB device or select its internal PHY in RTC_CNTL. Reclaim both here;
  // allocated queues alone do not establish that serial can reach the pads.
#if RISC_NATIVE_ESP_IDF && ESP_IDF_VERSION_MAJOR >= 6
  transport.end();
  if(!ours() || !transport.detached)return false;
  RiscPlatform::delay(20);
  if(!ours())return false;
  const bool initialized=transport.begin();
  const bool routed=nativeSerialRouted();
#else
  RiscPlatform::console.end();
  if(!ours() || RiscPlatform::console.available()>=0 || RiscPlatform::console.availableForWrite()!=0)return false;
  usb_serial_jtag_ll_enable_bus_clock(true);
  usb_serial_jtag_ll_reset_register();
  usb_serial_jtag_ll_enable_pad(false);
  if(!usb_serial_jtag_ll_module_is_enabled() || USB_SERIAL_JTAG.conf0.usb_pad_enable)return false;
  RiscPlatform::delay(20);
  if(!ours())return false;
  // Same internal-JTAG selection as IDF 4.4.7 usb_phy_ll_int_jtag_enable.
  // That C-only header also contains volatile USB_WRAP struct copies which
  // cannot be included by this C++ adapter.
  USB_SERIAL_JTAG.conf0.phy_sel=0;
  USB_SERIAL_JTAG.conf0.pad_pull_override=0;
  USB_SERIAL_JTAG.conf0.dp_pullup=1;
  USB_SERIAL_JTAG.conf0.usb_pad_enable=1;
  RTCCNTL.usb_conf.sw_hw_usb_phy_sel=1;
  RTCCNTL.usb_conf.sw_usb_phy_sel=0;
  const bool initialized=transport.begin();
  const bool routed=usb_serial_jtag_ll_module_is_enabled() &&
    RTCCNTL.usb_conf.sw_hw_usb_phy_sel && !RTCCNTL.usb_conf.sw_usb_phy_sel &&
    !USB_SERIAL_JTAG.conf0.phy_sel && !USB_SERIAL_JTAG.conf0.pad_pull_override &&
    USB_SERIAL_JTAG.conf0.dp_pullup && USB_SERIAL_JTAG.conf0.usb_pad_enable;
#endif
  if(!ours() || !initialized || !routed){
    RiscPlatform::console.end();
#if RISC_NATIVE_ESP_IDF && ESP_IDF_VERSION_MAJOR >= 6
    if(!usb_serial_jtag_is_driver_installed())usb_serial_jtag_ll_phy_enable_pad(false);
#else
    usb_serial_jtag_ll_enable_pad(false);
#endif
    return false;
  }
#if RISC_HWCDC_SLEEP_RECOVERY
  recovery.reset();
#endif
  if(!ours())return false;
  unfinishedLine=false;usbFenced=false;return true;
}
#endif
void poll(){
#if RISC_NATIVE_LOG_RETENTION
  if(xPortInIsrContext())return;
#endif
  if(!ours() || outputting)return;
#if RISC_ENABLE_USB_PHY
  if(usbFenced)return;
#endif
  OutputGuard guard;
#if RISC_HWCDC_SLEEP_RECOVERY
  if(!recovery.poll(transport))return;
#endif
  if(!transport.connected()){
#if RISC_NATIVE_LOG_RETENTION
    liveLogs().disconnected();
#endif
#if RISC_NATIVE_SERIAL_FILES
    if(serialConnected)RiscCpu::NativeSerialFiles::disconnect();
    serialConnected=false;
#endif
#if RISC_SLEEP_DIAGNOSTICS
    sleepReplay().disconnect();
#endif
#if RISC_PERFORMANCE_TRACE
    perfReplay().disconnect();
#endif
    return;
  }
#if RISC_NATIVE_SERIAL_FILES
  serialConnected=true;
#endif
#if !RISC_NATIVE_LOG_RETENTION
  // A zero write may truncate a best-effort live line. Terminate it before a
  // command dump or another line; never retain or retry its abandoned payload.
  if(unfinishedLine){finishLine();return;}
#if RISC_PERFORMANCE_TRACE
  if(!perfReplay().active())
#endif
#if RISC_SLEEP_DIAGNOSTICS
  if(!sleepReplay().active())
#endif
#if RISC_NATIVE_SERIAL_FILES
  if(!RiscCpu::NativeSerialFiles::pending())
#endif
  if(reportLoss())return;
#endif
#if RISC_SLEEP_DIAGNOSTICS || RISC_PERFORMANCE_TRACE || RISC_NATIVE_SERIAL_FILES
  for(unsigned i=0;i<(RISC_NATIVE_SERIAL_FILES?64u:16u) && RiscPlatform::console.available()>0;++i){
    int c=RiscPlatform::console.read();if(c<0)break;
#if RISC_NATIVE_SERIAL_FILES
    RiscCpu::NativeSerialFiles::input(uint8_t(c));
    if(RiscCpu::NativeSerialFiles::pending()){
      // A file frame wins only after its complete protocol prefix. Terminate
      // any partly emitted diagnostic replay before the first response byte.
#if RISC_SLEEP_DIAGNOSTICS
      if(sleepReplay().active())unfinishedLine=true;
      sleepReplay().disconnect();
#endif
#if RISC_PERFORMANCE_TRACE
      if(perfReplay().active())unfinishedLine=true;
      perfReplay().disconnect();
#endif
      continue;
    }
#endif
#if RISC_SLEEP_DIAGNOSTICS
#if RISC_PERFORMANCE_TRACE
    if(!perfReplay().active())
#endif
      sleepReplay().input(char(c),journal,&checkpointState(),&statState());
#endif
#if RISC_PERFORMANCE_TRACE
#if RISC_SLEEP_DIAGNOSTICS
    if(!sleepReplay().active())
#endif
      perfReplay().input(char(c));
#endif
#if RISC_SLEEP_DIAGNOSTICS && RISC_PERFORMANCE_TRACE
    // The winning command's newline must also discard the other parser's
    // prefix; otherwise the next request would concatenate two commands.
    if(sleepReplay().active())perfReplay().disconnect();
    else if(perfReplay().active())sleepReplay().disconnect();
#endif
  }
#endif
#if RISC_NATIVE_LOG_RETENTION
  // Ingress above is independent of TX pressure. Finish only an already-started
  // diagnostic line before a reply; never begin another queued line first.
  if(liveLogs().partial()){liveLogs().poll(transport);return;}
#endif
#if RISC_NATIVE_SERIAL_FILES
  if(RiscCpu::NativeSerialFiles::pending()){
    if(unfinishedLine){finishLine();return;}
    size_t count=RiscCpu::NativeSerialFiles::outputSize();
    if(count>64)count=64;
    const auto available=transport.writable();if(count>available)count=available;
    if(count){
      size_t written=transport.write(reinterpret_cast<const uint8_t*>(RiscCpu::NativeSerialFiles::output()),count);
      if(written>count)written=count;
      RiscCpu::NativeSerialFiles::consumeOutput(written);
    }
    return;
  }
#endif
#if RISC_NATIVE_LOG_RETENTION
  if(unfinishedLine){finishLine();return;}
#endif
#if RISC_PERFORMANCE_TRACE
  if(perfReplay().active()){perfReplay().poll(transport);return;}
#endif
#if RISC_SLEEP_DIAGNOSTICS
  if(sleepReplay().active()){sleepReplay().poll(transport);return;}
#endif
#if RISC_NATIVE_LOG_RETENTION
  if(reportLoss())return;
  liveLogs().poll(transport);
#endif
}
bool providerDiagnosticReady(){
  if(xPortInIsrContext() || !ours() || outputting)return false;
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER
  if(draining)return false;
#endif
  return true; // USB/SD fences suppress drains, not bounded RAM capture.
}
void line(const char* text){
#if RISC_NATIVE_LOG_RETENTION
  if(xPortInIsrContext())return;
#endif
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
  message(journal,RiscPlatform::millis(),text);
#endif
#if RISC_NATIVE_LOG_RETENTION
  char bytes[256];size_t n=0;
  while(n<sizeof(bytes)-1 && text[n]){bytes[n]=text[n];++n;}
  if(n==sizeof(bytes)-1 && text[n])noteTruncation();
  bytes[n++]='\n';
  if(!liveLogs().append(bytes,n))noteLost();
  return; // Only owner poll drains; capture also works behind USB fences.
#else
#if RISC_SLEEP_DIAGNOSTICS
  if(sleepReplay().active()){noteLost();return;}
#endif
#if RISC_PERFORMANCE_TRACE
  if(perfReplay().active()){noteLost();return;}
#endif
#if RISC_ENABLE_USB_PHY
  if(usbFenced){noteLost();return;}
#endif
#if RISC_HWCDC_SLEEP_RECOVERY
  if(!recovery.ready()){noteLost();return;}
#endif
#if RISC_NATIVE_SERIAL_FILES
  if(RiscCpu::NativeSerialFiles::pending()){noteLost();return;}
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
#endif
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
  if(ours())event(journal,RiscPlatform::millis(),LightEnter);
#endif
}
void lightReturn(int32_t result,uint32_t cause){
  if(!ours())return;
#if RISC_NATIVE_DIAGNOSTIC_OBSERVER && !RISC_SLEEP_DIAGNOSTICS && !RISC_HWCDC_SLEEP_RECOVERY
  (void)result;
#endif
#if RISC_SLEEP_DIAGNOSTICS
  event(journal,RiscPlatform::millis(),LightReturn,result,cause);
#else
  (void)cause;
#endif
#if RISC_HWCDC_SLEEP_RECOVERY
  if(result==ESP_OK){
#if RISC_NATIVE_LOG_RETENTION
    liveLogs().disconnected();
#endif
#if RISC_NATIVE_SERIAL_FILES
    RiscCpu::NativeSerialFiles::disconnect();serialConnected=false;
#endif
#if RISC_SLEEP_DIAGNOSTICS
    sleepReplay().disconnect();
#endif
#if RISC_PERFORMANCE_TRACE
    perfReplay().disconnect();
#endif
    recovery.request(); // RAM only; wake-source cleanup runs before any USB I/O.
  }
#endif
}
void deepEnter(){
#if RISC_SLEEP_DIAGNOSTICS
  if(ours())event(journal,RiscPlatform::millis(),DeepEnter);
#endif
}
}
#endif
