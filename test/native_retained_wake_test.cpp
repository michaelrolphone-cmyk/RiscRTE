#include "ports/esp32s3/NativeRetainedWake.h"
#include <esp_system.h>
#include <esp_sleep.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>
struct Case {esp_reset_reason_t reset;esp_sleep_wakeup_cause_t wake;uint32_t cause;bool cold,record;};
static const Case cases[]={
 {ESP_RST_POWERON,ESP_SLEEP_WAKEUP_UNDEFINED,RISC_BOOT_POWER_ON,true,false},
 {ESP_RST_SW,ESP_SLEEP_WAKEUP_UNDEFINED,RISC_BOOT_RESET,true,false},
 {ESP_RST_DEEPSLEEP,ESP_SLEEP_WAKEUP_TIMER,RISC_BOOT_DEEP_TIMER,false,true},
 {ESP_RST_DEEPSLEEP,ESP_SLEEP_WAKEUP_EXT0,RISC_BOOT_DEEP_GPIO,false,true},
 {ESP_RST_DEEPSLEEP,ESP_SLEEP_WAKEUP_EXT1,RISC_BOOT_DEEP_GPIO,false,true},
 {ESP_RST_DEEPSLEEP,ESP_SLEEP_WAKEUP_UNDEFINED,RISC_BOOT_DEEP_OTHER,false,true},
 {ESP_RST_DEEPSLEEP,ESP_SLEEP_WAKEUP_UNDEFINED,RISC_BOOT_DEEP_OTHER,false,false},
 {ESP_RST_DEEPSLEEP,ESP_SLEEP_WAKEUP_GPIO,RISC_BOOT_DEEP_GPIO,false,true},
 // Stale timer metadata cannot turn any non-deep reset into a sparse wake.
 {ESP_RST_POWERON,ESP_SLEEP_WAKEUP_TIMER,RISC_BOOT_POWER_ON,true,false},
 {ESP_RST_SW,ESP_SLEEP_WAKEUP_TIMER,RISC_BOOT_RESET,true,false},
 {ESP_RST_PANIC,ESP_SLEEP_WAKEUP_TIMER,RISC_BOOT_RESET,true,false},
 {ESP_RST_INT_WDT,ESP_SLEEP_WAKEUP_TIMER,RISC_BOOT_RESET,true,false},
 {ESP_RST_TASK_WDT,ESP_SLEEP_WAKEUP_TIMER,RISC_BOOT_RESET,true,false},
 {ESP_RST_WDT,ESP_SLEEP_WAKEUP_TIMER,RISC_BOOT_RESET,true,false},
 {ESP_RST_BROWNOUT,ESP_SLEEP_WAKEUP_TIMER,RISC_BOOT_RESET,true,false},
 {ESP_RST_EXT,ESP_SLEEP_WAKEUP_TIMER,RISC_BOOT_RESET,true,false},
 {ESP_RST_UNKNOWN,ESP_SLEEP_WAKEUP_TIMER,RISC_BOOT_RESET,true,false},
 {ESP_RST_SDIO,ESP_SLEEP_WAKEUP_TIMER,RISC_BOOT_RESET,true,false},
 // A timer wake without an app checkpoint still must not cold-start storage.
 {ESP_RST_DEEPSLEEP,ESP_SLEEP_WAKEUP_TIMER,RISC_BOOT_DEEP_TIMER,false,false}
};
static unsigned mode;
esp_reset_reason_t esp_reset_reason(){return cases[mode].reset;}
extern "C" esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(){return cases[mode].wake;}
int main(int argc,char** argv){assert(argc==2);mode=unsigned(atoi(argv[1]));assert(mode<sizeof(cases)/sizeof(cases[0]));
 using namespace RiscCpu::NativeRetainedWake;
 auto* s=backend();assert(!s->ready());assert(coldBoot()==cases[mode].cold);
 RiscRetainedWake::Identity id{};risc_retained_wake_record_v1 r{sizeof(r),1,1,128,{}};uint32_t cause=99;
 if(mode!=18){s->stage(id,r);if(mode==6)enter([](){});else s->commit();}
 start();assert(s->ready());assert(coldBoot()==cases[mode].cold);
 assert(s->read(id,1,1,r,cause)==(cases[mode].record?RISC_RETAINED_WAKE_OK:RISC_RETAINED_WAKE_ABSENT));
 assert(cause==cases[mode].cause);assert(coldBoot()==cases[mode].cold);
 s->stage(id,r);enter([](){}); // Actual adapter rollback; boot is idempotent.
 start();assert(s->read(id,1,1,r,cause)==RISC_RETAINED_WAKE_ABSENT);
 assert(coldBoot()==cases[mode].cold);
 puts("Native retained-wake and cold boot SDK classification PASS");
}
