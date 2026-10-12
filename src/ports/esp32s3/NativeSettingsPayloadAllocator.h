#pragma once
#ifndef RISC_NATIVE_SETTINGS_PSRAM_PAYLOAD
#define RISC_NATIVE_SETTINGS_PSRAM_PAYLOAD 0
#endif
#if RISC_NATIVE_SETTINGS_PSRAM_PAYLOAD
#ifndef BOARD_XTEINK_X4_PRO
#error Native settings PSRAM payload allocation requires explicit X4 selection
#endif
#include "../../../experimental/native_replica/NativeReplicaCore.h"
#include <cstddef>
namespace RiscCpu { namespace NativeSettingsPayloadAllocator {
// Payload bytes only: never tasks, stacks, atomic/coordinator/client controls,
// DMA or cache-disabled/ISR storage. A qualified caller supplies this through
// the existing Core/store/catalog allocator inputs; no product selects it here.
constexpr size_t Alignment=alignof(std::max_align_t)>16?alignof(std::max_align_t):16;
RiscExperimental::NativeReplicaCore::Allocator allocator();
} }
#endif
