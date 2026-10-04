#include <RiscProviderV2.h>
#include <RiscBoundKeyValueV2.h>
#include <string.h>
extern void test_kv2_provider(const risc_bound_key_value_v2*);
static bool start(const risc_provider_dependency_v1* deps,size_t count){
 if(count!=1||strcmp(deps[0].capability_id,RISC_BOUND_KEY_VALUE_CAPABILITY)||deps[0].api_version!=2)return false;
 const risc_bound_key_value_v2* kv=deps[0].api;
 if(!kv||kv->api_version!=2||kv->struct_size!=sizeof(*kv))return false;
 test_kv2_provider(kv);return true;
}
static void stop(void){}
static bool quiesce(void){return true;}
static const uint32_t api[2]={1,sizeof(uint32_t)*2};
static const risc_driver_v2 driver={2,sizeof(driver),"kv2-provider","test.kv2",1,api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t version){return version==2?&driver:0;}
