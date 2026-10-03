#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <string.h>
static const risc_hardware_device_v1* hardware;
static const unsigned api[2]={1,sizeof(api)};
static bool start(const risc_provider_dependency_v1* deps,size_t count) {
  if(count!=1 || strcmp(deps[0].capability_id,"hardware.device") || deps[0].api_version!=1) return false;
  hardware=deps[0].api;
  if(!hardware || hardware->instance_id!=7 || strcmp(hardware->compatible,"test,gpio") || strcmp(hardware->config_type,"gpio.bank") || hardware->config_size!=sizeof(risc_hw_gpio_bank_v1)) return false;
  const risc_hw_gpio_bank_v1* config=hardware->config;
  return config->count==1 && config->pins[0]==5;
}
static bool quiesce(void) { return hardware && hardware->instance_id==7; }
static void stop(void) { hardware=0; }
#ifdef YIELD_PROVIDER
extern void test_yield_poll(uint32_t);
static const risc_driver_poll_v2 extended={{{2,sizeof(extended),"probe","test.probe",1,&api,start,stop,quiesce},0,0},test_yield_poll};
#define driver extended.streams.driver
#else
static const risc_driver_v2 driver={2,sizeof(driver),"probe","test.probe",1,&api,start,stop,quiesce};
#endif
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi) {return abi==2?&driver:0;}
