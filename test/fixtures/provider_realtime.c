#include <RiscProviderV2.h>
#include <RiscPlatformRealtimeV1.h>
#include <RiscBoundKeyValueV1.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#ifndef PROVIDER_INDEX
#define PROVIDER_INDEX 0
#endif
_Static_assert(sizeof(risc_platform_realtime_api_v1)==sizeof(risc_realtime_api_v1),"canonical readonly table");
_Static_assert(sizeof(risc_platform_realtime_api_v1)<sizeof(risc_realtime_control_api_v1),"no seed suffix");
_Static_assert(sizeof(risc_realtime_snapshot_v1)==40,"canonical snapshot");
extern void test_provider_event(unsigned,const char*,const risc_platform_realtime_api_v1*);
extern int test_provider_failure(unsigned);
static risc_platform_realtime_api_v1 clock;
static int32_t read_time(risc_realtime_snapshot_v1* out){return clock.read(clock.context,out);}
static bool start(const risc_provider_dependency_v1* deps,size_t count){
 assert((count==1 || count==2) && deps && !strcmp(deps[0].capability_id,RISC_PLATFORM_REALTIME_CAPABILITY) && deps[0].api_version==1);
 clock=*(const risc_platform_realtime_api_v1*)deps[0].api;
 assert(clock.api_version==1 && clock.struct_size==sizeof(clock) && clock.context && clock.read);
 test_provider_event(PROVIDER_INDEX,"start",&clock);
 if(count==2){
  assert(!strcmp(deps[1].capability_id,RISC_BOUND_KEY_VALUE_CAPABILITY));
  const risc_bound_key_value_v1* kv=deps[1].api;char byte=0;uint32_t size=0;
  assert(kv->get(kv->context,"k8",&byte,1,&size)==0 && size==1 && byte==42);
 }
 return test_provider_failure(PROVIDER_INDEX)!=1;
}
static bool quiesce(void){test_provider_event(PROVIDER_INDEX,"quiesce",&clock);return !test_provider_failure(PROVIDER_INDEX);}
static void stop(void){test_provider_event(PROVIDER_INDEX,"stop",&clock);}
static void poll(uint32_t budget){assert(budget && budget<=8);test_provider_event(PROVIDER_INDEX,"poll",&clock);}
static const struct {uint32_t version,size;int32_t(*read)(risc_realtime_snapshot_v1*);} api={1,sizeof(api),read_time};
static const risc_driver_poll_v2 descriptor={{{2,sizeof(descriptor),PROVIDER_INDEX?"time-second":"time-first",PROVIDER_INDEX?"test.time.second":"test.time.first",1,&api,start,stop,quiesce},0,0},poll};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t version){return version==2?&descriptor.streams.driver:0;}
__attribute__((destructor)) static void unloaded(void){test_provider_event(PROVIDER_INDEX,"unload",&clock);}
