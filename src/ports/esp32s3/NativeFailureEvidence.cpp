#include "NativeFailureEvidence.h"
#if RISC_NATIVE_FAILURE_EVIDENCE
// Capture must not gain compiler-generated memset/stack-check call edges.
#pragma GCC optimize ("no-stack-protector", "no-tree-loop-distribute-patterns")
#include "runtime/diagnostics/FailureEvidenceStore.h"
#include "runtime/diagnostics/FailureBacktrace.h"
#include <esp_attr.h>
#include <esp_idf_version.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_debug_helpers.h>
#include <esp_private/panic_internal.h>
#include <esp_private/system_internal.h>
#include <xtensa/xtensa_context.h>
#include <soc/soc_memory_types.h>
#include <soc/cpu.h>
#include <cstddef>
extern "C" { __attribute__((used,visibility("default"))) extern const uint32_t risc_native_failure_evidence_abi=1; }
#if !CONFIG_IDF_TARGET_ESP32S3 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(4,4,7)
#error "Native failure capture requires the audited ESP32-S3 IDF 4.4.7 ABI"
#endif
static_assert(sizeof(panic_info_t)==36 && offsetof(panic_info_t,frame)==28 &&
  offsetof(panic_info_t,pseudo_excause)==32,"pinned panic_info_t changed");
static_assert(sizeof(XtExcFrame)==112 && offsetof(XtExcFrame,pc)==4 &&
  offsetof(XtExcFrame,a0)==12 && offsetof(XtExcFrame,a1)==16 &&
  offsetof(XtExcFrame,exccause)==80 && offsetof(XtExcFrame,excvaddr)==84,"pinned XtExcFrame changed");
namespace {
using namespace RiscFailureEvidence;
RTC_NOINIT_ATTR Image crashImage;
DRAM_ATTR State crashState;
DRAM_ATTR volatile uint32_t panicEntered;
// No PSRAM, flash, peripheral, RTC-heap or data-cache reads. Accept only a
// complete aligned range in the pinned chip's addressable internal SRAM.
bool IRAM_ATTR internalRange(uintptr_t address,uint32_t size,uint32_t alignment) {
  constexpr uintptr_t low=SOC_DRAM_LOW;
  constexpr uintptr_t high=SOC_DRAM_HIGH-CONFIG_ESP32S3_DATA_CACHE_SIZE;
  return RiscFailureBacktrace::internalRange(address,size,alignment,low,high);
}
Reset resetClass(esp_reset_reason_t reset) {
  switch(reset) {
    case ESP_RST_POWERON:return Reset::PowerOn;
    case ESP_RST_BROWNOUT:return Reset::Brownout;
    case ESP_RST_SW:return Reset::Software;
    case ESP_RST_DEEPSLEEP:return Reset::DeepSleep;
    case ESP_RST_PANIC:return Reset::Panic;
    case ESP_RST_INT_WDT:return Reset::InterruptWatchdog;
    case ESP_RST_TASK_WDT:return Reset::TaskWatchdog;
    case ESP_RST_WDT:return Reset::Watchdog;
    default:return Reset::Unknown;
  }
}
int32_t copiedRead(risc_failure_evidence_v1* out){return read(crashImage,crashState,*out);}
int32_t acknowledgeRecord(uint32_t boot,uint32_t sequence){return acknowledge(crashImage,crashState,boot,sequence);}
void stage(const char* app,uint64_t invocation,uint32_t phase,uint32_t role){
  (void)breadcrumb(crashImage,crashState,app,invocation,phase,role);
}
void IRAM_ATTR retain(int32_t status,const char* detail){(void)captureRetention(crashImage,crashState,status,detail);}
}
extern "C" void __real_esp_panic_handler(panic_info_t*);
extern "C" void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t* info) {
  // Native boot readiness excludes pre-setup/previous-boot pointer state.
  // SDK has stalled peer cores before this call. Never wait for a lock here.
  if(crashState.ready && !panicEntered) {
    panicEntered=1;
    asm volatile("memw" ::: "memory");
    Registers registers{};
    registers.core=UINT32_MAX;registers.exception=UINT32_MAX;
    registers.reset_hint=uint32_t(esp_reset_reason_get_hint());
    if(g_panic_abort)registers.flags|=RISC_FAILURE_ABORT;
    const XtExcFrame* frame=nullptr;
    if(internalRange(uintptr_t(info),sizeof(*info),4)) {
      registers.core=uint32_t(info->core);registers.exception=uint32_t(info->exception);
      registers.pseudo_excause=info->pseudo_excause?1u:0u;
      if(g_panic_abort)registers.exception=PANIC_EXCEPTION_ABORT;
      if(internalRange(uintptr_t(info->frame),sizeof(XtExcFrame),4)) {
        frame=static_cast<const XtExcFrame*>(info->frame);
        registers.pc=frame->pc;registers.sp=frame->a1;registers.a0=frame->a0;
        registers.ps=frame->ps;registers.exccause=frame->exccause;registers.excvaddr=frame->excvaddr;
        registers.flags|=RISC_FAILURE_REGISTERS;
      }
    }
    if(capturePanicBase(crashImage,crashState,registers)) {
      risc_failure_frame_v1 frames[RISC_FAILURE_EVIDENCE_FRAMES];
      uint32_t count=0,stop=RISC_FAILURE_STACK_UNSUPPORTED;
      if(frame) {
        const bool firstPc=esp_ptr_executable(reinterpret_cast<void*>(esp_cpu_process_stack_pc(registers.pc))) ||
          (!registers.pseudo_excause && registers.exccause==20u); // InstrFetchProhibited, as SDK
        count=RiscFailureBacktrace::collect({registers.pc,registers.sp,registers.a0},firstPc,
          SOC_DRAM_LOW,SOC_DRAM_HIGH-CONFIG_ESP32S3_DATA_CACHE_SIZE,frames,stop,
          [](RiscFailureBacktrace::Cursor& cursor) __attribute__((always_inline)) -> bool {
            esp_backtrace_frame_t sdk{cursor.pc,cursor.sp,cursor.next_pc,nullptr};
            const bool result=esp_backtrace_get_next_frame(&sdk);
            cursor.pc=sdk.pc;cursor.sp=sdk.sp;cursor.next_pc=sdk.next_pc;return result;
          },[](uint32_t pc) __attribute__((always_inline)) -> uint32_t {return esp_cpu_process_stack_pc(pc);});
      }
      (void)captureExtension(crashImage,crashState,frames,count,stop);
    }
  }
  // Preserve SDK diagnostics, watchdog handling, coredump attempt and restart.
  // Existing SDK console output includes UART/USB; our capture does no I/O.
  __real_esp_panic_handler(info);
}
namespace RiscCpu { namespace NativeFailureEvidence {
void start() {
  const auto reset=esp_reset_reason();
  // The descriptor may live in flash: copy during healthy setup only. Store
  // boot() prestages these bytes in internal DRAM before setting State.ready.
  const auto* app=esp_ota_get_app_description();
  if(!app || app->magic_word!=ESP_APP_DESC_MAGIC_WORD)return;
  uint8_t identityPresent=0;
  for(unsigned i=0;i<sizeof(app->app_elf_sha256);++i)identityPresent|=app->app_elf_sha256[i];
  if(!identityPresent)return; // unfilled ELF descriptor is not firmware identity
  (void)boot(crashImage,crashState,resetClass(reset),uint32_t(reset),app->app_elf_sha256);
}
const RiscBoot::FailureEvidenceBackend* backend() {
  static const RiscBoot::FailureEvidenceBackend table{copiedRead,acknowledgeRecord,stage,retain};
  return &table;
}
}}
#endif
