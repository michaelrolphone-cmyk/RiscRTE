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
inline bool cleanup(){
  // Retain every SDK reference while capture is unparked. The provider restores
  // the calibrated state before release; native cleanup must not reset it early.
  if(!ready())return false;
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
