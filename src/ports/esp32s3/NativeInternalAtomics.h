#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "NativeMemory.h"
#if defined(__XTENSA__)
#include <esp_attr.h>
#include <esp_idf_version.h>
#define RISC_NATIVE_ATOMIC_DRAM DRAM_ATTR
#if RISC_NATIVE_ESP_IDF
#if !CONFIG_IDF_TARGET_ESP32S3 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(6,1,0) || !CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND
#error "Internal atomics require the audited ESP32-S3 IDF6.1 PSRAM workaround"
#endif
// The SDK routes 1/2/4-byte internal-DRAM operations to its hardware helper.
// Global always-lock-free is false because external RAM uses a spinlock.
static_assert(ATOMIC_INT_LOCK_FREE==1 || ATOMIC_INT_LOCK_FREE==2,"Unexpected IDF6 atomic ABI");
#else
static_assert(ATOMIC_INT_LOCK_FREE==2,"Legacy native atomics must be always lock-free");
#endif
#else
#define RISC_NATIVE_ATOMIC_DRAM
static_assert(ATOMIC_INT_LOCK_FREE==2,"Host native-atomic tests require lock-free words");
#endif
static_assert(sizeof(std::atomic<uint32_t>)==4 && alignof(std::atomic<uint32_t>)>=4,
              "Native control atomics require aligned 32-bit storage");
static_assert(sizeof(std::atomic<bool>)==1 && sizeof(std::atomic_flag)==1,
              "Native flags require the audited byte-atomic ABI");
namespace RiscCpu { namespace NativeInternalAtomics {
// Validate before the first atomic operation, including a lock attempt. The
// section attribute pins target storage; the check fails closed if placement
// changes. DRAM is a contiguous region, so both endpoints cover the full span.
// Do not use is_lock_free(): compiler/global answers cannot prove placement.
template<class T> inline bool internal(const T& object) {
  const uintptr_t first=reinterpret_cast<uintptr_t>(&object);
  return !(first%alignof(T)) && first<=UINTPTR_MAX-(sizeof(T)-1) &&
    esp_ptr_in_dram(&object) && esp_ptr_in_dram(reinterpret_cast<const void*>(first+sizeof(T)-1));
}
} }
