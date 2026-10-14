#include <RiscProviderV2.h>
#include <RiscBoundKeyValueV1.h>
#include <assert.h>
#include <string.h>
#ifndef ATOMIC_READER_VERSION
#define ATOMIC_READER_VERSION 1
#endif
extern void test_atomic_bound_started(unsigned,risc_bound_key_value_v1);
extern void test_atomic_bound_stopped(risc_bound_key_value_v1);
static risc_bound_key_value_v1 storage;
static bool start(const risc_provider_dependency_v1*deps,size_t count){
 assert(count==1&&!strcmp(deps[0].capability_id,RISC_BOUND_KEY_VALUE_CAPABILITY));
 storage=*(const risc_bound_key_value_v1*)deps[0].api;
 assert(storage.api_version==ATOMIC_READER_VERSION);test_atomic_bound_started(ATOMIC_READER_VERSION,storage);return true;
}
static bool quiesce(void){test_atomic_bound_stopped(storage);return true;}
static void stop(void){test_atomic_bound_stopped(storage);}
static const uint32_t api[]={1,2*sizeof(uint32_t)};
static const risc_driver_v2 descriptor={2,sizeof(descriptor),ATOMIC_READER_VERSION==1?"atomic-reader-1":"atomic-reader-2",ATOMIC_READER_VERSION==1?"test.atomic.reader1":"test.atomic.reader2",1,api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t v){return v==2?&descriptor:0;}
