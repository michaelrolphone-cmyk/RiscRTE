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
#if RISC_NATIVE_SERIAL_SD
// Product-owned exact native selection, independent of the opaque wire ID.
// Preserve the original composition only when neither selector is overridden.
#if defined(RISC_NATIVE_SERIAL_SD_PROVIDER) != defined(RISC_NATIVE_SERIAL_SD_INSTANCE)
#error "Serial SD product selection requires both provider and instance"
#endif
#ifndef RISC_NATIVE_SERIAL_SD_PROVIDER
#define RISC_NATIVE_SERIAL_SD_PROVIDER "sdmmc-fatfs"
#define RISC_NATIVE_SERIAL_SD_INSTANCE 12
#endif
#include <type_traits>
namespace RiscCpu { namespace NativeSerialStorage { namespace Detail {
constexpr char sdProvider[]=RISC_NATIVE_SERIAL_SD_PROVIDER;
constexpr bool validProvider(){
  if(sizeof(sdProvider)<2 || sizeof(sdProvider)>96)return false;
  for(size_t i=0;i+1<sizeof(sdProvider);++i)if(!sdProvider[i])return false;
  return true;
}
static_assert(validProvider(),"Serial SD provider must be one nonempty bounded identifier");
static_assert(std::is_integral<decltype(RISC_NATIVE_SERIAL_SD_INSTANCE)>::value &&
              !std::is_same<decltype(RISC_NATIVE_SERIAL_SD_INSTANCE),bool>::value,
              "Serial SD instance must be an integer hardware instance");
static_assert(RISC_NATIVE_SERIAL_SD_INSTANCE>0 && RISC_NATIVE_SERIAL_SD_INSTANCE<=2147483647,
              "Serial SD instance must be an explicit positive hardware instance");
} } }
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
// Requires successful prepare/admission; the owner constructs this session once.
RiscBoot::RuntimeFiles& sdFiles();
#endif
} }
#endif
