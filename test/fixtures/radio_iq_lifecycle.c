#include <RiscProviderV2.h>
#include <RiscRuntimeV1.h>
#include <RiscRadioIqResourceV1.h>
#include <assert.h>
#include <string.h>
extern void test_iq_trace(const char*);
extern const char* test_iq_mode(void);
extern void test_iq_dirty(bool);
extern void test_iq_save(const void*,const void*);
typedef struct { uint32_t api_version,struct_size; bool (*claim)(void); bool (*release)(void); } probe_api;
#ifdef IQ_PROVIDER
static const risc_radio_iq_resource_v1* raw;
static uint64_t lease;
static bool claim(void){return raw->claim(raw->context,&lease);}
static bool release(void){if(!lease)return true;if(!raw->release(raw->context,lease))return false;lease=0;return true;}
static const probe_api api={1,sizeof(api),claim,release};
static bool start(const risc_provider_dependency_v1* deps,size_t count){
 for(size_t i=0;i<count;++i)if(!strcmp(deps[i].capability_id,RISC_RADIO_IQ_RESOURCE_CAPABILITY))raw=deps[i].api;
 test_iq_trace("provider start");return raw && raw->api_version==1 && raw->struct_size>=sizeof(*raw);
}
static bool quiesce(void){if(!release())return false;test_iq_trace("provider quiesce");return true;}
static void stop(void){test_iq_trace("provider stop");raw=0;}
static const risc_driver_v2 driver={2,sizeof(driver),"iq-probe","test.iq",1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver:0;}
__attribute__((destructor)) static void unloaded(void){test_iq_trace("provider unload");}
#else
__attribute__((visibility("default"))) int app_module_init(void){test_iq_trace("app init");return 0;}
__attribute__((visibility("default"))) void app_module_fini(void){test_iq_trace("app fini");}
__attribute__((destructor)) static void unloaded(void){test_iq_trace("app unload");}
__attribute__((visibility("default"))) void app_main(void){
 const risc_runtime_api_v1* rt=risc_runtime_get_api(1);assert(rt);
 risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)};
 assert(rt->acquire("test.iq",1,0,&grant));const probe_api* api=grant.api;
 test_iq_save((const void*)app_main,grant.api);
 const char* mode=test_iq_mode();
 if(!strcmp(mode,"refused")){test_iq_dirty(true);assert(!api->claim());test_iq_dirty(false);}
 else if(strcmp(mode,"lazy")){
  assert(api->claim());
  if(!strcmp(mode,"retry") || !strcmp(mode,"retained")){
   test_iq_dirty(true);assert(!api->release());
   if(!strcmp(mode,"retry")){test_iq_dirty(false);assert(api->release());}
  }else if(strcmp(mode,"unreleased"))assert(api->release());
 }
 assert(rt->release(&grant));
}
#endif
