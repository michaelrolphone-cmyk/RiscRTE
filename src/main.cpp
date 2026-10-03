#include <Arduino.h>
#include <RiscBuildIdentity.h>
#include <esp_spiffs.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/CpuPort.h"
#include "ports/esp32s3/CooperativeDelay.h"

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
bool bindPlatforms(RiscBoot::Runtime& runtime){return cpu.bind(runtime);}
RiscBoot::Runtime runtime({isOwner,health,cooperate,diagnostic,bindPlatforms});
}
void setup() {
  owner=xTaskGetCurrentTaskHandle(); Serial.begin(115200);
  Serial.println(RISC_BUILD_IDENTITY);
#ifdef RISC_BOARD_MARKER
  Serial.println(RISC_BOARD_MARKER);
#endif
  // Minimal flash-backed module-store bootstrap. No formatting, discovery,
  // repair, partition writes, SD bus ownership or production volume capability.
#ifdef RISC_EMBEDDED_BOOTSTORE
  esp_err_t mounted=riscrte_mount_embedded_store();
#else
  esp_vfs_spiffs_conf_t storage{};
  storage.base_path="/bootfs"; storage.partition_label="bootfs";
  storage.max_files=4; storage.format_if_mount_failed=false;
  esp_err_t mounted=esp_vfs_spiffs_register(&storage);
#endif
  if(mounted!=ESP_OK) { Serial.printf("RTE_BOOT error=storage-mount code=%d\n",mounted); return; }
  // GPIO22..25 do not exist on S3; octal PSRAM/flash pads and UART0 are reserved.
  for(int p=22;p<=37;++p) runtime.board().reservePin(p);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  runtime.board().reservePin(19); runtime.board().reservePin(20);
#else
  runtime.board().reservePin(43); runtime.board().reservePin(44);
#endif
  if(!runtime.prepare("/bootfs")) { Serial.printf("RTE_BOOT error=manifest detail=%s\n",runtime.error()); return; }
  Serial.println("RTE_BOOT board=validated drivers=admitted");
  if(!runtime.run()) Serial.printf("RTE_BOOT error=runtime detail=%s\n",runtime.error());
  else Serial.println("RTE_BOOT state=idle reason=app-returned");
}
void loop() { delay(50); }
