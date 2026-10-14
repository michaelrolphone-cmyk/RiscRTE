#pragma once
#ifndef RISC_NATIVE_SETTINGS_PSRAM_PAYLOAD
#define RISC_NATIVE_SETTINGS_PSRAM_PAYLOAD 0
#endif
#if RISC_NATIVE_SETTINGS_PSRAM_PAYLOAD
#if !defined(BOARD_XTEINK_X4_PRO) && !defined(RISC_NATIVE_SETTINGS_PORT_SELECTED)
#error Native settings PSRAM payload allocation requires an explicit native storage port selection
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
