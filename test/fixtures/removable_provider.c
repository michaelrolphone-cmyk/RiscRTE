#include <RiscProviderV2.h>
#include <string.h>
#include <RiscStorageVolumeV1.h>
extern const risc_storage_volume_api_v1* removable_volume(void);
extern const risc_storage_volume_api_v1* removable_internal_volume(void);
extern unsigned removable_event(unsigned,unsigned);
extern void removable_installation(const void*);
extern void removable_bound_settings(const void*);
static bool start(const risc_provider_dependency_v1* d,size_t n) {
#ifdef INTERNAL_PROVIDER
 for(size_t i=0;i<n;++i)if(!strcmp(d[i].capability_id,"platform.installation"))removable_installation(d[i].api);
 return n<=1;
#endif
  for(size_t i=0;i<n;++i)if(d[i].capability_id[0]=='p')removable_installation(d[i].api);
  for(size_t i=0;i<n;++i)if(!strcmp(d[i].capability_id,"storage.key-value.bound"))removable_bound_settings(d[i].api);
  removable_event(2,0);return n==1||n==2||n==3;
}
static bool quiesce(void){
#ifndef INTERNAL_PROVIDER
removable_event(3,0);
#endif
return true;}
static void stop(void){}
static risc_driver_v2 driver={2,sizeof(driver),"sd-test","storage.volume",1,0,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi) {
#ifdef INTERNAL_PROVIDER
 driver.driver_id="internal-test";driver.capability=removable_internal_volume();
#else
 driver.capability=removable_volume();
#endif
 return abi==2?&driver:0;
}
