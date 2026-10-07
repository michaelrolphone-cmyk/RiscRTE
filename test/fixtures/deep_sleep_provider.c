/* Test-only dynamic provider. No product pin map or policy is compiled in. */
#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <GardenPlatformV1.h>
#include <string.h>
#ifdef TEST_PROVIDER_REALTIME
#include <RiscPlatformRealtimeV1.h>
#include <assert.h>
extern void test_time_provider(const risc_platform_realtime_api_v1*);
static const risc_platform_realtime_api_v1* realtime;
#endif
extern bool test_deep_timed(void);
extern void test_deep_trace(const char *);
static const garden_gpio_v1 *gpio;
static uint64_t input,output;
static int32_t enter(void){
#ifdef TEST_PROVIDER_REALTIME
 risc_realtime_snapshot_v1 sample={.struct_size=sizeof(sample)};
 assert(realtime && realtime->read(realtime->context,&sample)==RISC_REALTIME_OK);
 const risc_realtime_snapshot_v1 before=sample;
#endif
 if(gpio->deep_sleep_hold(gpio->context,output,true)!=0)return RISC_DEEP_SLEEP_PLATFORM;
#ifdef TEST_PROVIDER_REALTIME
 assert(realtime->read(realtime->context,&sample)==RISC_REALTIME_CONTEXT && !memcmp(&sample,&before,sizeof(sample)));
#endif
 const int32_t result=test_deep_timed()?gpio->deep_sleep_for(gpio->context,input,false,123):gpio->deep_sleep(gpio->context,input,false);
 if(gpio->deep_sleep_hold(gpio->context,output,false)!=0)return RISC_DEEP_SLEEP_RETAINED;
 return result;
}
static bool start(const risc_provider_dependency_v1 *d,size_t count){
 const risc_hardware_device_v1 *hw=0;
 for(size_t i=0;i<count;i++){
  if(!strcmp(d[i].capability_id,"platform.gpio"))gpio=d[i].api;
  if(!strcmp(d[i].capability_id,"hardware.device"))hw=d[i].api;
#ifdef TEST_PROVIDER_REALTIME
  if(!strcmp(d[i].capability_id,RISC_PLATFORM_REALTIME_CAPABILITY)){realtime=d[i].api;test_time_provider(realtime);}
#endif
 }
 if(!hw || !gpio || gpio->struct_size<GARDEN_GPIO_DEEP_SLEEP_HOLD_V1_SIZE || !gpio->deep_sleep || !gpio->deep_sleep_hold)return false;
 if(test_deep_timed() && (gpio->struct_size<GARDEN_GPIO_DEEP_SLEEP_FOR_V1_SIZE || !gpio->deep_sleep_for))return false;
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
