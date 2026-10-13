#pragma once
#ifndef RISC_NATIVE_FILE_SOURCE
#define RISC_NATIVE_FILE_SOURCE 0
#endif
#if RISC_NATIVE_FILE_SOURCE != 0 && RISC_NATIVE_FILE_SOURCE != 1
#error RISC_NATIVE_FILE_SOURCE must be 0 or 1
#endif
#if RISC_NATIVE_FILE_SOURCE
#if !defined(RISC_PAIRED_APP_DATA) || !defined(RISC_PAIRED_BANKS)
#error Native file source requires the existing paired app-data mount
#endif
#if !defined(RISC_NATIVE_APP_DATA_MOUNT) || !RISC_NATIVE_APP_DATA_MOUNT
#error Native file source requires the checked shared app-data mount fence
#endif
#include "bootstrap/FileSourceBackend.h"
namespace RiscFileSource {
// Only after NativeAppData has admitted and mounted its exact partition.
// No mount, formatting, partition change, namespace mapping or fallback here.
bool prepare(bool(*owner)(),bool(*operationSafe)());
const RiscBoot::FileSourceBackend* backend();
bool exitSafe();
bool resetSafe();
}
#endif
