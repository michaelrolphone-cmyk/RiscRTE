#include <RiscProviderV2.h>
#include <string.h>
#ifndef PROVIDER_ID
#define PROVIDER_ID "leaf"
#endif
extern bool demand_event(const char*,const char*);
static const unsigned api[2]={1,sizeof(api)};
static const unsigned* dependency;
static bool start(const risc_provider_dependency_v1* deps,size_t count) {
#ifdef LEAF
  if(count!=1 || strcmp(deps[0].capability_id,"test.root") || !deps[0].api)return false;
  dependency=deps[0].api;
  if(dependency[0]!=1 || dependency[1]!=sizeof(api))return false;
#else
  (void)deps;if(count)return false;
#endif
  return demand_event(PROVIDER_ID,"start");
}
static bool quiesce(void) {
#ifdef LEAF
  if(!dependency || dependency[0]!=1)return false;
#endif
  return demand_event(PROVIDER_ID,"quiesce");
}
static void stop(void){demand_event(PROVIDER_ID,"stop");dependency=0;}
static const risc_driver_v2 driver={2,sizeof(driver),PROVIDER_ID,"test." PROVIDER_ID,1,api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t version){return version==2?&driver:0;}
