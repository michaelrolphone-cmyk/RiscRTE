#pragma once
#include <cstddef>
namespace RiscLimits {
// Cohort-capable targets already retain their Runtime metadata in PSRAM.
// Preserve the exact legacy bounds where Runtime is statically placed in scarce
// internal DRAM (including the original, CAM and X4 targets).
#if defined(ESP_PLATFORM) && !defined(RISC_PAIRED_BANKS) && !defined(RISC_RUNTIME_METADATA_PSRAM)
constexpr size_t Apps=19,Providers=17,Grants=32;
static_assert(Grants>=Providers+12,"Legacy boot pins must leave room for app grants");
#else
#if defined(RISC_INSTALLED_APP_CAPACITY)
static_assert(RISC_INSTALLED_APP_CAPACITY>=28 && RISC_INSTALLED_APP_CAPACITY<=64,"installed app capacity 28..64");
constexpr size_t Apps=RISC_INSTALLED_APP_CAPACITY;
constexpr size_t DefaultProviders=29;
#else
constexpr size_t Apps=28;
constexpr size_t DefaultProviders=28;
#endif
// Explicit source composition selection. Default and legacy targets retain
// their existing allocations. Changing this requires graph and memory evidence.
#if defined(RISC_PROVIDER_CAPACITY)
static_assert(RISC_PROVIDER_CAPACITY>=DefaultProviders && RISC_PROVIDER_CAPACITY<=40,
              "provider capacity must preserve the cohort and stay within 40");
constexpr size_t Providers=RISC_PROVIDER_CAPACITY;
#else
constexpr size_t Providers=DefaultProviders;
#endif
constexpr size_t Grants=Providers+16;
// Preserve the cohort's sixteen shared non-boot graph slots. Resident host
// and foreground grants share this reserve; their combined use is bounded by
// Grants, not by the independent sixteen-slot invocation ledgers.
static_assert(Grants>=Providers+16,"Cohort boot pins must leave sixteen shared app grants");
#endif
}
