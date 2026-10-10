#pragma once
#include <cstddef>
namespace RiscLimits {
// Preserve the exact default bounds and allocation footprint. A product that
// needs a larger provider graph selects the override in its native build; no
// application or optional service can silently grow the Runtime's metadata.
#if defined(ESP_PLATFORM) && !defined(RISC_PAIRED_BANKS) && !defined(RISC_RUNTIME_METADATA_PSRAM)
constexpr size_t Apps=19,DefaultProviders=17,DefaultGrants=32;
#else
constexpr size_t Apps=24,DefaultProviders=24,DefaultGrants=40;
#endif
#ifdef RISC_RUNTIME_PROVIDER_CAPACITY
static_assert(RISC_RUNTIME_PROVIDER_CAPACITY>=1 && RISC_RUNTIME_PROVIDER_CAPACITY<=64,
              "Provider capacity must be between 1 and 64");
constexpr size_t Providers=RISC_RUNTIME_PROVIDER_CAPACITY;
#else
constexpr size_t Providers=DefaultProviders;
#endif
// Retain the original foreground headroom when the boot/provider set grows.
constexpr size_t Grants=DefaultGrants+(Providers>DefaultProviders?Providers-DefaultProviders:0);
#if defined(ESP_PLATFORM) && !defined(RISC_PAIRED_BANKS) && !defined(RISC_RUNTIME_METADATA_PSRAM)
static_assert(Providers<=DefaultProviders,
              "Larger provider graphs require explicit PSRAM metadata placement");
#endif
static_assert(Grants>=Providers+12,"Boot pins must leave room for all app-policy grants");
}
