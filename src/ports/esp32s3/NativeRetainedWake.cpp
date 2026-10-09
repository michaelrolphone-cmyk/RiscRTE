#include "NativeRetainedWake.h"
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_sleep.h>
namespace RiscCpu { namespace NativeRetainedWake {
namespace { RTC_NOINIT_ATTR RiscRetainedWake::Image image;
RiscRetainedWake::Store store(image); }
RiscRetainedWake::Store* backend(){return &store;}
bool coldBoot(){return esp_reset_reason()!=ESP_RST_DEEPSLEEP;}
void start(){
 const auto reset=esp_reset_reason();const auto wake=esp_sleep_get_wakeup_cause();
 uint32_t cause=reset==ESP_RST_POWERON?RISC_BOOT_POWER_ON:RISC_BOOT_RESET;
 if(reset==ESP_RST_DEEPSLEEP){
  cause=wake==ESP_SLEEP_WAKEUP_TIMER?RISC_BOOT_DEEP_TIMER:
   (wake==ESP_SLEEP_WAKEUP_EXT0 || wake==ESP_SLEEP_WAKEUP_EXT1 || wake==ESP_SLEEP_WAKEUP_GPIO)?RISC_BOOT_DEEP_GPIO:RISC_BOOT_DEEP_OTHER;
 }
 store.boot(cause);
}
void enter(void (*terminal)()){store.commit();terminal();store.rollback();}
}}
