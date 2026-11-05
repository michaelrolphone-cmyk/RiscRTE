#pragma once
#ifndef RISC_NATIVE_SETTINGS_BACKEND
#define RISC_NATIVE_SETTINGS_BACKEND 0
#endif
#if RISC_NATIVE_SETTINGS_BACKEND
#include "NativeSettingsLifecycle.h"
#if !defined(ESP_PLATFORM) || !defined(RISC_PAIRED_APP_DATA) || !defined(RISC_PAIRED_BANKS) || !RISC_NATIVE_SETTINGS_ASSEMBLY || !RISC_NATIVE_APP_DATA_MOUNT
#error Native settings backend requires the selected X4 factory and qualified AppData mount
#endif
#ifndef RISC_NATIVE_SETTINGS_LOCAL_ONLY
#define RISC_NATIVE_SETTINGS_LOCAL_ONLY 0
#endif
#ifndef RISC_NATIVE_SETTINGS_LOCAL_FRESH
#define RISC_NATIVE_SETTINGS_LOCAL_FRESH 0
#endif
#if (RISC_NATIVE_SETTINGS_LOCAL_ONLY != 0 && RISC_NATIVE_SETTINGS_LOCAL_ONLY != 1) || (RISC_NATIVE_SETTINGS_LOCAL_FRESH != 0 && RISC_NATIVE_SETTINGS_LOCAL_FRESH != 1)
#error Native settings policy selections must be explicit booleans
#endif
#if RISC_NATIVE_SETTINGS_LOCAL_FRESH && !RISC_NATIVE_SETTINGS_LOCAL_ONLY
#error Local fresh permission requires explicit LocalOnly policy
#endif
namespace RiscCpu { namespace NativeSettingsBackend {
// Explicit pre-app owner step after NativeAppData::prepare. No mount, format,
// NVS initialization, source admission, defaults, worker creation or SD I/O.
// It binds the captured native VFS and configures two private AppDataFiles.
// A failed attempt is permanent; no backend/owner substitution is supported.
bool prepare(bool(*owner)());
const NativeSettingsLifecycle::Admission* admission(); // RAM-only getter
size_t controlBytes(); // supplied backend controls, excluding factory/workers
} }
#endif
