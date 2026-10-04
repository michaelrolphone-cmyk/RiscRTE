#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <GardenPlatformV1.h>
#include "i2s_test_api.h"
#include <string.h>
static const garden_gpio_v1* raw;static uint64_t token;
static int32_t enter(bool deep){if(deep)return raw->deep_sleep(raw->context,token,false);risc_light_sleep_result_v1 out={sizeof(out),0};return raw->light_sleep(raw->context,token,false,&out);}
static bool start(const risc_provider_dependency_v1* deps,size_t count){
 const risc_hardware_device_v1* hw=NULL;
 for(size_t i=0;i<count;i++){
  if(!strcmp(deps[i].capability_id,"hardware.device"))hw=deps[i].api;
  if(!strcmp(deps[i].capability_id,"platform.gpio"))raw=deps[i].api;
 }
 if(!hw || !raw || strcmp(hw->config_type,"gpio.bank"))return false;
 const risc_hw_gpio_bank_v1* config=hw->config;
 return config->count==1 && raw->claim(raw->context,config->pins[0],false,false,true,&token);
}
static bool quiesce(void){if(token && !raw->release(raw->context,token))return false;token=0;return true;}
static void stop(void){raw=NULL;}
static const test_i2s_sleep_v1 api={1,sizeof(api),enter};
static const risc_driver_v2 driver={2,sizeof(driver),"i2s-sleep-probe","test.sleep",1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
