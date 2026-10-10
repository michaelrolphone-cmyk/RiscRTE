#include "RiscProviderV2.h"
extern const void *capacity_provider(const char *);
static bool start(const risc_provider_dependency_v1 *d,size_t n){(void)d;(void)n;return true;}
static void stop(void){}
static bool quiesce(void){return true;}
static risc_driver_v2 driver={2,sizeof(driver),CAPACITY_PROVIDER_ID,CAPACITY_PROVIDER_CAP,CAPACITY_PROVIDER_VERSION,0,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi){if(abi!=2)return 0;driver.capability=capacity_provider(CAPACITY_PROVIDER_CAP);return &driver;}
