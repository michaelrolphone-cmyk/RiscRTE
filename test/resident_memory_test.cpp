#include "native/NativeAppMemory.h"
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <unordered_set>
extern "C" void native_app_memory_relocation(bool);
static bool owner=true,fail=false;
static std::unordered_set<void*> live;
extern "C" void* heap_caps_malloc(size_t n,uint32_t){if(fail)return nullptr;void*p=std::malloc(n);assert(p);live.insert(p);return p;}
extern "C" void* heap_caps_calloc(size_t n,size_t z,uint32_t caps){if(z && n>SIZE_MAX/z)return nullptr;void*p=heap_caps_malloc(n*z,caps);if(p)std::memset(p,0,n*z);return p;}
extern "C" void* heap_caps_realloc(void*p,size_t n,uint32_t caps){if(!p)return heap_caps_malloc(n,caps);if(fail)return nullptr;assert(live.erase(p));p=std::realloc(p,n);assert(p);live.insert(p);return p;}
extern "C" void heap_caps_free(void*p){if(p){assert(live.erase(p));std::free(p);}}
extern "C" void* pressure_malloc(size_t n){return heap_caps_malloc(n,0);}
extern "C" void* pressure_calloc(size_t n,size_t z){return heap_caps_calloc(n,z,0);}
extern "C" void* pressure_realloc(void*p,size_t n){return heap_caps_realloc(p,n,0);}
extern "C" void pressure_free(void*p){heap_caps_free(p);}
extern "C" size_t heap_caps_get_free_size(uint32_t){return 0;}
extern "C" size_t heap_caps_get_largest_free_block(uint32_t){return 0;}
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(){return reinterpret_cast<void*>(owner?1:2);}
extern "C" void vTaskDelay(TickType_t){}
int main(int argc,char** argv){
 assert(argc==2);const bool retain=!std::strcmp(argv[1],"retain");
 assert(!native_app_memory_begin_for(0));
 assert(native_app_memory_begin_for(1));
 auto* host=static_cast<unsigned char*>(native_app_psram_alloc(37));assert(host);std::memset(host,0x71,37);
 assert(!native_app_memory_begin_for(1));
 fail=true;assert(!native_app_memory_begin_for(2));fail=false;
 assert(native_app_memory_select(1) && host[0]==0x71 && live.size()==2);
 for(uint64_t generation=2;generation<22;++generation){
  assert(native_app_memory_begin_for(generation));
  auto* child=static_cast<unsigned char*>(native_app_psram_alloc(91));assert(child);std::memset(child,0x25,91);
  assert(!native_app_memory_begin_for(generation+100)); // Exactly two contexts.
  owner=false;assert(!native_app_memory_select(1) && !native_app_memory_end());
  assert(!native_app_psram_alloc(1));native_app_memory_free(child);assert(live.count(child));owner=true;
  assert(native_app_memory_select(1));assert(host[0]==0x71);
  auto* overlay=native_app_psram_alloc(128);assert(overlay);
  assert(native_app_memory_select(generation));assert(child[0]==0x25);
  // A free issued under another invocation resolves the original ledger.
  native_app_memory_free(overlay);assert(!live.count(overlay));
  if(retain){
   native_app_memory_retain_all();assert(!native_app_memory_select(1) && !native_app_memory_end());
   assert(!native_app_psram_alloc(1));native_app_memory_free(host);native_app_memory_free(child);
   assert(live.count(host) && live.count(child) && live.size()==4);
   puts("Resident allocation terminal retention PASS");return 0;
  }
  assert(native_app_memory_end());assert(!live.count(child) && live.count(host));
  assert(!native_app_memory_select(generation));assert(native_app_memory_select(1));
  assert(host[0]==0x71 && live.size()==2);
 }
 native_app_memory_free(host);assert(native_app_memory_end());assert(live.empty());
 puts("Resident allocation repeated child/overlay ownership PASS");
}
