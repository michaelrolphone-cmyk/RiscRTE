#include "ports/esp32s3/NativeI2s.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
using namespace RiscCpu::NativeI2s;
uint64_t native_sleep_test_output_mask=native_sleep_test_gpio_mask;
static int currentCore=0;static int64_t nowUs=0;
static unsigned failCall=0,calls=0,expectedMode=0,expectedRate=0;
static uint8_t expectedUnit=0;
static bool installed_[2]{},ioError=false,invalidCount=false,unalignedCount=false;
static size_t chunk=1024,available=1024,offset=0;
static uint8_t source[1024];static std::vector<uint8_t> written;
static std::vector<std::string> events;
static esp_err_t event(const std::string& name){events.push_back(name);return ++calls==failCall?ESP_FAIL:ESP_OK;}
int64_t esp_timer_get_time(){return nowUs;}
void vTaskDelay(TickType_t ticks){assert(ticks==1);nowUs+=1000;}
int xPortGetCoreID(){return currentCore;}
extern "C" bool rtc_gpio_is_valid_gpio(gpio_num_t pin){return pin<=21;}
extern "C" esp_err_t rtc_gpio_deinit(gpio_num_t pin){return event("rtc"+std::to_string(pin));}
extern "C" esp_err_t gpio_hold_dis(gpio_num_t pin){return event("hold"+std::to_string(pin));}
extern "C" esp_err_t gpio_set_level(gpio_num_t pin,uint32_t level){assert(pin!=42&&level==0);return event("low"+std::to_string(pin));}
extern "C" esp_err_t gpio_config(const gpio_config_t* config){
 assert(config->pull_up_en==GPIO_PULLUP_DISABLE&&config->pull_down_en==GPIO_PULLDOWN_DISABLE&&config->intr_type==GPIO_INTR_DISABLE);
 const bool input=config->pin_bit_mask==(uint64_t(1)<<42);
 assert(config->mode==(input?GPIO_MODE_INPUT:GPIO_MODE_INPUT_OUTPUT));
 return event(input?"input42":"output");
}
esp_err_t i2s_driver_install(i2s_port_t unit,const i2s_config_t* config,int count,void* queue){
 assert(unit==expectedUnit&&!installed_[unit]&&!count&&!queue);
 assert(config->mode==int(I2S_MODE_MASTER|((expectedMode&1)?I2S_MODE_TX:0)|((expectedMode&2)?I2S_MODE_RX:0)));
 assert(config->sample_rate==expectedRate&&config->mclk_multiple==256&&config->fixed_mclk==0&&config->bits_per_sample==16);
 assert(config->channel_format==I2S_CHANNEL_FMT_RIGHT_LEFT&&config->communication_format==I2S_COMM_FORMAT_STAND_I2S);
 assert(config->dma_buf_len==256&&config->dma_buf_count==2&&config->tx_desc_auto_clear&&!config->use_apll);
 esp_err_t result=event("install");if(result==ESP_OK)installed_[unit]=true;return result;
}
esp_err_t i2s_set_pin(i2s_port_t unit,const i2s_pin_config_t* config){
 assert(unit==expectedUnit&&installed_[unit]);assert(config->mck_io_num==16&&config->bck_io_num==41&&config->ws_io_num==45);
 assert(config->data_out_num==((expectedMode&1)?40:I2S_PIN_NO_CHANGE));
 assert(config->data_in_num==((expectedMode&2)?42:I2S_PIN_NO_CHANGE));return event("connect");
}
esp_err_t i2s_set_pdm_rx_down_sample(i2s_port_t,i2s_pdm_dsr_t){assert(false);return ESP_FAIL;}
esp_err_t i2s_stop(i2s_port_t unit){assert(installed_[unit]);return event("stop");}
esp_err_t i2s_driver_uninstall(i2s_port_t unit){assert(installed_[unit]);auto result=event("uninstall");if(result==ESP_OK)installed_[unit]=false;return result;}
esp_err_t i2s_write(i2s_port_t unit,const void* buffer,size_t bytes,size_t* done,TickType_t ticks){
 assert(unit==expectedUnit&&installed_[unit]&&(expectedMode&1)&&bytes<=1024&&!ticks);event("write");
 *done=invalidCount?bytes+4:unalignedCount?1:std::min(bytes,chunk);
 if(!invalidCount&&!unalignedCount){const auto* b=static_cast<const uint8_t*>(buffer);written.insert(written.end(),b,b+*done);}
 return ioError?ESP_FAIL:ESP_OK;
}
esp_err_t i2s_read(i2s_port_t unit,void* buffer,size_t bytes,size_t* done,TickType_t ticks){
 assert(unit==expectedUnit&&installed_[unit]&&(expectedMode&2)&&bytes<=1024&&!ticks);event("read");
 *done=invalidCount?bytes+4:unalignedCount?1:std::min(std::min(bytes,chunk),available-offset);
 if(!invalidCount&&!unalignedCount){memcpy(buffer,source+offset,*done);offset+=*done;}
 return ioError?ESP_FAIL:ESP_OK;
}
static risc_hw_i2s_standard_v1 config(){risc_hw_i2s_standard_v1 w{};w.struct_size=sizeof(w);w.controller=expectedUnit;w.mclk=16;w.bclk=41;w.ws=45;w.data_out=40;w.data_in=42;return w;}
static void reset(){failCall=0;currentCore=0;assert(close(0)&&close(1)&&idle());calls=0;events.clear();nowUs=0;chunk=available=1024;offset=0;written.clear();ioError=invalidCount=unalignedCount=false;}
int main(){
 for(unsigned i=0;i<sizeof(source);i++)source[i]=i;
 for(uint8_t unit:{0,1})for(unsigned mode:{1,2,3})for(unsigned rate:{8000,16000,22050,32000,44100,48000}){
  expectedUnit=unit;expectedMode=mode;expectedRate=rate;auto wire=config();
  assert(openStandard(wire,mode,rate));assert(!idle()&&!openStandard(wire,mode,rate));
  int16_t samples[512]{};size_t done=55;
  if(mode&1){assert(write(unit,reinterpret_cast<int16_t*>(source),256,&done,40)&&done==256&&written.size()==1024&&!memcmp(written.data(),source,1024));}
  else assert(!write(unit,samples,1,&done,40)&&!done);
  if(mode&2){assert(read(unit,samples,256,&done,40)&&done==256&&!memcmp(samples,source,1024));}
  else assert(!read(unit,samples,1,&done,40)&&!done);
  currentCore=1;unsigned before=calls;assert(!close(unit)&&!write(unit,samples,1,&done,40)&&!read(unit,samples,1,&done,40));assert(calls==before);currentCore=0;
  reset();
 }
 expectedUnit=1;expectedMode=3;expectedRate=16000;auto wire=config();
 assert(openStandard(wire,3,16000));unsigned startCalls=calls;reset();
 for(unsigned fault=1;fault<=startCalls;fault++){failCall=fault;assert(!openStandard(wire,3,16000)&&!idle());reset();}
 assert(openStandard(wire,3,16000));calls=0;assert(close(1));unsigned stopCalls=calls;reset();
 for(unsigned fault=1;fault<=stopCalls;fault++){
  assert(openStandard(wire,3,16000));calls=0;failCall=fault;assert(!close(1)&&!idle());
  unsigned before=calls;int16_t pcm[2]{};size_t done=0;assert(!read(1,pcm,1,&done,10)&&!write(1,pcm,1,&done,10)&&calls==before);reset();
 }
 for(int bad:{-1,22,23,24,25,49,64,255}){auto w=wire;w.mclk=bad;assert(!openStandard(w,3,16000));}
 auto w=wire;w.controller=2;assert(!openStandard(w,3,16000));w=wire;w.data_in=w.ws;assert(!openStandard(w,3,16000));
 assert(!openStandard(wire,0,16000)&&!openStandard(wire,4,16000)&&!openStandard(wire,3,12345));
 assert(openStandard(wire,3,16000));int16_t pcm[512]{};size_t done=55;
 chunk=128;assert(write(1,reinterpret_cast<int16_t*>(source),256,&done,40)&&done==256&&nowUs==7000&&!memcmp(written.data(),source,1024));
 assert(read(1,pcm,256,&done,40)&&done==256&&nowUs==14000&&!memcmp(pcm,source,1024));
 chunk=0;nowUs=0;assert(!write(1,pcm,256,&done,40)&&!done&&nowUs==40000);
 nowUs=0;assert(read(1,pcm,256,&done,40)&&!done&&nowUs==40000);
 chunk=128;available=256;offset=0;nowUs=0;assert(read(1,pcm,256,&done,40)&&done==64&&nowUs==40000);
 ioError=true;offset=0;assert(!read(1,pcm,256,&done,40)&&done==32);assert(!write(1,pcm,256,&done,40)&&done==32);ioError=false;
 invalidCount=true;assert(!read(1,pcm,256,&done,40)&&!done&&!write(1,pcm,256,&done,40));invalidCount=false;
 unalignedCount=true;assert(!read(1,pcm,256,&done,40)&&!done&&!write(1,pcm,256,&done,40));unalignedCount=false;
 for(unsigned bad:{0,41,100})assert(!read(1,pcm,1,&done,bad)&&!write(1,pcm,1,&done,bad));
 assert(!read(1,pcm,257,&done,40)&&!write(1,pcm,257,&done,40));reset();
 puts("Native standard I2S: both controllers/3 directions/6 rates, MCLK, stereo copies, bounded partial I/O, all startup/close faults and legacy isolation PASS");
}
