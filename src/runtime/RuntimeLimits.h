#pragma once
#include <cstddef>
// This is an explicit build selection, never inferred from product metadata.
#ifndef RISC_COHORT_PROVIDER_CAPACITY
#define RISC_COHORT_PROVIDER_CAPACITY 26
#endif
#if RISC_COHORT_PROVIDER_CAPACITY != 26 && RISC_COHORT_PROVIDER_CAPACITY != 29
#error "RISC_COHORT_PROVIDER_CAPACITY must be 26 or 29"
#endif
namespace RiscLimits {
// Cohort-capable targets already retain their Runtime metadata in PSRAM.
// Preserve the exact legacy bounds where Runtime is statically placed in scarce
// internal DRAM (including the original, CAM and X4 targets).
#if defined(ESP_PLATFORM) && !defined(RISC_PAIRED_BANKS) && !defined(RISC_RUNTIME_METADATA_PSRAM)
#if RISC_COHORT_PROVIDER_CAPACITY != 26
#error "Expanded cohort capacity requires PSRAM Runtime metadata"
#endif
constexpr size_t Apps=19,Providers=17,Grants=32;
#define RISC_RUNTIME_CAPACITY_MARKER "RISC_RUNTIME_CAPACITY:19:17:32"
static_assert(Grants>=Providers+12,"Legacy boot pins must leave room for app grants");
#else
constexpr size_t Apps=24,Providers=RISC_COHORT_PROVIDER_CAPACITY,Grants=Providers+16;
#if RISC_COHORT_PROVIDER_CAPACITY == 29
#define RISC_RUNTIME_CAPACITY_MARKER "RISC_RUNTIME_CAPACITY:24:29:45"
#else
#define RISC_RUNTIME_CAPACITY_MARKER "RISC_RUNTIME_CAPACITY:24:26:42"
#endif
// Preserve sixteen shared non-boot graph slots for both cohort selections.
// Resident host and foreground share this reserve; independent invocation
// ledgers do not expand it. Failed-release custody continues to occupy a slot.
static_assert(Grants==Providers+16,"Cohort boot pins must leave sixteen shared app grants");
#endif
}
