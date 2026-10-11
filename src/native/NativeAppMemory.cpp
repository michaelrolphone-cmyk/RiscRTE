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
struct Context { Ledger ledger; Ledger::Entry* entries=nullptr; uint64_t token=0; };
Context contexts[2];
Context* selected=nullptr;
TaskHandle_t allocationOwner=nullptr;
std::atomic<bool> retained{false};
Ledger* currentLedger() {
  return !retained && selected && xTaskGetCurrentTaskHandle()==allocationOwner ? &selected->ledger : nullptr;
}
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
  Lock lock; if(!lock) return nullptr; auto* ledger=currentLedger(); return ledger ? ledger->allocate(bytes) : nullptr;
}
void* appCalloc(size_t count,size_t size) {
  if(!mutex) return nullptr;
  Lock lock; if(!lock) return nullptr; auto* ledger=currentLedger(); return ledger ? ledger->calloc(count,size) : nullptr;
}
void* appRealloc(void* ptr,size_t bytes) {
  if(!mutex) return nullptr;
  Lock lock; if(!lock) return nullptr; auto* ledger=currentLedger(); return ledger ? ledger->resize(ptr,bytes) : nullptr;
}
void* appHeapMalloc(size_t bytes,uint32_t caps) {
  if(!mutex) return nullptr;
  Lock lock; if(!lock) return nullptr; auto* ledger=currentLedger(); return ledger ? ledger->allocate(bytes,caps) : nullptr;
}
void* appHeapCalloc(size_t count,size_t bytes,uint32_t caps) {
  if(!mutex) return nullptr;
  Lock lock; if(!lock) return nullptr; auto* ledger=currentLedger(); return ledger ? ledger->calloc(count,bytes,caps) : nullptr;
}
void* appNewNothrow(size_t bytes,const std::nothrow_t&) { return appMalloc(bytes ? bytes : 1); }
}
extern "C" bool native_app_memory_begin_for(uint64_t token) {
  if(!token || retained) return false;
  if(!mutex) mutex=xSemaphoreCreateMutex();
  if(!mutex) return false;
  Lock lock; if(!lock) return false;
  const auto owner=xTaskGetCurrentTaskHandle();
  if(allocationOwner && allocationOwner!=owner)return false;
  Context* slot=nullptr;
  for(auto& context:contexts) {
    if(context.token==token)return false;
    if(!context.token && !slot)slot=&context;
  }
  if(!slot)return false;
  auto* entries=static_cast<Ledger::Entry*>(heap_caps_calloc(kCapacity,sizeof(Ledger::Entry),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!entries && risc_image_pressure_reclaim())
    entries=static_cast<Ledger::Entry*>(heap_caps_calloc(kCapacity,sizeof(Ledger::Entry),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!entries)return false;
  slot->ledger.begin(entries,kCapacity,{allocate,resize,heap_caps_free,cooperate});
  slot->entries=entries;slot->token=token;selected=slot;allocationOwner=owner;
  return true;
}
extern "C" bool native_app_memory_begin() { return native_app_memory_begin_for(UINT64_MAX); }
extern "C" bool native_app_memory_select(uint64_t token) {
  if(!mutex)return false;
  Lock lock;if(!lock || retained || xTaskGetCurrentTaskHandle()!=allocationOwner)return false;
  for(auto& context:contexts)if(context.token==token && token){selected=&context;return true;}
  return false;
}
extern "C" void native_app_memory_retain_all() {
  // Only the serialized owner may fence both ledgers. Do not wait on an unsafe
  // allocation lock or free anything after a failed context transition.
  if(xTaskGetCurrentTaskHandle()==allocationOwner)retained=true;
}
extern "C" bool native_app_memory_end() {
  if(!mutex)return true;
  Lock lock;
  if(!lock) { LOG_ERR("APP_MEM","Allocator busy during exit; retaining invocation"); return false; }
  if(!selected)return !retained;
  if(!currentLedger())return false;
  auto& ledger=selected->ledger;
  LOG_INF("APP_MEM","reclaim blocks=%u bytes=%u peak=%u internal=%u largest=%u psram=%u",
      (unsigned)ledger.count(),(unsigned)ledger.bytes(),(unsigned)ledger.peak(),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  ledger.end();heap_caps_free(selected->entries);
  selected->entries=nullptr;selected->token=0;selected=nullptr;
  bool any=false;for(const auto& context:contexts)any=any || context.token;
  if(!any)allocationOwner=nullptr;
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
  if(!currentLedger())return;
  // Resolve the real ledger even when release occurs inside the other live
  // invocation. Never leave a stale ledger entry to be freed again on exit.
  for(auto& context:contexts)if(context.token && context.ledger.release(pointer))return;
  heap_caps_free(pointer);
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
