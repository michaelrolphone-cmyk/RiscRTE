#pragma once
// Resource proof only. This adapter never initializes, tunes or parks the modem.
// ROM map / allocator provenance and hardware limits: docs/RADIO_IQ_RESOURCE.md.
#include <esp_chip_info.h>
#include <esp_wifi.h>
#include <esp_phy_init.h>
#include <heap_memory_layout.h>
#include <soc/soc.h>
#include <cstdint>

extern "C" {
extern const uint32_t _rom_chip_id;
extern const int32_t _rom_eco_version;
extern const soc_reserved_region_t soc_reserved_memory_region_start[];
extern const soc_reserved_region_t soc_reserved_memory_region_end[];
extern char _heap_start, _iram_end;
}
namespace RiscCpu { namespace NativeRadioIq {
inline bool phyPrepared=false;
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
  // The external IQ driver intentionally cannot import private PHY symbols.
  // Calibrate through IDF's native PHY owner before granting raw modem access.
  // The driver snapshots this calibrated state and restores it before release.
  esp_phy_enable();
  phyPrepared=true;
  return true;
}
inline bool cleanup(){
  if(!phyPrepared)return ready();
  esp_phy_disable();
  phyPrepared=false;
  return ready();
}
} }
