#include "ports/esp32s3/NativeI2s.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
using namespace RiscCpu::NativeI2s;
uint64_t native_sleep_test_output_mask=native_sleep_test_gpio_mask;
static std::vector<std::string> calls;
static std::string failure;
static bool installed[2]{};
static int core=0;static int64_t now=0;
static size_t limit=512,sourceOffset=0;static bool readError=false,badCount=false,unaligned=false;
static int16_t source[256];
static esp_err_t event(const std::string& s){calls.push_back(s);return failure==s?ESP_FAIL:ESP_OK;}
int64_t esp_timer_get_time(){return now;}
void vTaskDelay(TickType_t t){assert(t==1);now+=1000;}
int xPortGetCoreID(){return core;}
extern "C" bool rtc_gpio_is_valid_gpio(gpio_num_t pin){return pin<=21;}
extern "C" esp_err_t rtc_gpio_deinit(gpio_num_t pin){return event("rtc"+std::to_string(pin));}
extern "C" esp_err_t gpio_hold_dis(gpio_num_t pin){return event("hold"+std::to_string(pin));}
extern "C" esp_err_t gpio_set_level(gpio_num_t pin,uint32_t value){assert(pin!=11 && value==0);return event("low"+std::to_string(pin));}
extern "C" esp_err_t gpio_config(const gpio_config_t* c){
 assert(c->pull_up_en==GPIO_PULLUP_DISABLE && c->pull_down_en==GPIO_PULLDOWN_DISABLE && c->intr_type==GPIO_INTR_DISABLE);
 const bool input=c->pin_bit_mask==(uint64_t(1)<<11);assert(c->mode==(input?GPIO_MODE_INPUT:GPIO_MODE_OUTPUT));
 return event(input?"input":"output");
}
esp_err_t i2s_driver_install(i2s_port_t p,const i2s_config_t* c,int q,void* queue){
 assert(!installed[p] && !q && !queue && c->bits_per_sample==16 && c->dma_buf_len==256 && c->dma_buf_count==2 && !c->use_apll);
 if(p==0){assert(c->mode==(I2S_MODE_MASTER|I2S_MODE_RX|I2S_MODE_PDM) && (c->sample_rate==8000 || c->sample_rate==16000) && c->channel_format==I2S_CHANNEL_FMT_ONLY_LEFT && !c->tx_desc_auto_clear);}
 else {assert(c->mode==(I2S_MODE_MASTER|I2S_MODE_TX) && c->channel_format==I2S_CHANNEL_FMT_RIGHT_LEFT && c->tx_desc_auto_clear);}
 auto result=event("install");if(result==ESP_OK)installed[p]=true;return result;
}
esp_err_t i2s_set_pdm_rx_down_sample(i2s_port_t p,i2s_pdm_dsr_t dsr){assert(p==0 && installed[p] && dsr==I2S_PDM_DSR_16S);return event("decimation");}
esp_err_t i2s_set_pin(i2s_port_t p,const i2s_pin_config_t* c){
 assert(installed[p] && c->mck_io_num==-1);
 if(p==0)assert(c->bck_io_num==-1 && c->ws_io_num==10 && c->data_out_num==-1 && c->data_in_num==11);
 else assert(c->bck_io_num==7 && c->ws_io_num==8 && c->data_out_num==9 && c->data_in_num==-1);
 return event("set_pin");
}
esp_err_t i2s_stop(i2s_port_t p){assert(installed[p]);return event("stop");}
esp_err_t i2s_driver_uninstall(i2s_port_t p){assert(installed[p]);auto result=event("uninstall");if(result==ESP_OK)installed[p]=false;return result;}
esp_err_t i2s_read(i2s_port_t p,void* b,size_t n,size_t* done,TickType_t ticks){
 assert(p==0 && installed[p] && n<=512 && !ticks);event("read");
 *done=badCount?n+2:unaligned?1:std::min(n,limit);
 if(!badCount && !unaligned){assert(sourceOffset+*done<=sizeof(source));memcpy(b,reinterpret_cast<uint8_t*>(source)+sourceOffset,*done);sourceOffset+=*done;}
 return readError?ESP_FAIL:ESP_OK;
}
esp_err_t i2s_write(i2s_port_t,const void*,size_t,size_t*,TickType_t){assert(false);return ESP_FAIL;}
static bool called(const char* s){return std::find(calls.begin(),calls.end(),s)!=calls.end();}
static void reset(){failure.clear();core=0;assert(close(0));assert(close(1));calls.clear();now=0;sourceOffset=0;limit=512;readError=badCount=unaligned=false;}
int main(){
 for(unsigned i=0;i<256;++i)source[i]=int16_t(i*127-16000);
 assert(idle());
 for(uint8_t p:{22,23,24,25,49,64,255})assert(!openRx(0,p,11,16000) && !openRx(0,10,p,16000));
 assert(!openRx(1,10,11,16000) && !openRx(2,10,11,16000) && !openRx(0,10,10,16000) && !openRx(0,10,11,44100));assert(calls.empty());
 native_sleep_test_output_mask&=~(uint64_t(1)<<11);assert(openRx(0,10,11,8000));reset();native_sleep_test_output_mask=native_sleep_test_gpio_mask;
 for(const char* fail:{"rtc11","input","hold11","rtc10","low10","output","hold10","install","decimation","set_pin"}){
  failure=fail;assert(!openRx(0,10,11,16000));assert(!idle());reset();
 }
 assert(openRx(0,10,11,16000));assert(open(1,7,8,9,8000));assert(!openRx(0,10,11,16000));
 int16_t pcm[256]{};size_t done=99;
 core=1;calls.clear();assert(!read(0,pcm,256,&done,40) && !done && !close(0));assert(calls.empty());core=0;
 assert(!write(0,pcm,1,&done,40) && !read(1,pcm,1,&done,40));
 assert(!read(0,pcm,257,&done,40) && !read(0,pcm,256,&done,0) && !read(0,pcm,256,&done,41) && !read(0,nullptr,256,&done,40));
 assert(read(0,pcm,256,&done,40) && done==256 && !memcmp(pcm,source,sizeof(pcm)));sourceOffset=0;limit=64;now=0;
 assert(read(0,pcm,256,&done,40) && done==256 && now==7000 && !memcmp(pcm,source,sizeof(pcm)));
 sourceOffset=0;limit=0;now=0;assert(!read(0,pcm,256,&done,40) && !done && now==40000);
 sourceOffset=0;limit=64;readError=true;assert(!read(0,pcm,256,&done,40) && done==32);readError=false;
 badCount=true;assert(!read(0,pcm,256,&done,40) && !done);badCount=false;unaligned=true;assert(!read(0,pcm,256,&done,40) && !done);unaligned=false;
 assert(close(0) && installed[1] && !idle());assert(close(1) && idle());reset();
 for(const char* fail:{"stop","uninstall","input","output","rtc10","rtc11","hold10","hold11","low10"}){
  assert(openRx(0,10,11,16000));calls.clear();failure=fail;assert(!close(0) && !idle());
  assert(called("rtc11") && called("rtc10"));if(failure=="stop")assert(!called("uninstall") && installed[0]);
  calls.clear();assert(!read(0,pcm,1,&done,40) && !done && calls.empty());reset();
 }
 puts("Native PDM RX: mono/128x/WS clock, input-only data, bounded exact/partial reads, direction/core ownership, independent TX and retained cleanup PASS");
}
