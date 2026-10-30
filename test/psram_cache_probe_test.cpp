// Execute the actual diagnostic logic against a declared cache/DMA model.
// This tests discrimination and refusal, NOT physical cache or DMA behavior.
#include <sys/mman.h>
#include <cassert>
#include <cstdlib>
#include <string>
#include "native_psram_cache_target/main/probe.cpp"
static std::string scenario;
static int64_t clockUs=0;static unsigned submissions=0;static uint8_t memory[4096]{};
static bool inIsr=false;static unsigned realFlushes=0;
TaskHandle_t xTaskGetCurrentTaskHandle(){return reinterpret_cast<void*>(1);}
bool xPortInIsrContext(){return inIsr;}
bool xPortCanYield(){return !inIsr;}
int xTaskGetSchedulerState(){return taskSCHEDULER_RUNNING;}
void vTaskDelay(unsigned){clockUs+=1000;}
int64_t esp_timer_get_time(){return clockUs;}
size_t esp_cache_get_line_size_by_addr(const void*){return 32;}
bool esp_ptr_external_ram(const void*p){return uintptr_t(p)>=0x3c000000 && uintptr_t(p)<0x3c001000;}
void* heap_caps_aligned_alloc(size_t alignment,size_t bytes,unsigned caps){
 if(caps&MALLOC_CAP_SPIRAM){void*p=mmap(reinterpret_cast<void*>(0x3c000000),4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(p==reinterpret_cast<void*>(0x3c000000));return p;}
 void*p=nullptr;assert(posix_memalign(&p,alignment,bytes)==0);return p;
}
void heap_caps_free(void*){assert(false && "Probe must retain custody; no claimed SDK ISR quiescence");}
extern "C" esp_err_t __real_esp_cache_msync(void*p,size_t n,int){
 ++realFlushes;assert(esp_ptr_external_ram(p)&&n<=4096);std::memcpy(memory+uintptr_t(p)-0x3c000000,p,n);return ESP_OK;
}
int esp_cache_msync(void*p,size_t n,int flags){return __wrap_esp_cache_msync(p,n,flags);}
esp_err_t esp_async_memcpy_install(const async_memcpy_config_t*,async_memcpy_handle_t*out){*out=reinterpret_cast<void*>(2);return ESP_OK;}
esp_err_t esp_async_memcpy(async_memcpy_handle_t h,void*dst,void*src,size_t n,async_memcpy_isr_cb_t cb,void*arg){
 ++submissions;const int flags=ESP_CACHE_MSYNC_FLAG_DIR_C2M|ESP_CACHE_MSYNC_FLAG_UNALIGNED;
 if(scenario=="missing-wrap")__real_esp_cache_msync(src,n,flags);
 else if(scenario=="unexpected-flags")__wrap_esp_cache_msync(src,n,flags|ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
 else if(scenario=="wrong-context"){inIsr=true;__wrap_esp_cache_msync(src,n,flags);inIsr=false;}
 else {__wrap_esp_cache_msync(src,n,flags);if(scenario=="duplicate-flush")__wrap_esp_cache_msync(src,n,flags);}
 if(scenario=="submit-error")return -1;
 if(scenario=="evicted")std::memcpy(memory,src,n);
 std::memcpy(dst,memory,n);
 if(scenario!="timeout"){inIsr=true;async_memcpy_event_t event{};cb(h,&event,arg);inIsr=false;}
 return ESP_OK;
}
int main(int argc,char**argv){assert(argc==2);scenario=argv[1];app_main();assert(source&&destination&&dma);assert(submissions==(scenario=="valid"?2u:1u));assert(realFlushes>=1);}
