// Test-only instrumentation: never linked into production Runtime.
#include "ports/esp32s3/NativePsramCache.h"
#include <esp_async_memcpy.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <cstdio>
#include <cstring>
namespace {
async_memcpy_handle_t dma=nullptr;
uint8_t* source=nullptr;uint8_t* destination=nullptr;
size_t count=0;unsigned suppressed=0;bool unexpectedSourceFlush=false;
TaskHandle_t probeTask=nullptr;std::atomic<bool> suppressSourceFlush{false};
std::atomic<bool> completed{false};
bool completion(async_memcpy_handle_t,async_memcpy_event_t*,void*){
 completed.store(true,std::memory_order_release);return false;
}
bool transfer(){
 completed.store(false,std::memory_order_relaxed);suppressed=0;unexpectedSourceFlush=false;
 suppressSourceFlush.store(true,std::memory_order_release);
 const esp_err_t rc=esp_async_memcpy(dma,destination,source,count,completion,nullptr);
 suppressSourceFlush.store(false,std::memory_order_release);
 if(rc!=ESP_OK)return false; // Keep global handle/buffers on any uncertainty.
 const int64_t began=esp_timer_get_time();
 while(!completed.load(std::memory_order_acquire)){
  const int64_t now=esp_timer_get_time();if(now<began || now-began>=1000000)return false;
  vTaskDelay(1);
 }
 return suppressed==1 && !unexpectedSourceFlush; // SDK source pin must take exactly the reviewed flush path.
}
void retained(const char* why){std::printf("PSRAM_PROBE result=unqualified retained=1 reason=%s\n",why);}
}
extern "C" esp_err_t __real_esp_cache_msync(void*,size_t,int);
extern "C" esp_err_t __wrap_esp_cache_msync(void* address,size_t bytes,int flags){
 // Suppress only the SDK async-copy source flush. Native wrapper calls run
 // outside this phase. Other cache operations are forwarded unchanged.
 if(suppressSourceFlush.load(std::memory_order_acquire) && !xPortInIsrContext() &&
    xTaskGetCurrentTaskHandle()==probeTask){
  if(address==source && bytes==count &&
     flags==(ESP_CACHE_MSYNC_FLAG_DIR_C2M|ESP_CACHE_MSYNC_FLAG_UNALIGNED)){
   ++suppressed;return ESP_OK;
  }
  const uintptr_t p=reinterpret_cast<uintptr_t>(address),first=reinterpret_cast<uintptr_t>(source);
  if(p<first+count && bytes>(p<first?first-p:0))unexpectedSourceFlush=true;
 }

 return __real_esp_cache_msync(address,bytes,flags);
}
#ifndef RISC_CACHE_PROBE_SOURCE_SHA
#define RISC_CACHE_PROBE_SOURCE_SHA "host-model-unqualified"
#endif
extern "C" void app_main(){
 std::printf("PSRAM_PROBE source=%s\n",RISC_CACHE_PROBE_SOURCE_SHA);
 probeTask=xTaskGetCurrentTaskHandle();
 const auto* api=RiscCpu::NativePsramCache::api();
 if(!api){std::puts("PSRAM_PROBE result=unsupported_config");return;}
 count=api->cache_line_bytes*4;
 source=static_cast<uint8_t*>(heap_caps_aligned_alloc(api->cache_line_bytes,count,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 destination=static_cast<uint8_t*>(heap_caps_aligned_alloc(api->cache_line_bytes,count,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT));
 if(!source || !destination){retained("allocation");return;}
 async_memcpy_config_t config=ASYNC_MEMCPY_DEFAULT_CONFIG();config.backlog=1;
 if(esp_async_memcpy_install(&config,&dma)!=ESP_OK){retained("install");return;}
 std::memset(source,0x35,count);
 if(api->writeback(api->context,reinterpret_cast<uintptr_t>(source),count)!=0){retained("baseline_writeback_no_dma");return;}
 std::memset(source,0x6a,count);std::memset(destination,0,count);
 if(!transfer()){retained("negative_transfer_or_completion");return;}
 bool stale=true;for(size_t n=0;n<count;++n)stale&=destination[n]==0x35;
 // Eviction can make the negative control nondiscriminating: not a pass.
 if(!stale){retained("inconclusive_negative_control");return;}
 if(api->writeback(api->context,reinterpret_cast<uintptr_t>(source),count)!=0){retained("candidate_writeback_no_dma");return;}
 std::memset(destination,0,count);
 if(!transfer()){retained("positive_transfer_or_completion");return;}
 for(size_t n=0;n<count;++n)if(destination[n]!=0x6a){retained("visibility_mismatch");return;}
 // Callback completion is not ISR-return/driver quiescence. This one-shot
 // diagnostic intentionally retains global handle, buffers and callback state;
 // do not infer production shutdown or resource release from visibility proof.
 std::printf("PSRAM_PROBE result=visibility_passed bytes=%u line=%u negative_stale=1 positive_equal=1 sdk_source_flush_suppressed=1 retained=1 cleanup_qualified=0\n",unsigned(count),unsigned(api->cache_line_bytes));
}
