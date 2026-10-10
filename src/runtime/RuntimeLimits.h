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
constexpr size_t Apps=24,Providers=26,Grants=42;
// Preserve the cohort's sixteen shared non-boot graph slots. Resident host
// and foreground grants share this reserve; their combined use is bounded by
// Grants, not by the independent sixteen-slot invocation ledgers.
static_assert(Grants>=Providers+16,"Cohort boot pins must leave sixteen shared app grants");
#endif
}
