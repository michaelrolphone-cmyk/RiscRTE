#include <RiscProviderV2.h>
extern bool test_home_quiesce(void);
static const unsigned api[2]={1,sizeof(api)};
static bool start(const risc_provider_dependency_v1* deps,size_t n){(void)deps;return n==0;}
static void stop(void){}
static const risc_driver_v2 driver={2,sizeof(driver),"home-probe","test.home",1,api,start,stop,test_home_quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver:0;}
