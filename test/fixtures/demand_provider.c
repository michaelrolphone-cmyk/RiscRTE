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
#ifdef SYNCHRONOUS_SERVICE
static void service(uint32_t budget){if(budget==RISC_DRIVER_SERVICE_MAX_MS)demand_event(PROVIDER_ID,"service");}
static const risc_driver_service_v2 driver={{{{2,sizeof(driver),PROVIDER_ID,"test." PROVIDER_ID,1,api,start,stop,quiesce},0,0},0},RISC_DRIVER_SERVICE_TAG_V1,RISC_DRIVER_SERVICE_VERSION_V1,service};
__attribute__((visibility("default")))const risc_driver_v2* t5_driver_get(uint32_t version){return version==2?&driver.poll.streams.driver:0;}
#else
static const risc_driver_v2 driver={2,sizeof(driver),PROVIDER_ID,"test." PROVIDER_ID,1,api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t version){return version==2?&driver:0;}

#endif
