#include <RiscProviderV2.h>
#include <string.h>
#ifndef FIXTURE_SLOT
#define FIXTURE_SLOT 0
#endif
#ifndef FIXTURE_ID
#define FIXTURE_ID "poll-0"
#endif
/* Host-owned controls remain borrowed until successful quiescence/stop. */
typedef struct {
  void (*poll)(unsigned,uint32_t);
  bool (*start)(unsigned);
  bool (*quiesce)(unsigned);
  void (*stop)(unsigned);
} control;
static const control* host;
static const unsigned api[2]={1,sizeof(api)};
static bool start(const risc_provider_dependency_v1* deps,size_t count) {
  if(count!=1 || strcmp(deps[0].capability_id,"test.poll-control"))return false;
  host=deps[0].api;
  return host && host->start(FIXTURE_SLOT);
}
static void poll(uint32_t ms){host->poll(FIXTURE_SLOT,ms);}
static bool quiesce(void){return host->quiesce(FIXTURE_SLOT);}
static void stop(void){host->stop(FIXTURE_SLOT);host=0;}
static const risc_driver_poll_v2 driver={{{2,sizeof(driver),FIXTURE_ID,"test.poll",1,&api,start,stop,quiesce},0,0},poll};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver.streams.driver:0;}
