/* Target-loader/import fixture only. Never installed in the boot store. */
#include <RiscProviderV2.h>
#include <RiscBoundKeyValueV1.h>
#include <string.h>
static risc_bound_key_value_v1 saved;
static const unsigned api[2]={1,sizeof(api)};
static bool start(const risc_provider_dependency_v1* deps,size_t count) {
  if(count!=1 || strcmp(deps[0].capability_id,RISC_BOUND_KEY_VALUE_CAPABILITY) ||
     deps[0].api_version!=RISC_BOUND_KEY_VALUE_API_V1 || !deps[0].api) return false;
  const risc_bound_key_value_v1* storage=deps[0].api;
  if(storage->api_version!=1 || storage->struct_size<sizeof(*storage) ||
     !storage->context || !storage->get || !storage->put) return false;
  saved.context=storage->context;
  saved.get=storage->get;
  saved.put=storage->put;
  unsigned char data=0; uint32_t size=0;
  const int32_t result=saved.get(saved.context,"state",&data,1,&size);
  if(result!=RISC_BOUND_KEY_VALUE_OK && result!=RISC_BOUND_KEY_VALUE_NOT_FOUND) return false;
  return saved.put(saved.context,"state",&data,1)==RISC_BOUND_KEY_VALUE_OK;
}
static bool quiesce(void) {
  if(!saved.context) return true;
  uint32_t size=1;
  return saved.get(saved.context,"state",0,0,&size)==RISC_BOUND_KEY_VALUE_CONTEXT && !size;
}
static void stop(void) { saved.context=0; }
static const risc_driver_v2 driver={2,sizeof(driver),"bound-storage-probe",
  "test.bound-storage",1,&api,start,stop,quiesce};
__attribute__((visibility("default")))
const risc_driver_v2* t5_driver_get(uint32_t abi) {return abi==2?&driver:0;}
