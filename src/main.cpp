#include <Arduino.h>
#include <RiscBuildIdentity.h>
#include <esp_spiffs.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/CpuPort.h"
#include "ports/esp32s3/NativeBoard.h"
#include "ports/esp32s3/CooperativeDelay.h"
#ifdef RISC_PAIRED_BANKS
#include "ports/esp32s3/NativeBankStore.h"
#include "ports/esp32s3/NativeRuntime.h"
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
void cooperate(uint32_t ms) { vTaskDelay(RiscCpu::cooperativeDelayTicks(ms,configTICK_RATE_HZ)); }
bool diagnostic(const char* line) { Serial.println(line); return true; }
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
bool providerStorageSafe(){return cpu.providerStorageSafe();}
bool restartSafe(){return cpu.restartResourcesSafe();}
bool confirmBoot(){
#ifdef RISC_PAIRED_BANKS
  return cpu.providerStorageSafe() && RiscBankStore::confirmBoot();
#else
  return true;
#endif
}
#ifdef RISC_PAIRED_BANKS
RiscBoot::Runtime* retainedRuntime=nullptr;
#elif defined(RISC_EMBEDDED_BOOTSTORE)
RiscBoot::Runtime runtime({isOwner,health,cooperate,diagnostic,bindPlatforms,nullptr,appExitSafe,providerStorageSafe,confirmBoot});
#else
RiscBoot::Runtime runtime({isOwner,health,cooperate,diagnostic,bindPlatforms,RiscNvs::backend(),appExitSafe,providerStorageSafe,confirmBoot});
#endif
}
void setup() {
  owner=xTaskGetCurrentTaskHandle(); Serial.begin(115200);
  Serial.println(RISC_BUILD_IDENTITY);
#ifndef RISC_EMBEDDED_BOOTSTORE
  if(RiscNvs::initializationStatus()!=ESP_OK) Serial.printf("RTE_STORAGE unavailable=nvs code=%d erase_recovery=disabled\n",RiscNvs::initializationStatus());
#endif
#ifdef RISC_BOARD_MARKER
  Serial.println(RISC_BOARD_MARKER);
#endif
#ifdef RISC_PAIRED_BANKS
  if(!RiscBankStore::prepareBoot(isOwner,restartSafe,providerStorageSafe)) {
    Serial.println("RTE_BOOT error=paired-bank-integrity");RiscBankStore::rejectBoot();return;
  }
  if(!retainedRuntime)retainedRuntime=RiscCpu::createRetainedRuntime({isOwner,health,cooperate,diagnostic,bindPlatforms,RiscNvs::backend(),appExitSafe,providerStorageSafe,confirmBoot});
  if(!retainedRuntime){
    Serial.println("RTE_BOOT error=paired-runtime-psram");RiscBankStore::rejectBoot();return;
  }
  auto& runtime=*retainedRuntime;
#endif
  // Minimal flash-backed module-store bootstrap. No formatting, discovery,
  // repair, partition writes, SD bus ownership or production volume capability.
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
  if(mounted!=ESP_OK) { Serial.printf("RTE_BOOT error=storage-mount code=%d\n",mounted);
#ifdef RISC_PAIRED_BANKS
    RiscBankStore::rejectBoot();
#endif
    return; }
  RiscCpu::reserveNativePins(runtime.board());
  if(!runtime.prepare("/bootfs")) { Serial.printf("RTE_BOOT error=manifest detail=%s\n",runtime.error());
#ifdef RISC_PAIRED_BANKS
    RiscBankStore::rejectBoot();
#endif
    return; }
  Serial.println("RTE_BOOT board=validated drivers=admitted");
  if(!runtime.run()) Serial.printf("RTE_BOOT error=runtime detail=%s\n",runtime.error());
  else Serial.println("RTE_BOOT state=idle reason=app-returned");
#ifdef RISC_PAIRED_BANKS
  // A return (including intentional default exit) is never a health signal.
  // Do not reboot retained native resources or bypass the existing barrier.
  if(!runtime.retained() && appExitSafe())RiscBankStore::rejectBoot();
#endif
}
void loop() { delay(50); }
