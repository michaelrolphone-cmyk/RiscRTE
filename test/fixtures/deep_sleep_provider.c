/* Test-only dynamic provider. No product pin map or policy is compiled in. */
#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <GardenPlatformV1.h>
#include <string.h>
extern void test_deep_trace(const char *);
static const garden_gpio_v1 *gpio;
static uint64_t input,output;
static int32_t enter(void){
 if(gpio->deep_sleep_hold(gpio->context,output,true)!=0)return RISC_DEEP_SLEEP_PLATFORM;
 const int32_t result=gpio->deep_sleep(gpio->context,input,false);
 if(gpio->deep_sleep_hold(gpio->context,output,false)!=0)return RISC_DEEP_SLEEP_RETAINED;
 return result;
}
static bool start(const risc_provider_dependency_v1 *d,size_t count){
 const risc_hardware_device_v1 *hw=0;
 for(size_t i=0;i<count;i++){
  if(!strcmp(d[i].capability_id,"platform.gpio"))gpio=d[i].api;
  if(!strcmp(d[i].capability_id,"hardware.device"))hw=d[i].api;
 }
 if(!hw || !gpio || gpio->struct_size<GARDEN_GPIO_DEEP_SLEEP_HOLD_V1_SIZE || !gpio->deep_sleep || !gpio->deep_sleep_hold)return false;
 const risc_hw_gpio_bank_v1 *c=hw->config;
 if(c->count!=2 || !gpio->claim(gpio->context,c->pins[0],false,false,true,&input) ||
    !gpio->claim(gpio->context,c->pins[1],true,false,false,&output))return false;
 test_deep_trace("PROVIDER fresh");return true;
}
static bool quiesce(void){
 if(output && !gpio->release(gpio->context,output))return false;
 output=0;
 if(input && !gpio->release(gpio->context,input))return false;
 input=0;
 test_deep_trace("PROVIDER quiesced");return true;
}
static void stop(void){test_deep_trace("PROVIDER stopped");gpio=0;}
static const struct {uint32_t version,size;int32_t(*enter)(void);} api={1,sizeof(api),enter};
static const risc_driver_v2 driver={2,sizeof(driver),"deep-probe","test.deep",1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi){return abi==2?&driver:0;}
