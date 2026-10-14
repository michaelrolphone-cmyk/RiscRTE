#pragma once
#include <cstddef>
#include <cstdint>
#ifndef RISC_NATIVE_APP_DATA_MOUNT
#define RISC_NATIVE_APP_DATA_MOUNT 0
#endif
#if RISC_NATIVE_APP_DATA_MOUNT
#if !defined(ESP_PLATFORM) || !defined(RISC_PAIRED_APP_DATA)
#error Native AppData mount custody requires ESP paired AppData
#endif
#include <esp_partition.h>
#include <esp_vfs.h>
namespace RiscAppDataMount {
// No construction authority for inspectors/workers or invented lease epochs.
enum class Access { AppData, Settings, FileSource, Packages };
struct Identity {
 const void* context;
 const esp_vfs_t* vfs; // fenced table only; never the unguarded vendor table
 const esp_partition_t* partition;
 const char* mountPath;
 const char* settingsRoot;
 uint64_t mountGeneration;
 uint64_t rootGeneration;
};
struct Custody {
 const void* ownerTask;
 uint64_t generation;
 Access access;
};
// Called once by the existing NativeAppData owner, immediately before register.
bool prepare(const esp_partition_t* partition);
// Verify the captured mount and provision the otherwise inaccessible directory.
bool finish();
bool ready();
bool retained();
void retain();
bool enter(Access access);
void leave();
bool held();
bool identity(Identity& out);
bool custody(Custody& out);
bool usage(size_t* total, size_t* used);
struct Guard {
 bool held;
 explicit Guard(Access access):held(enter(access)){}
 ~Guard(){if(held)leave();}
 Guard(const Guard&)=delete;
 Guard& operator=(const Guard&)=delete;
};
}
#endif
