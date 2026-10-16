#pragma once
#include "NativeSerialFiles.h"
#ifndef RISC_NATIVE_SERIAL_BOOT_FILES
#define RISC_NATIVE_SERIAL_BOOT_FILES 0
#endif
#ifndef RISC_NATIVE_SERIAL_APPDATA_FILES
#define RISC_NATIVE_SERIAL_APPDATA_FILES 0
#endif
#ifndef RISC_NATIVE_SERIAL_SD
#define RISC_NATIVE_SERIAL_SD 0
#endif
#define RISC_NATIVE_SERIAL_STORAGE_SELECTED (RISC_NATIVE_SERIAL_BOOT_FILES || RISC_NATIVE_SERIAL_APPDATA_FILES || RISC_NATIVE_SERIAL_SD)
#if RISC_NATIVE_SERIAL_STORAGE_SELECTED && !RISC_NATIVE_SERIAL_FILES
#error "Selected serial storage requires the optional serial endpoint"
#endif
#if RISC_NATIVE_SERIAL_APPDATA_FILES && (!defined(RISC_PAIRED_APP_DATA) || !RISC_NATIVE_APP_DATA_MOUNT)
#error "Serial private files require explicitly selected fenced AppData storage"
#endif
#if RISC_NATIVE_SERIAL_STORAGE_SELECTED
namespace RiscCpu { namespace NativeSerialStorage {
// Explicit native product composition. IDs and labels are permanent logical
// authority, not discovered mounts or grants borrowed from an application.
// BOOT_FILES is always read-only: active paired store bytes are immutable.
// APPDATA_FILES alone provisions /appdata/.serial-files, never an app namespace.
bool prepare(NativeSerialFiles::Check owner,NativeSerialFiles::Check safe);
const NativeSerialFiles::Admission* admission();
#if RISC_NATIVE_SERIAL_SD
RiscBoot::RuntimeFiles& sdFiles();
#endif
} }
#endif
