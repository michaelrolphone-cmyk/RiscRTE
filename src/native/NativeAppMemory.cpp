#include "NativeAppMemory.h"
#include "runtime/resources/AppAllocationLedger.h"
#include "../../lib/hal/RuntimeImagePressure.h"
#include <cstdlib>
#include <cstring>
#include <new>
#include <esp_heap_caps.h>
#include <Logging.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {
using Ledger=RuntimeResources::AppAllocationLedger;
constexpr size_t kCapacity=4096;
Ledger ledger;
Ledger::Entry* entries=nullptr;
SemaphoreHandle_t mutex=nullptr;
std::atomic<TaskHandle_t> relocationOwner{nullptr};
struct Lock {
  bool acquired;
  Lock() : acquired(xSemaphoreTake(mutex,pdMS_TO_TICKS(100)+1)==pdTRUE) {}
  ~Lock() { if(acquired) xSemaphoreGive(mutex); }
  explicit operator bool() const { return acquired; }
};
void* allocate(size_t bytes,uint32_t caps) {
  if(!caps)return risc_image_malloc(bytes);
  void* result=heap_caps_malloc(bytes,caps);
  if(!result && bytes && risc_image_pressure_reclaim())result=heap_caps_malloc(bytes,caps);
  return result;
}
void* resize(void* ptr,size_t bytes,uint32_t caps) {
  if(!caps)return risc_image_realloc(ptr,bytes);
  void* result=heap_caps_realloc(ptr,bytes,caps);
  if(!result && bytes && risc_image_pressure_reclaim())result=heap_caps_realloc(ptr,bytes,caps);
  return result;
}
void cooperate() { vTaskDelay(1); }
void* appMalloc(size_t bytes) {
  if(!mutex) return nullptr;
  Lock lock; if(!lock) return nullptr; return ledger.allocate(bytes);
}
void* appCalloc(size_t count,size_t size) {
  if(!mutex) return nullptr;
  Lock lock; if(!lock) return nullptr; return ledger.calloc(count,size);
}
void* appRealloc(void* ptr,size_t bytes) {
  if(!mutex) return nullptr;
  Lock lock; if(!lock) return nullptr; return ledger.resize(ptr,bytes);
}
void* appHeapMalloc(size_t bytes,uint32_t caps) {
  if(!mutex) return nullptr;
  Lock lock; if(!lock) return nullptr; return ledger.allocate(bytes,caps);
}
void* appHeapCalloc(size_t count,size_t bytes,uint32_t caps) {
  if(!mutex) return nullptr;
  Lock lock; if(!lock) return nullptr; return ledger.calloc(count,bytes,caps);
}
void* appNewNothrow(size_t bytes,const std::nothrow_t&) { return appMalloc(bytes ? bytes : 1); }
}
extern "C" bool native_app_memory_begin() {
  // Launcher serialization owns creation; the mutex is a persistent host object.
  if(!mutex) mutex=xSemaphoreCreateMutex();
  if(!mutex) return false;
  Lock lock;
  if(!lock) return false;
  if(entries) return false;
  entries=static_cast<Ledger::Entry*>(heap_caps_calloc(kCapacity,sizeof(*entries),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!entries && risc_image_pressure_reclaim())
    entries=static_cast<Ledger::Entry*>(heap_caps_calloc(kCapacity,sizeof(*entries),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!entries) return false;
  ledger.begin(entries,kCapacity,{allocate,resize,heap_caps_free,cooperate});
  return true;
}
extern "C" bool native_app_memory_end() {
  if(!mutex) return true;
  Lock lock;
  if(!lock) { LOG_ERR("APP_MEM","Allocator busy during exit; retaining invocation"); return false; }
  if(!entries) return true;
  LOG_INF("APP_MEM","reclaim blocks=%u bytes=%u peak=%u internal=%u largest=%u psram=%u",
      (unsigned)ledger.count(),(unsigned)ledger.bytes(),(unsigned)ledger.peak(),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  ledger.end();
  heap_caps_free(entries); entries=nullptr;
  LOG_INF("APP_MEM","after exit internal=%u largest=%u psram=%u",
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  return true;
}
extern "C" void* native_app_psram_alloc(size_t bytes) {
  constexpr uint32_t caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
  void* pointer=appHeapMalloc(bytes,caps);
  // Report the same memory capabilities as the failed request. The ordinary
  // exit log's "largest" field is INTERNAL RAM, not the PSRAM allocation limit.
  // appHeapMalloc has released the allocation-ledger mutex before logging.
  if(!pointer && bytes) {
    LOG_ERR("APP_MEM","psram-failed request=%u free_psram=%u largest_psram=%u",
        (unsigned)bytes,(unsigned)heap_caps_get_free_size(caps),
        (unsigned)heap_caps_get_largest_free_block(caps));
  }
  return pointer;
}
extern "C" void native_app_memory_free(void* pointer) {
  if(!pointer) return;
  if(!mutex) { heap_caps_free(pointer); return; }
  Lock lock;
  if(!lock) return;
  // Some legacy C++ imports return firmware-allocated objects; preserve their
  // allocator ABI without charging them to the app merely by task identity.
  if(!ledger.release(pointer)) heap_caps_free(pointer);
}
extern "C" void native_app_memory_relocation(bool active) {
  relocationOwner.store(active ? xTaskGetCurrentTaskHandle() : nullptr);
}
extern "C" uintptr_t native_app_memory_symbol(const char* name) {
  if(!name || relocationOwner.load() != xTaskGetCurrentTaskHandle()) return 0;
#define APP_SYMBOL(symbol, function) if(!std::strcmp(name,symbol)) return reinterpret_cast<uintptr_t>(&function)
  APP_SYMBOL("malloc",appMalloc);
  APP_SYMBOL("calloc",appCalloc);
  APP_SYMBOL("realloc",appRealloc);
  APP_SYMBOL("free",native_app_memory_free);
  APP_SYMBOL("heap_caps_malloc",appHeapMalloc);
  APP_SYMBOL("heap_caps_calloc",appHeapCalloc);
  APP_SYMBOL("heap_caps_free",native_app_memory_free);
  APP_SYMBOL("_ZnwjRKSt9nothrow_t",appNewNothrow);
  APP_SYMBOL("_ZdlPv",native_app_memory_free);
#undef APP_SYMBOL
  return 0;
}
