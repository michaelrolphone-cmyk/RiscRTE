#include "NativeInfrastructureRecovery.h"
#if defined(ESP_PLATFORM) && RISC_NATIVE_INFRA_RECOVERY
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <esp_private/system_internal.h>
#include <nvs.h>
namespace RiscCpu { namespace {
esp_task_wdt_user_handle_t watchdog=nullptr;
uint32_t boots=0; bool bootsValid=false;
int32_t feed(void*) { return watchdog && esp_task_wdt_reset_user(watchdog)==ESP_OK ? 0 : -1; }
int32_t snapshot(void*,risc_recovery_snapshot_v1* out) {
 if(!out || out->struct_size<sizeof(*out))return -1;
 wifi_ap_record_t ap{}; const bool connected=esp_wifi_sta_get_ap_info(&ap)==ESP_OK;
 *out={sizeof(*out),uint32_t(esp_reset_reason()),boots,esp_get_free_heap_size(),esp_get_minimum_free_heap_size(),uint32_t(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),connected?int32_t(ap.rssi):0,bootsValid?1u:0u}; return 0;
}
void restart(void*) { esp_restart_noos(); }
risc_platform_recovery_v1 api{1,sizeof(api),nullptr,feed,snapshot,restart};
}
const risc_platform_recovery_v1* infrastructureRecovery() {
 if(watchdog)return &api;
 // This explicit infrastructure profile owns the watchdog interval. Ordinary
 // products never select this source path. Interrupt watchdog remains enabled.
 esp_task_wdt_config_t config{90000,(1u<<portNUM_PROCESSORS)-1u,true};
 auto rc=esp_task_wdt_reconfigure(&config);
 if(rc==ESP_ERR_INVALID_STATE)rc=esp_task_wdt_init(&config);
 if(rc!=ESP_OK || esp_task_wdt_add_user("infra-executor",&watchdog)!=ESP_OK)esp_restart_noos();
 // One NVS atomic commit per boot, never per poll/heartbeat. Never erase NVS.
 nvs_handle_t h;
 if(nvs_open("infra-recovery",NVS_READWRITE,&h)==ESP_OK) {
   uint32_t old=0; auto got=nvs_get_u32(h,"boots",&old);
   if((got==ESP_OK || got==ESP_ERR_NVS_NOT_FOUND) && old<UINT32_MAX) {
     boots=old+1; bootsValid=nvs_set_u32(h,"boots",boots)==ESP_OK && nvs_commit(h)==ESP_OK;
   }
   nvs_close(h);
 }
 return &api;
}
}
#endif
