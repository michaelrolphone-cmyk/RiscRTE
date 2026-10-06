#pragma once
#include <cstddef>
namespace RiscLimits {
// Cohort-capable targets already retain their Runtime metadata in PSRAM.
// Preserve the exact legacy bounds where Runtime is statically placed in scarce
// internal DRAM (including the original, CAM and X4 targets).
#if defined(ESP_PLATFORM) && !defined(RISC_PAIRED_BANKS) && !defined(RISC_RUNTIME_METADATA_PSRAM)
constexpr size_t Apps=19,Providers=17,Grants=32;
#else
constexpr size_t Apps=24,Providers=24,Grants=40;
#endif
static_assert(Grants>=Providers+12,"Boot pins must leave room for all app-policy grants");
}
