#include <RiscProviderV2.h>
#ifndef FILE_PROVIDER_INDEX
#define FILE_PROVIDER_INDEX 0
#endif
extern const void* storage_runtime_backend(unsigned);
extern bool storage_runtime_backend_safe(unsigned);
extern const char* storage_runtime_capability(void);
static bool start(const risc_provider_dependency_v1* deps,size_t n){(void)deps;return n==0;}
static bool quiesce(void){return storage_runtime_backend_safe(FILE_PROVIDER_INDEX);}
static void stop(void){}
static risc_driver_v2 driver={2,sizeof(driver),FILE_PROVIDER_INDEX?"runtime-files-second":"runtime-files-first","storage.volume",1,0,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){if(abi!=2)return 0;driver.capability_id=storage_runtime_capability();driver.capability=storage_runtime_backend(FILE_PROVIDER_INDEX);return &driver;}
