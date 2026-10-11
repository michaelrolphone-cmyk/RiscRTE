#include <RiscProviderV2.h>
#include <assert.h>
extern void *image_pressure_provider_alloc(void);
extern bool image_pressure_provider_quiesce(void);
extern void image_pressure_provider_free(void*);
static void *memory;
static const unsigned api[2]={1,sizeof(api)};
static bool start(const risc_provider_dependency_v1* deps,size_t count) {
    (void)deps;assert(!count && !memory);
    memory=image_pressure_provider_alloc();return memory!=0;
}
static bool quiesce(void) { return image_pressure_provider_quiesce(); }
static void stop(void) { image_pressure_provider_free(memory);memory=0; }
static const risc_driver_v2 driver={2,sizeof(driver),"pressure","test.pressure",1,api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t version){return version==2?&driver:0;}
