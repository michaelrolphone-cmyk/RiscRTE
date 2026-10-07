#include <Arduino.h>
#include <RiscBuildIdentity.h>
#include <esp_spiffs.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/CpuPort.h"
#include "ports/esp32s3/NativeRetainedWake.h"
#include "ports/esp32s3/NativeBoard.h"
#include "ports/esp32s3/CooperativeDelay.h"
#include "ports/esp32s3/SleepDiagnostics.h"
#include <cstdarg>
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
#if RISC_SLEEP_DIAGNOSTICS
  RiscDiagnostics::line(line);
#else
  Serial.println(line);
#endif
}
void diagnosticFormat(const char* format,...) {
  char line[256];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);
  diagnosticLine(line);
}
void cooperate(uint32_t ms) {
#if RISC_SLEEP_DIAGNOSTICS
  RiscDiagnostics::poll();
#endif
  vTaskDelay(RiscCpu::cooperativeDelayTicks(ms,configTICK_RATE_HZ));
}
bool diagnostic(const char* line) { diagnosticLine(line); return true; }
// Static lifetime intentionally retains manifests, dependency tables and ELF
// mappings after failed quiescence. Never destroy these while hardware is live.
RiscCpu::Port cpu(RiscCpu::nativeHardware(isOwner));
bool bindPlatforms(RiscBoot::Runtime& runtime){return cpu.bind(runtime)
#ifdef RISC_PAIRED_BANKS
  && RiscBankStore::bind(runtime)
#endif
  ;}
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
#if defined(RISC_PAIRED_BANKS) || defined(RISC_RUNTIME_METADATA_PSRAM)
RiscBoot::Runtime* retainedRuntime=nullptr;
#elif defined(RISC_EMBEDDED_BOOTSTORE)
RiscBoot::Runtime runtime({isOwner,health,cooperate,diagnostic,bindPlatforms,nullptr,appExitSafe,providerStorageSafe,confirmBoot,nullptr,RiscCpu::NativeRetainedWake::backend()});
#else
RiscBoot::Runtime runtime({isOwner,health,cooperate,diagnostic,bindPlatforms,RiscNvs::backend(),appExitSafe,providerStorageSafe,confirmBoot,nullptr,RiscCpu::NativeRetainedWake::backend()});
#endif
}
void setup() {
  owner=xTaskGetCurrentTaskHandle(); RiscCpu::NativeRetainedWake::start(); Serial.begin(115200);
#if RISC_SLEEP_DIAGNOSTICS
  RiscDiagnostics::start();
#endif
  diagnosticLine(RISC_BUILD_IDENTITY);
#ifndef RISC_EMBEDDED_BOOTSTORE
  if(RiscNvs::initializationStatus()!=ESP_OK) diagnosticFormat("RTE_STORAGE unavailable=nvs code=%d erase_recovery=disabled",RiscNvs::initializationStatus());
#endif
#ifdef RISC_OWNER_INSTALLER
  diagnosticLine("RTE_OWNER_MAINTENANCE=1");
  RiscBootstrap::ownerMaintenance(isOwner);return;
#endif
#ifdef RISC_BOARD_MARKER
  diagnosticLine(RISC_BOARD_MARKER);
#endif
#ifdef RISC_PAIRED_BANKS
  if(!RiscBankStore::prepareBoot(isOwner,restartSafe,providerStorageSafe)) {
    diagnosticLine("RTE_BOOT error=paired-bank-integrity");RiscBankStore::rejectBoot();return;
  }
#endif
  // Minimal flash-backed module-store bootstrap. No formatting, discovery,
  // repair, SD bus ownership or production volume capability. Provisioning may
  // subsequently stage ONLY the verified inactive paired bank.
#ifdef RISC_EMBEDDED_BOOTSTORE
  esp_err_t mounted=riscrte_mount_embedded_store();
#else
  esp_vfs_spiffs_conf_t storage{};
  storage.base_path="/bootfs"; storage.partition_label="bootfs";
#ifdef RISC_PAIRED_BANKS
  storage.partition_label=RiscBankStore::bootLabel();
#endif
  storage.max_files=4; storage.format_if_mount_failed=false;
  esp_err_t mounted=esp_vfs_spiffs_register(&storage);
#endif
  if(mounted!=ESP_OK) { diagnosticFormat("RTE_BOOT error=storage-mount code=%d",mounted);
#ifdef RISC_PAIRED_BANKS
    RiscBankStore::rejectBoot();
#endif
    return; }
#ifdef RISC_PAIRED_BANKS
#ifdef RISC_PAIRED_APP_DATA
  if(!RiscAppData::prepare(isOwner,[](){return cpu.providerStorageSafe() && RiscBankStore::exitSafe();}))diagnosticLine("RTE_STORAGE unavailable=appdata format_and_grow=disabled");
#endif
  // Read-only owner input, before any app/driver binding. No configured fresh
  // time source means offline installed boot, never a stale timestamp bypass.
  const auto provision=RiscBootstrap::run({cpu.bootstrapHardware(),RiscNvs::backend(),providerStorageSafe,
    RiscBootstrap::nvsInput(),RiscBootstrap::configuredFreshTime(RiscBootstrap::nvsInput(),cpu.bootstrapHardware().now)
#ifdef RISC_PAIRED_APP_DATA
    ,RiscAppData::backend()
#endif
  },"/bootfs");
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
    ,RiscCpu::NativeRetainedWake::backend()
  });
  if(!retainedRuntime){
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
  if(!runtime.prepare("/bootfs")) { diagnosticFormat("RTE_BOOT error=manifest detail=%s",runtime.error());
#ifdef RISC_PAIRED_BANKS
    if(!runtime.retained() && !runtime.metadataCloseRetained() && appExitSafe())RiscBankStore::rejectBoot();
#endif
    return; }
  diagnosticLine("RTE_BOOT board=validated drivers=admitted");
  if(!runtime.run()) diagnosticFormat("RTE_BOOT error=runtime detail=%s",runtime.error());
  else diagnosticLine("RTE_BOOT state=idle reason=app-returned");
#ifdef RISC_PAIRED_BANKS
  // A return (including intentional default exit) is never a health signal.
  // Do not reboot retained native resources or bypass the existing barrier.
  if(!runtime.retained() && !runtime.metadataCloseRetained() && appExitSafe())RiscBankStore::rejectBoot();
#endif
}
void loop() { cooperate(50); }
