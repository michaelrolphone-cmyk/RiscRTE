/* Synthetic storage provider: production Runtime owns hardware admission and
 * lifecycle. This does not represent or qualify either product's SD driver. */
#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <assert.h>
#include <string.h>
extern const void* serial_product_backend(void);
extern void serial_product_started(void);
extern bool serial_product_quiesce(void);
extern void serial_product_stopped(void);
static bool start(const risc_provider_dependency_v1* deps,size_t count){
  assert(count==1 && !strcmp(deps[0].capability_id,"hardware.device") && deps[0].api_version==1);
  const risc_hardware_device_v1* hardware=deps[0].api;
  assert(hardware && hardware->instance_id==TEST_PRODUCT_INSTANCE);
  serial_product_started();return true;
}
static risc_driver_v2 driver={2,sizeof(driver),TEST_PRODUCT_PROVIDER,"storage.volume",1,0,
                             start,serial_product_stopped,serial_product_quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){
  if(abi!=2)return 0;
  driver.capability=serial_product_backend();return &driver;
}
