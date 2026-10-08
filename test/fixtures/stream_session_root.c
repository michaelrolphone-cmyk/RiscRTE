#include <RiscProviderV2.h>
extern void stream_test_event(const char*);
extern void stream_test_root(const void*);
static const uint32_t api[2]={1,sizeof(api)};
static bool start(const risc_provider_dependency_v1* deps,size_t count){(void)deps;stream_test_event("root:start");stream_test_root(api);return !count;}
static bool quiesce(void){stream_test_event("root:quiesce");return true;}
static void stop(void){stream_test_event("root:stop");}
static const risc_driver_v2 driver={2,sizeof(driver),"stream-root","test.root",1,api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver:0;}
