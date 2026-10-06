#include <RiscProviderV2.h>
#ifndef SLOT
#error SLOT required
#endif
#define STR1(x) #x
#define STR(x) STR1(x)
static const unsigned api[3]={1,sizeof(api),SLOT};
static bool start(const risc_provider_dependency_v1* deps,size_t count){(void)deps;return count==0;}
static bool quiesce(void){return true;}
static void stop(void){}
static const risc_driver_v2 driver={2,sizeof(driver),"slot" STR(SLOT),"test.slot" STR(SLOT),1,api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver:0;}
