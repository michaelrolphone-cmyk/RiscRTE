/* Bound-KV operations cross the real broker while a separate provider owns DMA. */
#include <RiscProviderV2.h>
#include <RiscBoundKeyValueV1.h>
#include "i2s_test_api.h"
#include <string.h>
static const risc_bound_key_value_v1* kv;
static bool check(bool expected){
 char value=0;uint32_t size=0;
 const int32_t get=kv->get(kv->context,"alarm_occ",&value,1,&size);
 const int32_t put=kv->put(kv->context,"alarm_occ","x",1);
 if(expected)return get==RISC_BOUND_KEY_VALUE_OK && value=='x' && size==1 && put==RISC_BOUND_KEY_VALUE_OK;
 return get==RISC_BOUND_KEY_VALUE_CONTEXT && !size && put==RISC_BOUND_KEY_VALUE_CONTEXT;
}
static bool start(const risc_provider_dependency_v1* deps,size_t count){
 if(count!=1 || strcmp(deps[0].capability_id,RISC_BOUND_KEY_VALUE_CAPABILITY))return false;
 kv=deps[0].api;return kv && check(true);
}
static bool quiesce(void){return check(false);}
static void stop(void){kv=NULL;}
static const test_i2s_storage_v1 api={1,sizeof(api),check};
static const risc_driver_v2 driver={2,sizeof(driver),"i2s-storage-probe","test.alert",1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
