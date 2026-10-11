#include <RiscProviderV2.h>
#include <RiscStorageVolumeV1.h>
extern const risc_storage_volume_api_v1* removable_volume(void);
extern unsigned removable_event(unsigned,unsigned);
extern void removable_installation(const void*);
static bool start(const risc_provider_dependency_v1* d,size_t n) {
  for(size_t i=0;i<n;++i)if(d[i].capability_id[0]=='p')removable_installation(d[i].api);
  removable_event(2,0);return n==1||n==2;
}
static bool quiesce(void){removable_event(3,0);return true;}
static void stop(void){}
static risc_driver_v2 driver={2,sizeof(driver),"sd-test","storage.volume",1,0,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi) {
  driver.capability=removable_volume();return abi==2?&driver:0;
}
