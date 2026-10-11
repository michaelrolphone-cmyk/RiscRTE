#include <RiscProviderV2.h>
#include <string.h>
#ifndef FIXTURE_SLOT
#define FIXTURE_SLOT 0
#endif
#ifndef FIXTURE_ID
#define FIXTURE_ID "service-0"
#endif
#ifndef SERVICE_TAG
#define SERVICE_TAG RISC_DRIVER_SERVICE_TAG_V1
#endif
#ifndef SERVICE_VERSION
#define SERVICE_VERSION RISC_DRIVER_SERVICE_VERSION_V1
#endif
typedef struct {void(*service)(unsigned,uint32_t);bool(*start)(unsigned);bool(*quiesce)(unsigned);void(*stop)(unsigned);} control;
static const control* host;
static const unsigned api[2]={1,sizeof(api)};
static bool start(const risc_provider_dependency_v1* deps,size_t count){
 if(count!=1 || strcmp(deps[0].capability_id,"test.service-control"))return false;
 host=deps[0].api;return host && host->start(FIXTURE_SLOT);
}
static void service(uint32_t ms){host->service(FIXTURE_SLOT,ms);}
static bool quiesce(void){return host->quiesce(FIXTURE_SLOT);}
static void stop(void){host->stop(FIXTURE_SLOT);host=0;}
static const risc_driver_service_v2 driver={{{{2,sizeof(driver),FIXTURE_ID,"test.service",1,&api,start,stop,quiesce},0,0},0},SERVICE_TAG,SERVICE_VERSION,service};
__attribute__((visibility("default")))const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver.poll.streams.driver:0;}
