#include "runtime/drivers/SelectedNativeProviderPolicyV1.h"
#include <Arduino.h>
#include <RiscBuildIdentity.h>
#include <esp_spiffs.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include "bootstrap/Runtime.h"
// Retained by the boot diagnostic reference; available to native packaging
// without exporting any new application or provider ABI.
extern "C" const char risc_app_policy_rows[]=RISC_APP_POLICY_ROWS_MARKER;
#if RISC_APP_REQUIREMENT_ROWS == 17
extern "C" const char risc_app_requirement_rows[]=RISC_APP_REQUIREMENT_ROWS_MARKER;
#endif
#include "ports/esp32s3/CpuPort.h"
#include "ports/esp32s3/NativeRetainedWake.h"
#include "ports/esp32s3/NativeRealtime.h"
#include "ports/esp32s3/NativeBoard.h"
#include "ports/esp32s3/CooperativeDelay.h"
#include "ports/esp32s3/SleepDiagnostics.h"
#include "ports/esp32s3/NativeDiagnosticBinding.h"
#include "ports/esp32s3/NativeFailureEvidence.h"
#if RISC_SLEEP_DIAGNOSTICS
#define RISC_DIAGNOSTIC_CHECKPOINT_BACKEND RiscDiagnostics::checkpoint
#else
#define RISC_DIAGNOSTIC_CHECKPOINT_BACKEND nullptr
#endif
#include <cstdarg>
#include <esp_timer.h>
#include "diagnostics/Performance.h"
#ifndef RISC_PERFORMANCE_TRACE
#define RISC_PERFORMANCE_TRACE 0
#endif
extern "C" void risc_perf_loader_event(uint32_t phase,uint32_t value) {
#if RISC_PERFORMANCE_TRACE
  if(!RiscPerf::allowed())return;
  // Loader is synchronous on the owner task. Pair each nested phase separately;
  // durations are inclusive, never summed into a misleading total.
  static uint64_t starts[3]{};
  static bool live[3]{};
  const unsigned slot=phase==40 || phase==41 ? 0 : phase==42 || phase==43 ? 1 : 2;
  if(phase==40 || phase==42 || phase==45){starts[slot]=RiscPerf::now();live[slot]=true;}
  if(phase==41 || phase==43 || phase==46 || phase==44){
    const uint64_t now=RiscPerf::now();
    for(unsigned i=0;i<3;++i)if(live[i] && (phase==44 || i==slot)){
      RiscPerf::add(RiscPerf::data.durations_us[i==0?40:i==1?42:45],now>=starts[i]?now-starts[i]:0);
      live[i]=false;
    }
  }
  RiscPerf::emit(phase,value);
#else
  (void)phase;(void)value;
#endif
}
#ifdef RISC_PAIRED_APP_DATA
#include "ports/esp32s3/NativeAppData.h"
#endif
#ifdef RISC_PAIRED_BANKS
#include "ports/esp32s3/NativeBankStore.h"
#endif
#if defined(RISC_PAIRED_BANKS) || defined(RISC_RUNTIME_METADATA_PSRAM)
#include "ports/esp32s3/NativeRuntime.h"
#include "ports/esp32s3/NativeBootstrap.h"
#include "ports/esp32s3/NativeSntp.h"
#include "ports/esp32s3/NvsBootstrapInput.h"
#include "ports/esp32s3/OwnerMaintenance.h"
#endif
#ifndef RISC_EMBEDDED_BOOTSTORE
#include "ports/esp32s3/NvsKeyValue.h"
#endif

#ifndef RISC_TARGET
#define RISC_TARGET "esp32s3-baseline"
#endif
#ifdef RISC_EMBEDDED_BOOTSTORE
extern esp_err_t riscrte_mount_embedded_store();
#endif
// Optional native-platform startup status; no product policy or pin mapping.
// A composed platform can report an earlier initVariant failure. No hook keeps
// the existing generic/Watch boot sequence.
extern "C" const char* risc_native_startup_error(void) __attribute__((weak));
namespace {
TaskHandle_t owner=nullptr;
bool isOwner() { return xTaskGetCurrentTaskHandle()==owner; }
bool health(risc_runtime_health_v1* out) {
  out->uptime_ms=millis(); out->free_heap=ESP.getFreeHeap();
  const auto* app=esp_ota_get_running_partition(); out->app_address=app?app->address:0;
  esp_efuse_mac_get_default(out->mac);
  snprintf(out->target,sizeof(out->target),"%s", RISC_TARGET);
  return true;
}
void diagnosticLine(const char* line) {
#if RISC_DIAGNOSTIC_ADAPTER
  RiscDiagnostics::line(line);
#else
  Serial.println(line);
#endif
}
void diagnosticFormat(const char* format,...) {
  char line[256];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);
  diagnosticLine(line);
}
void schedulerDelay(uint32_t ms) {
  vTaskDelay(RiscCpu::cooperativeDelayTicks(ms,configTICK_RATE_HZ));
}
void cooperate(uint32_t ms) {
#if RISC_DIAGNOSTIC_ADAPTER
  RiscDiagnostics::poll();
#endif
  schedulerDelay(ms);
}
bool diagnostic(const char* line) { diagnosticLine(line); return true; }
// Static lifetime intentionally retains manifests, dependency tables and ELF
// mappings after failed quiescence. Never destroy these while hardware is live.
RiscCpu::Port cpu(RiscCpu::nativeHardware(isOwner));
bool bindPlatforms(RiscBoot::Runtime& runtime){
  if(!(cpu.bind(runtime)
#ifdef RISC_PAIRED_BANKS
  && RiscBankStore::bind(runtime)
#endif
  ))return false;
  return RiscDiagnostics::bindNativeSource(runtime);
}
bool appExitSafe(){return cpu.appExitSafe()
#ifdef RISC_PAIRED_BANKS
  && RiscBankStore::exitSafe()
#endif
  ;}
bool providerStorageSafe(){return cpu.providerStorageSafe()
#ifdef RISC_PAIRED_APP_DATA
 && RiscAppData::exitSafe()
#endif
 ;}
bool restartSafe(){return cpu.restartResourcesSafe()
#ifdef RISC_PAIRED_APP_DATA
 && RiscAppData::exitSafe()
#endif
 ;}
bool confirmBoot(){
#ifdef RISC_PAIRED_BANKS
  return cpu.providerStorageSafe() && RiscBankStore::confirmBoot();
#else
  return true;
#endif
}
bool priorFailure(risc_resident_failure_v1* out){
  if(!out || out->struct_size<sizeof(*out))return false;
  const auto reason=esp_reset_reason();
  if(reason!=ESP_RST_PANIC && reason!=ESP_RST_INT_WDT && reason!=ESP_RST_TASK_WDT &&
     reason!=ESP_RST_WDT && reason!=ESP_RST_BROWNOUT)return false;
  *out={};out->struct_size=sizeof(*out);out->kind=RISC_RESIDENT_FAILURE_PRIOR_RESET;
  out->native_reason=uint32_t(reason);
  snprintf(out->detail,sizeof(out->detail),"Previous native reset reason=%u; application identity unavailable",unsigned(reason));
  return true;
}
#if defined(RISC_PAIRED_BANKS) || defined(RISC_RUNTIME_METADATA_PSRAM)
RiscBoot::Runtime* retainedRuntime=nullptr;
#elif defined(RISC_EMBEDDED_BOOTSTORE)
RiscBoot::Runtime runtime({isOwner,health,cooperate,diagnostic,bindPlatforms,nullptr,appExitSafe,providerStorageSafe,confirmBoot,nullptr,RiscCpu::NativeRetainedWake::backend(),schedulerDelay,RiscCpu::NativeRetainedWake::coldBoot,priorFailure,RuntimeProviders::selectedNativeProviderPoliciesV1(),RiscCpu::NativeFailureEvidence::backend(),RISC_DIAGNOSTIC_CHECKPOINT_BACKEND});
#else
RiscBoot::Runtime runtime({isOwner,health,cooperate,diagnostic,bindPlatforms,RiscNvs::backend(),appExitSafe,providerStorageSafe,confirmBoot,nullptr,RiscCpu::NativeRetainedWake::backend(),schedulerDelay,RiscCpu::NativeRetainedWake::coldBoot,priorFailure,RuntimeProviders::selectedNativeProviderPoliciesV1(),RiscCpu::NativeFailureEvidence::backend(),RISC_DIAGNOSTIC_CHECKPOINT_BACKEND});
#endif
}
void setup() {
  owner=xTaskGetCurrentTaskHandle();
  RiscCpu::NativeFailureEvidence::start();
#if RISC_STAGE_LOGS
  const auto bootUs=uint64_t(esp_timer_get_time());
  uint64_t stageUs=0;
#endif
  RiscPerf::configure([]()->uint64_t{return uint64_t(esp_timer_get_time());},isOwner,RISC_PERFORMANCE_TRACE);
  RiscPerf::emit(50);
  RiscCpu::NativeRetainedWake::start(); RiscCpu::NativeRealtime::start();
#if RISC_STAGE_LOGS && RISC_HWCDC_SERIAL
  const bool diagnosticTxReady=RiscDiagnostics::prepareSerial();
#endif
  Serial.begin(115200);
#if RISC_DIAGNOSTIC_ADAPTER
  RiscDiagnostics::start();
#endif
#if RISC_STAGE_LOGS && RISC_HWCDC_SERIAL
  RISC_STAGE_LOG("usb tx-buffer requested=8192 result=%s",diagnosticTxReady?"ready":"allocation-failed-fallback");
#endif
  RISC_STAGE_LOG("boot begin reset=%d wake=%lu setup_start_us=%llu",int(esp_reset_reason()),
                 (unsigned long)esp_sleep_get_wakeup_cause(),(unsigned long long)bootUs);
  diagnosticLine(RISC_BUILD_IDENTITY);
  diagnosticLine(risc_app_policy_rows);
#if RISC_APP_REQUIREMENT_ROWS == 17
  diagnosticLine(risc_app_requirement_rows);
#endif
  if(risc_native_startup_error) {
    const char* failure=risc_native_startup_error();
    if(failure) { diagnosticFormat("RTE_BOOT error=native-startup detail=%s",failure);return; }
  }
#ifndef RISC_EMBEDDED_BOOTSTORE
  if(RiscNvs::initializationStatus()!=ESP_OK) diagnosticFormat("RTE_STORAGE unavailable=nvs code=%d erase_recovery=disabled",RiscNvs::initializationStatus());
#endif
#ifdef RISC_OWNER_INSTALLER
  RISC_STAGE_LOG("boot normal-start skipped reason=owner-maintenance");
  diagnosticLine("RTE_OWNER_MAINTENANCE=1");
  RiscBootstrap::ownerMaintenance(isOwner);return;
#endif
#ifdef RISC_BOARD_MARKER
  diagnosticLine(RISC_BOARD_MARKER);
#endif
#ifdef RISC_PAIRED_BANKS
  bool bankReady;
#if RISC_STAGE_LOGS
  stageUs=RiscDiagnostics::monotonicUs();
#endif
  RISC_STAGE_LOG("boot bank-selection begin");
  {RiscPerf::Scope phase(51,52);bankReady=RiscBankStore::prepareBoot(isOwner,restartSafe,providerStorageSafe);}
  RISC_STAGE_LOG("boot bank-selection end result=%s elapsed_us=%llu",bankReady?"ok":"failed",
                 (unsigned long long)(RiscDiagnostics::monotonicUs()-stageUs));
  if(!bankReady) {
    RISC_STAGE_LOG("boot failed reason=paired-bank-integrity");
    diagnosticLine("RTE_BOOT error=paired-bank-integrity");RiscBankStore::rejectBoot();return;
  }
#endif
  // Minimal flash-backed module-store bootstrap. No formatting, discovery,
  // repair, SD bus ownership or production volume capability. Provisioning may
  // subsequently stage ONLY the verified inactive paired bank.
  esp_err_t mounted;
#if RISC_STAGE_LOGS
  stageUs=RiscDiagnostics::monotonicUs();
#endif
  RISC_STAGE_LOG("boot filesystem-mount begin");
  {RiscPerf::Scope phase(53,54);
#ifdef RISC_EMBEDDED_BOOTSTORE
  mounted=riscrte_mount_embedded_store();
#else
  esp_vfs_spiffs_conf_t storage{};
  storage.base_path="/bootfs"; storage.partition_label="bootfs";
#ifdef RISC_PAIRED_BANKS
  storage.partition_label=RiscBankStore::bootLabel();
#endif
  storage.max_files=4; storage.format_if_mount_failed=false;
  mounted=esp_vfs_spiffs_register(&storage);
#endif
  }
  RISC_STAGE_LOG("boot filesystem-mount end result=%d elapsed_us=%llu",int(mounted),
                 (unsigned long long)(RiscDiagnostics::monotonicUs()-stageUs));
  if(mounted!=ESP_OK) { diagnosticFormat("RTE_BOOT error=storage-mount code=%d",mounted);
    RISC_STAGE_LOG("boot failed reason=storage-mount");
#ifdef RISC_PAIRED_BANKS
    RiscBankStore::rejectBoot();
#endif
    return; }
#ifdef RISC_PAIRED_BANKS
#ifdef RISC_PAIRED_APP_DATA
  bool appDataReady;
#if RISC_STAGE_LOGS
  stageUs=RiscDiagnostics::monotonicUs();
#endif
  RISC_STAGE_LOG("boot app-data begin");
  {RiscPerf::Scope phase(55,56);
  appDataReady=RiscAppData::prepare(isOwner,[](){return cpu.providerStorageSafe() && RiscBankStore::exitSafe();});
  if(!appDataReady)diagnosticLine("RTE_STORAGE unavailable=appdata format_and_grow=disabled");
  }
  RISC_STAGE_LOG("boot app-data end result=%s elapsed_us=%llu",appDataReady?"ok":"unavailable",
                 (unsigned long long)(RiscDiagnostics::monotonicUs()-stageUs));
#endif
  // Read-only owner input, before any app/driver binding. No configured fresh
  // time source means offline installed boot, never a stale timestamp bypass.
  RiscPerf::emit(57);
  const auto provisionStart=RiscPerf::now();
#if RISC_STAGE_LOGS
  stageUs=RiscDiagnostics::monotonicUs();
#endif
  RISC_STAGE_LOG("boot provisioning begin");
  const auto provision=RiscBootstrap::run({cpu.bootstrapHardware(),RiscNvs::backend(),providerStorageSafe,
    RiscBootstrap::nvsInput(),RiscBootstrap::configuredFreshTime(RiscBootstrap::nvsInput(),cpu.bootstrapHardware().now)
#ifdef RISC_PAIRED_APP_DATA
    ,RiscAppData::backend()
#endif
  },"/bootfs");
  RiscPerf::finish(57,58,provisionStart,uint32_t(provision.outcome));
  RISC_STAGE_LOG("boot provisioning end action=%s reason=%s elapsed_us=%llu",
                 provision.outcome==RiscBootstrap::Outcome::Stopped?"stop":"continue-installed",
                 RiscBootstrap::reasonName(provision.reason),
                 (unsigned long long)(RiscDiagnostics::monotonicUs()-stageUs));
  diagnosticFormat("RTE_PROVISION action=%s reason=%s",provision.outcome==RiscBootstrap::Outcome::Stopped?"stop":"continue-installed",RiscBootstrap::reasonName(provision.reason));
  if(provision.outcome==RiscBootstrap::Outcome::Stopped)return;
#endif
#if defined(RISC_PAIRED_BANKS) || defined(RISC_RUNTIME_METADATA_PSRAM)
  if(!retainedRuntime)retainedRuntime=RiscCpu::createRetainedRuntime({isOwner,health,cooperate,diagnostic,bindPlatforms,RiscNvs::backend(),appExitSafe,providerStorageSafe,confirmBoot
#ifdef RISC_PAIRED_APP_DATA
    ,RiscAppData::backend()
#else
    ,nullptr
#endif
    ,RiscCpu::NativeRetainedWake::backend(),schedulerDelay,RiscCpu::NativeRetainedWake::coldBoot,priorFailure,RuntimeProviders::selectedNativeProviderPoliciesV1(),RiscCpu::NativeFailureEvidence::backend(),RISC_DIAGNOSTIC_CHECKPOINT_BACKEND
  });
  if(!retainedRuntime){
    RISC_STAGE_LOG("boot failed reason=runtime-metadata-psram");
#ifdef RISC_PAIRED_BANKS
    diagnosticLine("RTE_BOOT error=paired-runtime-psram");RiscBankStore::rejectBoot();
#else
    diagnosticLine("RTE_BOOT error=runtime-metadata-psram");
#endif
    return;
  }
  auto& runtime=*retainedRuntime;
#endif
  RiscCpu::reserveNativePins(runtime.board());
#if RISC_STAGE_LOGS
  stageUs=RiscDiagnostics::monotonicUs();
#endif
  RISC_STAGE_LOG("boot manifest-prepare begin");
  const bool prepared=runtime.prepare("/bootfs");
  RISC_STAGE_LOG("boot manifest-prepare end result=%s elapsed_us=%llu",prepared?"ok":"failed",
                 (unsigned long long)(RiscDiagnostics::monotonicUs()-stageUs));
  if(!prepared) { diagnosticFormat("RTE_BOOT error=manifest detail=%s",runtime.error());
    RISC_STAGE_LOG("boot failed reason=%s",runtime.error());
#ifdef RISC_PAIRED_BANKS
    if(!runtime.retained() && !runtime.metadataCloseRetained() && appExitSafe())RiscBankStore::rejectBoot();
#endif
    return; }
  RiscPerf::emit(59);
  RISC_STAGE_LOG("boot metadata-ready elapsed_us=%llu",(unsigned long long)(RiscDiagnostics::monotonicUs()-bootUs));
  diagnosticLine("RTE_BOOT board=validated drivers=admitted");
  const bool ran=runtime.run();
  RISC_STAGE_LOG("boot app-session returned result=%s reason=%s",ran?"ok":"failed",ran?"app-returned":runtime.error());
  if(!ran) diagnosticFormat("RTE_BOOT error=runtime detail=%s",runtime.error());
  else diagnosticLine("RTE_BOOT state=idle reason=app-returned");
#ifdef RISC_PAIRED_BANKS
  // A return (including intentional default exit) is never a health signal.
  // Do not reboot retained native resources or bypass the existing barrier.
  if(!runtime.retained() && !runtime.metadataCloseRetained() && appExitSafe())RiscBankStore::rejectBoot();
#endif
}
void loop() { cooperate(50); }
