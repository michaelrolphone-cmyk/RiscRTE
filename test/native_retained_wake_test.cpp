#include "ports/esp32s3/NativeRetainedWake.h"
#include <esp_system.h>
#include <esp_sleep.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>
static unsigned mode;
esp_reset_reason_t esp_reset_reason(){return mode==0?ESP_RST_POWERON:mode==1?ESP_RST_SW:ESP_RST_DEEPSLEEP;}
extern "C" esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(){
 return mode==2?ESP_SLEEP_WAKEUP_TIMER:mode==3?ESP_SLEEP_WAKEUP_EXT0:mode==4?ESP_SLEEP_WAKEUP_EXT1:ESP_SLEEP_WAKEUP_UNDEFINED;
}
int main(int argc,char** argv){assert(argc==2);mode=unsigned(atoi(argv[1]));
 using namespace RiscCpu::NativeRetainedWake;
 auto* s=backend();assert(!s->ready());
 RiscRetainedWake::Identity id{};risc_retained_wake_record_v1 r{sizeof(r),1,1,128,{}};uint32_t cause=99;
 s->stage(id,r);if(mode==6)enter([](){});else s->commit(); // Model RTC left by previous boot before native start.
 start();assert(s->ready());
 assert(s->read(id,1,1,r,cause)==(mode>=2 && mode<6?RISC_RETAINED_WAKE_OK:RISC_RETAINED_WAKE_ABSENT));
 assert(cause==(mode==4?uint32_t(RISC_BOOT_DEEP_GPIO):mode>=5?uint32_t(RISC_BOOT_DEEP_OTHER):mode));
 s->stage(id,r);enter([](){}); // Actual adapter rollback; boot is idempotent.
 start();assert(s->read(id,1,1,r,cause)==RISC_RETAINED_WAKE_ABSENT);
 puts("Native retained-wake SDK adapter classification PASS");
}
