/* The production Runtime maps two independent instances of this fixture. */
#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <TWatchHardwareV1.h>
#include <TWatchPlatformV1.h>
#include "i2s_test_api.h"
#include <string.h>
extern void test_i2s_trace(const char*);
static const twatch_i2s_controller_v1* raw;
static const tw_hw_audio_v1* config;
static uint64_t token;
static bool open_audio(void){return raw->open(raw->context,config->controller,config->pdm_rx,config->bclk,config->ws,config->data,16000,config->pdm_rx?1:2,&token);}
static bool transfer(void){int16_t pcm[512]={0};size_t done=0;return config->pdm_rx?raw->read(raw->context,token,pcm,256,&done,40):raw->write(raw->context,token,pcm,256,&done,40);}
static bool close_audio(void){if(!token)return true;if(!raw->close(raw->context,token))return false;token=0;return true;}
static bool start(const risc_provider_dependency_v1* deps,size_t count){
 const risc_hardware_device_v1* hw=NULL;
 for(size_t i=0;i<count;i++){
  if(!strcmp(deps[i].capability_id,"hardware.device"))hw=deps[i].api;
  if(!strcmp(deps[i].capability_id,"platform.i2s.controller"))raw=deps[i].api;
 }
 if(!hw || !raw || strcmp(hw->config_type,"audio.i2s"))return false;
 config=hw->config;test_i2s_trace("audio started");return true;
}
static bool quiesce(void){if(!close_audio())return false;test_i2s_trace("audio quiesced");return true;}
static void stop(void){raw=NULL;config=NULL;}
static const test_i2s_v1 api={1,sizeof(api),open_audio,transfer,close_audio};
static const risc_driver_v2 driver={2,sizeof(driver),"i2s-probe","test.audio",1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}
