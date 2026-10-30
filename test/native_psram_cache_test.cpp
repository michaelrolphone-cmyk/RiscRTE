#include "ports/esp32s3/NativePsramCache.h"
#include <cassert>
#include <cstdio>
#ifndef TEST_CACHE_LINE
#define TEST_CACHE_LINE 32
#endif
static uintptr_t touchedLow=UINTPTR_MAX,touchedHigh=0;static size_t line=TEST_CACHE_LINE;
size_t esp_cache_get_line_size_by_addr(const void*){return TEST_CACHE_LINE;}
static bool yieldAllowed=true,schedulerRunning=true;
static unsigned calls=0,yields=0;static bool isr=false,mapped=true,fail=false;static int64_t now=0;
bool xPortInIsrContext(){return isr;}
bool xPortCanYield(){return yieldAllowed;}
int xTaskGetSchedulerState(){return schedulerRunning?taskSCHEDULER_RUNNING:0;}
void vTaskDelay(unsigned n){assert(n==1);++yields;now+=1000;}
int64_t esp_timer_get_time(){return now;}
bool esp_ptr_external_ram(const void* p){return mapped && uintptr_t(p)>=SOC_EXTRAM_DATA_LOW && uintptr_t(p)<SOC_EXTRAM_DATA_HIGH;}
int esp_cache_msync(void* p,size_t n,int flags){
 assert(uintptr_t(p)>=SOC_EXTRAM_DATA_LOW && n && n<=4096);
 assert(flags==(ESP_CACHE_MSYNC_FLAG_DIR_C2M|ESP_CACHE_MSYNC_FLAG_TYPE_DATA|ESP_CACHE_MSYNC_FLAG_UNALIGNED));
 // Model SDK/ROM cache-line granularity, not byte-only writeback.
 const uintptr_t first=uintptr_t(p)&~uintptr_t(line-1);
 const uintptr_t end=(uintptr_t(p)+n+line-1)&~uintptr_t(line-1);
 if(first<touchedLow)touchedLow=first;
 if(end>touchedHigh)touchedHigh=end;
 ++calls;return fail?-1:ESP_OK;
}
int main(){
 using namespace RiscCpu::NativePsramCache;
 const auto* a=api();
#if CONFIG_SPIRAM_FETCH_INSTRUCTIONS && CONFIG_SPIRAM_RODATA
 assert(a && a->api_version==1 && a->struct_size==sizeof(*a) && a->cache_line_bytes==TEST_CACHE_LINE);
 assert(a->writeback(a->context,SOC_EXTRAM_DATA_LOW+1,32769)==0 && calls==9 && yields==1);
 assert(touchedLow==SOC_EXTRAM_DATA_LOW && touchedHigh==SOC_EXTRAM_DATA_LOW+32768+TEST_CACHE_LINE);
 isr=true;assert(a->writeback(a->context,SOC_EXTRAM_DATA_LOW,1)==-2 && calls==9);isr=false;
 yieldAllowed=false;assert(a->writeback(a->context,SOC_EXTRAM_DATA_LOW,1)==-2 && calls==9);yieldAllowed=true;
 schedulerRunning=false;assert(a->writeback(a->context,SOC_EXTRAM_DATA_LOW,1)==-2 && calls==9);schedulerRunning=true;
 mapped=false;assert(a->writeback(a->context,SOC_EXTRAM_DATA_LOW,1)==-1 && calls==9);mapped=true;
 line=64;touchedLow=UINTPTR_MAX;touchedHigh=0;
 assert(a->writeback(a->context,SOC_EXTRAM_DATA_LOW+63,2)==0);
 assert(touchedLow==SOC_EXTRAM_DATA_LOW && touchedHigh==SOC_EXTRAM_DATA_LOW+128);
 fail=true;assert(a->writeback(a->context,SOC_EXTRAM_DATA_LOW,1)==-4 && calls==11);
#else
 assert(!a);assert(writeback(nullptr,SOC_EXTRAM_DATA_LOW,1)==-3 && !calls);
#endif
 puts("Native PSRAM adapter flags/range/ISR/configuration controls PASS; SDK is modeled, not DMA proof");
}
