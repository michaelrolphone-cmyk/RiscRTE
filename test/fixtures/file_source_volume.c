#include <RiscProviderV2.h>
extern const void* source_browser_volume(void);
extern bool source_browser_volume_safe(void);
static bool start(const risc_provider_dependency_v1* deps,size_t n){(void)deps;return n==0;}
static void stop(void){}
static risc_driver_v2 driver={2,sizeof(driver),"source-test-volume","storage.test-volume",1,0,start,stop,source_browser_volume_safe};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){if(abi!=2)return 0;driver.capability=source_browser_volume();return &driver;}
