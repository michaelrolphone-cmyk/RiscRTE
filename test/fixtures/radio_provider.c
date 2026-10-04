#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <GardenPlatformV1.h>
#include "radio_test_api.h"
#include <assert.h>
#include <string.h>
extern void test_radio_trace(const char*);
static const risc_hardware_device_v1* hardware;
static uint64_t token;
#ifdef SLEEP_PROVIDER
static const garden_gpio_v1* raw;
#define ID "sleep-probe"
#define CAP "test.sleep"
static int32_t enter(bool deep){
 if(deep)return raw->deep_sleep(raw->context,token,false);
 risc_light_sleep_result_v1 out={sizeof(out),0};
 return raw->light_sleep(raw->context,token,false,&out);
}
static const test_radio_sleep_v1 api={1,sizeof(api),enter};
#else
static const garden_radio_v1* raw;
#define ID "radio-probe"
#define CAP "test.radio"
static bool join(void){return raw->join(raw->context,token,"synthetic-test","synthetic-pass");}
static bool scan(void){return raw->scan_start(raw->context,token);}
static bool cancel(void){return raw->scan_cancel(raw->context,token);}
static bool leave(void){return raw->leave(raw->context,token);}
static bool state(void){uint8_t value=0;int8_t rssi=0;return raw->state(raw->context,token,&value,&rssi);}
static bool addresses(void){uint8_t station[12],ap[12];return raw->addresses(raw->context,token,station,ap);}
static bool poll(void){garden_radio_scan_result_v1 result={.struct_size=sizeof(result)};return raw->scan_poll(raw->context,token,&result);}
static const test_radio_api_v1 api={1,sizeof(api),join,scan,cancel,leave,state,addresses,poll};
#endif
__attribute__((constructor)) static void loaded(void){test_radio_trace(ID " loaded");}
__attribute__((destructor)) static void unloaded(void){test_radio_trace(ID " unloaded");}
static bool start(const risc_provider_dependency_v1* deps,size_t count){
 for(size_t i=0;i<count;++i){
  if(!strcmp(deps[i].capability_id,"hardware.device"))hardware=deps[i].api;
#ifdef SLEEP_PROVIDER
  if(!strcmp(deps[i].capability_id,"platform.gpio"))raw=deps[i].api;
#else
  if(!strcmp(deps[i].capability_id,"platform.radio"))raw=deps[i].api;
#endif
 }
 if(!hardware || !raw || token)return false;
#ifdef SLEEP_PROVIDER
 const risc_hw_gpio_bank_v1* config=hardware->config;
 if(strcmp(hardware->config_type,"gpio.bank") || config->count!=1 || !raw->claim(raw->context,config->pins[0],false,false,true,&token))return false;
#else
 const risc_hw_radio_v1* config=hardware->config;
 if(strcmp(hardware->config_type,"radio.integrated") || config->unit || !(config->features&1) || raw->struct_size<GARDEN_RADIO_SCAN_V1_SIZE || !raw->claim(raw->context,&token))return false;
#endif
 test_radio_trace(ID " started");return true;
}
static bool quiesce(void){
 if(!token)return true;
#ifndef SLEEP_PROVIDER
 if(!raw->leave(raw->context,token))return false;
#endif
 if(!raw->release(raw->context,token))return false;
 token=0;test_radio_trace(ID " quiesced");return true;
}
static void stop(void){test_radio_trace(ID " stopped");raw=NULL;hardware=NULL;}
static const risc_driver_v2 driver={2,sizeof(driver),ID,CAP,1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
