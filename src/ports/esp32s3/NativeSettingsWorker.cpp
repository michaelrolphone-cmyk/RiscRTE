#include "NativeSettingsWorker.h"
#if defined(ESP_PLATFORM) && RISC_NATIVE_SETTINGS_WORKER
#include "NativeRadioResources.h"
#if RISC_NATIVE_APP_DATA_MOUNT
#include "NativeAppDataMount.h"
#endif
#include <esp_heap_caps.h>
#include "NativeMemory.h"
#if RISC_NATIVE_ESP_IDF
#include "NativeInternalAtomics.h"
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>
namespace RiscCpu { namespace NativeSettingsWorker { namespace {
using C=RiscExperimental::NativeReplicaCore;
using S=RiscExperimental::CheckedNativeReplicaStore;
bool internal(const void*,size_t);
struct Binding {
 C&core;S&store;
 bool(*ownerCheck)();TaskHandle_t ownerTask;
 #if RISC_NATIVE_ESP_IDF && defined(__XTENSA__)
 static_assert(sizeof(std::atomic<TaskHandle_t>)==4 && alignof(std::atomic<TaskHandle_t>)>=4,
               "Native worker publication requires aligned 32-bit task handles");
 #endif
 std::atomic<TaskHandle_t>workerTask{nullptr};
 Coordinator service;
 StaticTask_t taskControl{};
 alignas(16) StackType_t stack[StackBytes/sizeof(StackType_t)]{};
 bool internalAtomicsReady()const{
 #if RISC_NATIVE_ESP_IDF
  return NativeInternalAtomics::internal(*this)&&NativeInternalAtomics::internal(workerTask)&&service.internalAtomicsReady();
 #else
  return true;
 #endif
 }
 Binding(C&c,S&s,bool(*owner)())
  :core(c),store(s),ownerCheck(owner),ownerTask(xTaskGetCurrentTaskHandle()),service(c,s,{this,
   [](void*p){auto&s=*static_cast<Binding*>(p);return s.internalAtomicsReady()&&!xPortInIsrContext()&&xTaskGetCurrentTaskHandle()==s.ownerTask&&s.ownerCheck&&s.ownerCheck();},
   [](void*p){auto&s=*static_cast<Binding*>(p);return s.internalAtomicsReady()&&!xPortInIsrContext()&&xTaskGetCurrentTaskHandle()==s.workerTask.load(std::memory_order_acquire);},
   [](void*){return nativeBackgroundResourceTry();},[](void*){nativeBackgroundResourceEnd();},
   [](void*,const void*p,size_t n){return internal(p,n);},
   #if RISC_NATIVE_APP_DATA_MOUNT
   [](void*){return RiscAppDataMount::retained();}
   #else
   nullptr
   #endif
   }){}
};
struct Registry {
 std::atomic_flag preparing=ATOMIC_FLAG_INIT;
 std::atomic<uint32_t>count{0};
 // Each entry is immutable and published by count's release store. Never read
 // an unpublished slot, replace a live binding, or free a scheduler target.
 Binding*bindings[MaxBindings]{};
};
#if RISC_NATIVE_ESP_IDF
RISC_NATIVE_ATOMIC_DRAM Registry registry;
#else
Registry registry;
#endif
bool registryReady(){
 #if RISC_NATIVE_ESP_IDF
 return NativeInternalAtomics::internal(registry)&&NativeInternalAtomics::internal(registry.preparing)&&NativeInternalAtomics::internal(registry.count);
 #else
 return true;
 #endif
}
struct Preparing {
 bool held=registryReady()&&!registry.preparing.test_and_set(std::memory_order_acquire);
 ~Preparing(){if(held&&registryReady())registry.preparing.clear(std::memory_order_release);}
};
bool internal(const void*p,size_t n){
 if(!p||!n||uintptr_t(p)>UINTPTR_MAX-(n-1))return false;
 #if RISC_NATIVE_ESP_IDF
 return esp_ptr_in_dram(p)&&esp_ptr_in_dram(static_cast<const uint8_t*>(p)+n-1);
 #else
 return esp_ptr_internal(p)&&esp_ptr_internal(static_cast<const uint8_t*>(p)+n-1);
 #endif
}
Binding*find(uint32_t ns){
 if(!registryReady())return nullptr;
 const uint32_t count=registry.count.load(std::memory_order_acquire);
 if(count>MaxBindings)return nullptr;
 for(uint32_t i=0;i<count;++i){auto*s=registry.bindings[i];if(!s||!s->internalAtomicsReady())return nullptr;if(s->service.namespaceId()==ns)return s;}
 return nullptr;
}
Binding*single(){
 if(!registryReady()||registry.count.load(std::memory_order_acquire)!=1)return nullptr;
 auto*s=registry.bindings[0];return s&&s->internalAtomicsReady()?s:nullptr;
}
bool onWorker(Binding*s){return s&&s->internalAtomicsReady()&&!xPortInIsrContext()&&xTaskGetCurrentTaskHandle()==s->workerTask.load(std::memory_order_acquire);}
void dispatch(Binding&s){
 if(!s.internalAtomicsReady())return;
 #if RISC_NATIVE_APP_DATA_MOUNT
 // The accepted lease can span owner-side SD phases. These probes may acquire
 // only the RAM custody guard while that lease waits; no idle/no-lease entry.
 Coordinator::Custody accepted{};if(!s.service.workerCustody(&accepted))return;
 if(!RiscAppDataMount::enter(RiscAppDataMount::Access::Settings)){
  if(RiscAppDataMount::retained())s.service.retainFromWorker();
  return;
 }
 s.service.workerStep();
 // Client callbacks finish/check their store and sources before returning.
 // Mailbox publication may already have happened: only permanent store/mount
 // RAM custody is touched here, never I/O or reconstructible client controls.
 // The mount's sticky failure is also an immediate coordinator fence, so owner
 // collection cannot release an unsafe native lease while this tail unwinds.
 if(!s.store.exitSafe()||s.service.retained()){
  RiscAppDataMount::retain();s.service.retainFromWorker();return;
 }
 RiscAppDataMount::leave(); // refuses and retains any still-live descriptor
 if(RiscAppDataMount::retained())s.service.retainFromWorker();
 #else
 s.service.workerStep();
 #endif
}
void run(void*p){
 auto&s=*static_cast<Binding*>(p);
 // This scheduler target is permanent. A failed placement proof parks it
 // without publishing a handle, invoking a client, deleting or freeing it.
 if(!s.internalAtomicsReady()){for(;;)vTaskDelay(1);}
 s.workerTask.store(xTaskGetCurrentTaskHandle(),std::memory_order_release);
 for(;;){dispatch(s);vTaskDelay(1);}
}
}
bool prepare(uint32_t ns,C&core,S&store,bool(*owner)()){
 if(xPortInIsrContext()||!owner||!owner()||!ns||ns>INT32_MAX)return false;
 if(!registryReady()||!internal(&registry,sizeof registry)||!internal(&core,sizeof core)||!internal(&store,sizeof store))return false;
 if(core.namespaceId()!=ns||store.namespaceId()!=ns||!core.usesBackend(store.backend()))return false;
 Preparing preparing;if(!preparing.held)return false;
 const uint32_t count=registry.count.load(std::memory_order_acquire);
 if(count>=MaxBindings)return false;
 for(uint32_t i=0;i<count;++i){
  auto&s=*registry.bindings[i];
  if(!s.internalAtomicsReady()||!s.service.owner()||!s.service.exitSafe()||s.service.namespaceId()==ns||&s.core==&core||&s.store==&store)return false;
 }
 void*allocation=heap_caps_malloc(sizeof(Binding)+alignof(Binding)-1,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
 if(!allocation)return false;
 void*memory=reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(allocation)+alignof(Binding)-1)&~uintptr_t(alignof(Binding)-1));
 if(!internal(memory,sizeof(Binding))){heap_caps_free(allocation);return false;}
 auto*s=new(memory)Binding(core,store,owner);
 if(!s->internalAtomicsReady()){s->~Binding();heap_caps_free(allocation);return false;}
 // ESP-IDF's task stack depth is bytes. Both TCB and stack have explicit
 // INTERNAL allocation; no blocked task is ever deleted or forcibly unwound.
 TaskHandle_t task=xTaskCreateStatic(run,"native-settings",StackBytes,s,1,s->stack,&s->taskControl);
 if(!task){s->~Binding();heap_caps_free(allocation);return false;}
 s->workerTask.store(task,std::memory_order_release);
 registry.bindings[count]=s;registry.count.store(count+1,std::memory_order_release);return true;
}
bool prepare(C&core,S&store,bool(*owner)()){return prepare(core.namespaceId(),core,store,owner);}
Coordinator*coordinator(uint32_t ns){auto*s=find(ns);return s&&s->service.owner()?&s->service:nullptr;}
bool isWorker(uint32_t ns){return onWorker(find(ns));}
bool workerWithLease(uint32_t ns){auto*s=find(ns);return s&&s->service.workerWithLease();}
bool custody(uint32_t ns,Custody*out){
 if(out)*out={};
 auto*s=find(ns);if(!out||!onWorker(s))return false;
 Coordinator::Custody accepted{};if(!s->service.workerCustody(&accepted)||accepted.nameSpace!=ns||accepted.coordinator!=&s->service)return false;
 *out={accepted.coordinator,s->workerTask.load(std::memory_order_acquire),accepted.generation,accepted.nameSpace,accepted.holder};return true;
}
Coordinator*coordinator(){auto*s=single();return s&&s->service.owner()?&s->service:nullptr;}
bool isWorker(){return onWorker(single());}
bool workerWithLease(){auto*s=single();return s&&s->service.workerWithLease();}
bool exitSafe(){
 if(!registryReady())return false;
 const uint32_t count=registry.count.load(std::memory_order_acquire);
 if(count>MaxBindings)return false;
 for(uint32_t i=0;i<count;++i)if(!registry.bindings[i]||!registry.bindings[i]->internalAtomicsReady()||!registry.bindings[i]->service.exitSafe())return false;
 return true;
}
} }
#endif
