#include <Arduino.h>
#include <RiscBuildIdentity.h>
#include <esp_spiffs.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/CpuPort.h"
#include "ports/esp32s3/CooperativeDelay.h"
#ifdef RISC_PAIRED_APP_DATA
#include "ports/esp32s3/NativeAppData.h"
#endif
#ifdef RISC_PAIRED_BANKS
#include "ports/esp32s3/NativeBankStore.h"
#endif
#if defined(RISC_PAIRED_BANKS) || defined(RISC_RUNTIME_METADATA_PSRAM)
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
#endif
#if defined(RISC_PAIRED_BANKS) || defined(RISC_RUNTIME_METADATA_PSRAM)
#ifdef RISC_PAIRED_APP_DATA
  if(!RiscAppData::prepare(isOwner,[](){return cpu.providerStorageSafe() && RiscBankStore::exitSafe();}))Serial.println("RTE_STORAGE unavailable=appdata format_and_grow=disabled");
#endif
  if(!retainedRuntime)retainedRuntime=RiscCpu::createRetainedRuntime({isOwner,health,cooperate,diagnostic,bindPlatforms,RiscNvs::backend(),appExitSafe,providerStorageSafe,confirmBoot
#ifdef RISC_PAIRED_APP_DATA
    ,RiscAppData::backend()
#endif
  });
  if(!retainedRuntime){
#ifdef RISC_PAIRED_BANKS
    Serial.println("RTE_BOOT error=paired-runtime-psram");RiscBankStore::rejectBoot();
#else
    Serial.println("RTE_BOOT error=runtime-metadata-psram");
#endif
    return;
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
  // GPIO22..25 do not exist on S3; octal PSRAM/flash pads and UART0 are reserved.
  for(int p=22;p<=37;++p) runtime.board().reservePin(p);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  runtime.board().reservePin(19); runtime.board().reservePin(20);
#else
  runtime.board().reservePin(43); runtime.board().reservePin(44);
#endif
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
