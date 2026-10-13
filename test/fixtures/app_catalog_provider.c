#include <RiscProviderV2.h>
#include <RiscStorageVolumeV1.h>
#include <string.h>
extern void app_catalog_test_installer(const void*);
static risc_storage_volume_api_v1 volume={.api_version=1,.struct_size=sizeof(volume)};
static bool start(const risc_provider_dependency_v1* deps,size_t count) {
    if(count!=2)return false;
    for(size_t i=0;i<count;++i)
        if(!strcmp(deps[i].capability_id,"platform.installation"))app_catalog_test_installer(deps[i].api);
    return true;
}
static void stop(void) {}
static bool quiesce(void) {return true;}
static const risc_driver_v2 driver={2,sizeof(driver),"catalog-volume","storage.volume",1,&volume,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi) {return abi==2?&driver:0;}
