/* Acquisition-policy probe only: no storage operation is implemented/called. */
#include <RiscRuntimeV1.h>
#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <string.h>
#ifdef VOLUME_INSTANCE
#define STR_(value) #value
#define STR(value) STR_(value)
static bool live;
static const unsigned probe[3]={1,sizeof(probe),VOLUME_INSTANCE};
static bool start(const risc_provider_dependency_v1* deps,size_t count) {
  if(count!=1 || strcmp(deps[0].capability_id,"hardware.device") || deps[0].api_version!=1)return false;
  const risc_hardware_device_v1* device=deps[0].api;
  live=device && device->instance_id==VOLUME_INSTANCE;
  return live;
}
static bool quiesce(void){return live;}
static void stop(void){live=false;}
static const risc_driver_v2 driver={2,sizeof(driver),"volume-" STR(VOLUME_INSTANCE),
  "storage.volume",1,probe,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver:0;}
#else
__attribute__((visibility("default"))) void app_main(void) {
  const risc_runtime_api_v1* api=risc_runtime_get_api(1);
  if(!api || api->struct_size<RISC_RUNTIME_CAPABILITIES_V1_SIZE)return;
  risc_runtime_capability_v1 grant={0};grant.struct_size=sizeof(grant);
  if(!api->acquire("storage.volume",1,0,&grant)) {
    api->diagnostic("VOLUME absent");return;
  }
  const unsigned* probe=grant.api;
  if(!probe || probe[0]!=1 || probe[1]!=3*sizeof(unsigned) || (probe[2]!=7 && probe[2]!=8)) {
    api->diagnostic("VOLUME ERROR provider");return;
  }
  const unsigned selected=probe[2],other=selected==7?8:7;
  if(!api->release(&grant) || api->acquire("storage.volume",1,other,&grant) ||
     !api->acquire("storage.volume",1,selected,&grant) || !api->release(&grant)) {
    api->diagnostic("VOLUME ERROR authority");return;
  }
  api->diagnostic(selected==7?"VOLUME 7":"VOLUME 8");
}
#endif
