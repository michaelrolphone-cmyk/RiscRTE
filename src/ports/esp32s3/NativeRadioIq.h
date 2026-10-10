#pragma once
// Resource proof and native SDK PHY ownership. External ELFs tune/park capture.
// ROM map / allocator provenance and hardware limits: docs/RADIO_IQ_RESOURCE.md.
#include <esp_chip_info.h>
#include <esp_wifi.h>
#include <esp_phy_init.h>
#include <esp_bt.h>
#include <heap_memory_layout.h>
#include <soc/soc.h>
#include <cstdint>
#include "SleepDiagnostics.h"
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <RiscRadioIqResourceV1.h>

extern "C" {
extern const uint32_t _rom_chip_id;
extern const int32_t _rom_eco_version;
extern const soc_reserved_region_t soc_reserved_memory_region_start[];
extern const soc_reserved_region_t soc_reserved_memory_region_end[];
extern char _heap_start, _iram_end;
}
namespace RiscCpu { namespace NativeRadioIq {
inline bool phyPrepared=false;
// Only the first admitted native preparation is traced. Repeated captures must
// not displace the bounded diagnostic journal with per-burst lifecycle noise.
inline bool tracePending=true,tracePrepared=false;
inline void trace(const char* line){
#if RISC_SLEEP_DIAGNOSTICS
  if(tracePrepared)RiscDiagnostics::line(line);
#else
  (void)line;
#endif
}
constexpr uintptr_t BankBase=0x3FCB0000u, BankEnd=0x3FCC0000u;
static_assert(SOC_I_D_OFFSET==0x6f0000u,"unreviewed S3 SRAM alias map");
// The pinned IDF linker KEEP and startup heap subtraction consume this before
// any heap is created. It reserves the physical bank through both D/I aliases.
SOC_RESERVE_MEMORY_REGION(BankBase,BankEnd,risc_radio_iq);
inline bool supported(){
  esp_chip_info_t chip{};esp_chip_info(&chip);
  // The published S3 ROM interface specifies ECO >= 0. Do not fabricate a
  // wafer-revision whitelist from a manifest's descriptive revision string.
  return chip.model==CHIP_ESP32S3 && (chip.features&(CHIP_FEATURE_WIFI_BGN|CHIP_FEATURE_BLE))==
    (CHIP_FEATURE_WIFI_BGN|CHIP_FEATURE_BLE) && _rom_chip_id==CHIP_ESP32S3 && _rom_eco_version>=0;
}
inline bool reserved(){
  // Static placement must not consume any byte of either bank alias. The
  // post-link check also inspects every allocatable ELF section and PT_LOAD.
  if(reinterpret_cast<uintptr_t>(&_heap_start)>BankBase ||
     reinterpret_cast<uintptr_t>(&_iram_end)>BankBase+SOC_I_D_OFFSET)return false;
  unsigned matches=0;
  for(auto r=soc_reserved_memory_region_start;r<soc_reserved_memory_region_end;++r)
    if(r->start==BankBase && r->end==BankEnd)++matches;
  return matches==1;
}
inline bool ready(){
  // Never touch the undocumented dump registers on an unsupported actual ROM.
  if(!supported() || !reserved())return false;
  // Bookkeeping alone cannot prove that an out-of-band native Wi-Fi owner is
  // absent. The read-only SDK query must report an uninitialized controller.
  wifi_mode_t mode=WIFI_MODE_NULL;
  if(esp_wifi_get_mode(&mode)!=ESP_ERR_WIFI_NOT_INIT)return false;
  return !(REG_READ(0x60033D5Cu)&0x80000000u) && !(REG_READ(0x600C101Cu)&0xFu);
}
inline bool prepare(){
  if(phyPrepared || !ready())return false;
  tracePrepared=tracePending;tracePending=false;
  trace("RTE_IQ stage=modem-init");
  // IDF 4.4.7 keeps modem backup memory, domain power and PHY enable as separate
  // references. BT teardown can leave the domain powered off. Its first power-on
  // resets the modem, so acquire it BEFORE PHY calibration/wakeup, never after.
  esp_phy_modem_init();
  trace("RTE_IQ stage=power-on");
  esp_wifi_bt_power_domain_on();
  trace("RTE_IQ stage=phy-enable");
  // SDK calls are synchronous void APIs; stage logs locate a stall but do not
  // add a timeout or claim that calibration completed until this call returns.
  esp_phy_enable();
  phyPrepared=true;
  trace("RTE_IQ stage=prepared");
  return true;
}
// The timer only wakes a dedicated task. Slow captures are skipped, never
// queued as invented evenly spaced observations. Actual timestamps travel with
// every record. stop joins; no task deletion while provider code is executing.
inline TaskHandle_t workerTask=nullptr;
inline portMUX_TYPE workerMux=portMUX_INITIALIZER_UNLOCKED;
inline SemaphoreHandle_t workerDone=nullptr;
inline esp_timer_handle_t workerTimer=nullptr;
inline bool workerStopping=false;
inline risc_radio_iq_tick_v1 workerTick=nullptr;
inline void* workerArgument=nullptr;
inline uint64_t nowUs(){return static_cast<uint64_t>(esp_timer_get_time());}
inline void workerWake(void*){portENTER_CRITICAL(&workerMux);if(workerTask)xTaskNotifyGive(workerTask);portEXIT_CRITICAL(&workerMux);}
inline void workerRun(void*){
  for(;;){ulTaskNotifyTake(pdTRUE,portMAX_DELAY);if(__atomic_load_n(&workerStopping,__ATOMIC_ACQUIRE))break;workerTick(workerArgument,nowUs());}
  xSemaphoreGive(workerDone);for(;;)vTaskSuspend(nullptr);
}
inline bool workerStop(){
  if(!workerTask)return true;
  __atomic_store_n(&workerStopping,true,__ATOMIC_RELEASE);
  if(workerTimer)esp_timer_stop(workerTimer);
  workerWake(nullptr);
  if(xSemaphoreTake(workerDone,pdMS_TO_TICKS(100))!=pdTRUE)return false;
  portENTER_CRITICAL(&workerMux);TaskHandle_t done=workerTask;workerTask=nullptr;portEXIT_CRITICAL(&workerMux);
  vTaskDelete(done);
  if(workerTimer){esp_timer_delete(workerTimer);workerTimer=nullptr;}
  vSemaphoreDelete(workerDone);workerDone=nullptr;workerTick=nullptr;workerArgument=nullptr;return true;
}
inline bool workerStart(uint32_t interval,risc_radio_iq_tick_v1 tick,void*argument){
  if(!phyPrepared||workerTask||!tick||interval<1000||interval>100000)return false;
  workerDone=xSemaphoreCreateBinary();if(!workerDone)return false;
  esp_timer_create_args_t config{};config.callback=workerWake;config.name="iq-period";
  if(esp_timer_create(&config,&workerTimer)!=ESP_OK){vSemaphoreDelete(workerDone);workerDone=nullptr;return false;}
  workerTick=tick;workerArgument=argument;__atomic_store_n(&workerStopping,false,__ATOMIC_RELEASE);
  if(xTaskCreatePinnedToCore(workerRun,"iq-envelope",4096,nullptr,3,&workerTask,0)!=pdPASS){esp_timer_delete(workerTimer);workerTimer=nullptr;vSemaphoreDelete(workerDone);workerDone=nullptr;workerTask=nullptr;return false;}
  if(esp_timer_start_periodic(workerTimer,interval)!=ESP_OK){
    // A failed join still owns callback code and its lease. Report ownership so
    // provider stop/quiesce must retry the join before releasing/unmapping.
    return !workerStop();
  }
  return true;
}
inline bool cleanup(){
  // Retain every SDK reference while capture is unparked. The provider restores
  // the calibrated state before release; native cleanup must not reset it early.
  if(workerTask||!ready())return false;
  if(!phyPrepared)return true;
  trace("RTE_IQ stage=phy-disable");
  esp_phy_disable();
  trace("RTE_IQ stage=power-off");
  esp_wifi_bt_power_domain_off();
  esp_phy_modem_deinit();
  phyPrepared=false;
  trace("RTE_IQ stage=released");
  tracePrepared=false;
  return ready();
}
} }
