#pragma once
#include <cstdint>
namespace RiscCpu {
// Round up fractional RTOS ticks without adding a whole tick to exact waits.
// A zero/short wait still blocks for one tick so the idle task can run.
constexpr uint32_t cooperativeDelayTicks(uint32_t ms, uint32_t tickRateHz) {
  const uint64_t ticks = (uint64_t(ms) * tickRateHz + 999) / 1000;
  return ticks ? static_cast<uint32_t>(ticks) : 1;
}
}
