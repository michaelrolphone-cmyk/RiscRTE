#pragma once
#include "sdkconfig.h"
#include "runtime/memory/PsramCache.h"
#include <esp_cache.h>
#include <esp_memory_utils.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/soc.h>
namespace RiscCpu { namespace NativePsramCache {
// SDK6.1 public msync explicitly excludes concurrent flash unless both XIP
// options are enabled. Do not infer exclusion from an owner-loop idle check.
#if CONFIG_IDF_TARGET_ESP32S3 && CONFIG_SPIRAM_FETCH_INSTRUCTIONS && CONFIG_SPIRAM_RODATA
inline bool supported(){return true;}
#else
inline bool supported(){return false;}
#endif
inline bool range(uintptr_t address,size_t bytes){
 return esp_ptr_external_ram(reinterpret_cast<void*>(address)) &&
   esp_ptr_external_ram(reinterpret_cast<void*>(address+bytes-1));
}
inline int32_t writeback(void*,uintptr_t address,size_t bytes){
 const RiscMemory::CacheBackend backend{
   [](){return !xPortInIsrContext() && xPortCanYield() && xTaskGetSchedulerState()==taskSCHEDULER_RUNNING;},supported,range,esp_timer_get_time,
   [](uintptr_t p,size_t n){return esp_cache_msync(reinterpret_cast<void*>(p),n,
     ESP_CACHE_MSYNC_FLAG_DIR_C2M|ESP_CACHE_MSYNC_FLAG_TYPE_DATA|
     ESP_CACHE_MSYNC_FLAG_UNALIGNED)==ESP_OK;},
   [](){vTaskDelay(1);},SOC_EXTRAM_DATA_LOW,SOC_EXTRAM_DATA_HIGH};
 return RiscMemory::writeback(backend,address,bytes);
}
inline const risc_psram_cache_v1* api(){
 if(!supported())return nullptr;
 static const uint32_t line=esp_cache_get_line_size_by_addr(reinterpret_cast<void*>(SOC_EXTRAM_DATA_LOW));
 if(!line || line>4096 || (line&(line-1)))return nullptr;
 static const risc_psram_cache_v1 value{1,sizeof(value),line,nullptr,writeback};
 return &value;
}
}}
