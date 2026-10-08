#include <RiscProviderV2.h>
#include <assert.h>
#include <string.h>
extern const char* retained_yield_mode(void);
extern void retained_yield_event(const char*);
extern void retained_yield_lifecycle(void);
extern void retained_yield_save_provider(const void*);
static unsigned witness;
static unsigned api[2]={1,sizeof(api)};
__attribute__((constructor)) static void loaded(void){retained_yield_event("provider:loaded");}
__attribute__((destructor)) static void unloaded(void){retained_yield_event("provider:unloaded");}
static bool start(const risc_provider_dependency_v1* deps,size_t count){
  (void)deps;assert(!count);retained_yield_save_provider(&witness);
  if(!strcmp(retained_yield_mode(),"invalid-interface"))api[0]=2;
  retained_yield_event("provider:start");retained_yield_lifecycle();return true;
}
static bool quiesce(void){
  retained_yield_event("provider:quiesce");retained_yield_lifecycle();
  return strcmp(retained_yield_mode(),"graph-retained") && strcmp(retained_yield_mode(),"invalid-interface");
}
static void stop(void){retained_yield_event("provider:stop");retained_yield_lifecycle();}
static void poll(uint32_t budget){
  assert(budget && budget<=8);retained_yield_event("provider:poll");retained_yield_lifecycle();
}
static const risc_driver_poll_v2 driver={{{2,sizeof(driver),"yield-provider","test.yield",1,api,start,stop,quiesce},0,0},poll};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver.streams.driver:0;}
