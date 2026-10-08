#include <RiscProviderV2.h>
#include <RiscProviderSyncV1.h>
#include <RiscHardwareConfigV1.h>
#include "provider_sync_api.h"
#include <string.h>
extern void test_sync_trace(const char*);
extern const char* test_sync_mode(void);
static const risc_provider_sync_api_v1* sync;
static uint64_t token;
static bool held;
static bool hold(void){if(!sync || !token || held || !sync->try_lock(sync->context,token))return false;held=true;return true;}
static bool release(void){if(!held)return true;if(!sync->unlock(sync->context,token))return false;held=false;return true;}
static const test_sync_api api={1,sizeof(api),hold,release};
__attribute__((destructor)) static void unloaded(void){test_sync_trace("provider-unloaded");}
static bool start(const risc_provider_dependency_v1* deps,size_t n){
 const risc_hardware_device_v1* hardware=NULL;
 for(size_t i=0;i<n;++i){
  if(!strcmp(deps[i].capability_id,"hardware.device"))hardware=deps[i].api;
  if(!strcmp(deps[i].capability_id,RISC_PROVIDER_SYNC_CAPABILITY))sync=deps[i].api;
 }
 if(!hardware || !sync || strcmp(hardware->config_type,"gpio.bank") || sync->struct_size<sizeof(*sync) || !sync->is_owner(sync->context))return false;
 return sync->create(sync->context,&token);
}
static bool quiesce(void){
 if(held && !strcmp(test_sync_mode(),"retained"))return false;
 if(!release() || (token && !sync->destroy(sync->context,token)))return false;
 token=0;return true;
}
static void stop(void){sync=NULL;test_sync_trace("provider-stopped");}
static const risc_driver_v2 driver={2,sizeof(driver),"sync-probe","test.sync",1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
