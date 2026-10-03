#include <Arduino.h>
#include <RiscBuildIdentity.h>
#include <esp_spiffs.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include "bootstrap/Runtime.h"

namespace {
TaskHandle_t owner=nullptr;
bool isOwner() { return xTaskGetCurrentTaskHandle()==owner; }
bool health(risc_runtime_health_v1* out) {
  out->uptime_ms=millis(); out->free_heap=ESP.getFreeHeap();
  const auto* app=esp_ota_get_running_partition(); out->app_address=app?app->address:0;
  esp_efuse_mac_get_default(out->mac);
  snprintf(out->target,sizeof(out->target),"%s", "esp32s3-baseline");
  return true;
}
void cooperate(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)+1); }
bool diagnostic(const char* line) { Serial.println(line); return true; }
// Static lifetime intentionally retains manifests, dependency tables and ELF
// mappings after failed quiescence. Never destroy these while hardware is live.
RiscBoot::Runtime runtime({isOwner,health,cooperate,diagnostic});
}
void setup() {
  owner=xTaskGetCurrentTaskHandle(); Serial.begin(115200);
  Serial.println(RISC_BUILD_IDENTITY);
  // Minimal flash-backed module-store bootstrap. No formatting, discovery,
  // repair, partition writes, SD bus ownership or production volume capability.
  esp_vfs_spiffs_conf_t storage{};
  storage.base_path="/bootfs"; storage.partition_label="bootfs";
  storage.max_files=4; storage.format_if_mount_failed=false;
  esp_err_t mounted=esp_vfs_spiffs_register(&storage);
  if(mounted!=ESP_OK) { Serial.printf("RTE_BOOT error=storage-mount code=%d\n",mounted); return; }
  // GPIO22..25 do not exist on S3; octal PSRAM/flash pads and UART0 are reserved.
  for(int p=22;p<=37;++p) runtime.board().reservePin(p);
  runtime.board().reservePin(43); runtime.board().reservePin(44);
  if(!runtime.prepare("/bootfs")) { Serial.printf("RTE_BOOT error=manifest detail=%s\n",runtime.error()); return; }
  Serial.println("RTE_BOOT board=validated drivers=admitted");
  if(!runtime.run()) Serial.printf("RTE_BOOT error=runtime detail=%s\n",runtime.error());
  else Serial.println("RTE_BOOT state=idle reason=app-returned");
}
void loop() { delay(50); }
