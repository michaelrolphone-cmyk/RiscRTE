#pragma once
#include <cstddef>
#include <cstdint>
#ifndef RISC_NATIVE_SETTINGS_STARTUP_ENTROPY
#define RISC_NATIVE_SETTINGS_STARTUP_ENTROPY 0
#endif
#if RISC_NATIVE_SETTINGS_STARTUP_ENTROPY
#if !defined(ESP_PLATFORM) || !defined(BOARD_XTEINK_X4_PRO) || !RISC_NATIVE_SETTINGS_BACKEND
#error Startup settings entropy requires the explicitly selected X4 backend
#endif
namespace RiscCpu { namespace NativeSettingsStartupEntropy {
enum class Purpose:uint32_t {Incarnation=1,FirstCheckpoint=2};
// One call at the start of setup, before RF/ADC/provider initialization. The
// qualified product composition must preserve that precondition. No later
// retry, peripheral activation, persistent seed or app-facing entropy API.
bool capture(bool(*owner)());
// Native-only IDs derived from the captured seed and a nonwrapping counter.
// Authorization is checked before and after derivation; failure copies nothing.
bool take(uint32_t nameSpace,Purpose,void*context,bool(*authorized)(void*),uint8_t out[16]);
size_t controlBytes();
} }
#endif
