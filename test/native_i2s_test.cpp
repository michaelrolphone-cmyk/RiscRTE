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
static bool installed=false,pendingDisplay=true;
static int core=0;static int64_t now=0;
static size_t limit=1024;static bool writeError=false,badCount=false,unaligned=false;
static std::vector<uint8_t> accepted;
static esp_err_t event(const char* s){calls.emplace_back(s);return failure==s?ESP_FAIL:ESP_OK;}
int64_t esp_timer_get_time(){return now;}
void vTaskDelay(TickType_t t){assert(t==1);now+=1000;}
int xPortGetCoreID(){return core;}
extern "C" bool rtc_gpio_is_valid_gpio(gpio_num_t pin){return pin<=21;}
extern "C" esp_err_t rtc_gpio_deinit(gpio_num_t){return event("rtc_deinit");}
extern "C" esp_err_t gpio_hold_dis(gpio_num_t){return event("hold_dis");}
extern "C" esp_err_t gpio_set_level(gpio_num_t p,uint32_t v){assert(p==7 || p==8 || p==9);assert(v==0);return event(("low"+std::to_string(p)).c_str());}
extern "C" esp_err_t gpio_config(const gpio_config_t* c){assert(c->mode==GPIO_MODE_OUTPUT && c->pull_up_en==GPIO_PULLUP_DISABLE && c->pull_down_en==GPIO_PULLDOWN_DISABLE && c->intr_type==GPIO_INTR_DISABLE);return event("gpio_config");}
esp_err_t i2s_driver_install(i2s_port_t p,const i2s_config_t* c,int q,void* queue){
 assert(p==1 && !installed && q==0 && !queue);assert(c->mode==(I2S_MODE_MASTER|I2S_MODE_TX) && c->sample_rate==8000 && c->bits_per_sample==16 && c->channel_format==I2S_CHANNEL_FMT_RIGHT_LEFT && c->communication_format==I2S_COMM_FORMAT_STAND_I2S && c->dma_buf_len==256 && c->dma_buf_count==2 && c->tx_desc_auto_clear && !c->use_apll);
 auto result=event("install");if(result==ESP_OK)installed=true;return result;
}
esp_err_t i2s_set_pin(i2s_port_t p,const i2s_pin_config_t* c){assert(p==1 && installed && c->mck_io_num==-1 && c->data_in_num==-1 && c->bck_io_num==7 && c->ws_io_num==8 && c->data_out_num==9);return event("set_pin");}
esp_err_t i2s_stop(i2s_port_t p){assert(p==1 && installed);return event("stop");}
esp_err_t i2s_driver_uninstall(i2s_port_t p){assert(p==1 && installed);auto result=event("uninstall");if(result==ESP_OK)installed=false;return result;}
esp_err_t i2s_write(i2s_port_t p,const void* b,size_t n,size_t* done,TickType_t ticks){
 assert(p==1 && installed && n<=1024 && ticks==0);event("write");
 *done=badCount?n+4:unaligned?1:std::min(n,limit);
 if(!badCount && !unaligned){auto bytes=static_cast<const uint8_t*>(b);accepted.insert(accepted.end(),bytes,bytes+*done);}
 return writeError?ESP_FAIL:ESP_OK;
}
static void reset(){assert(close(1));calls.clear();failure.clear();now=0;core=0;limit=1024;writeError=badCount=unaligned=false;accepted.clear();}
static bool called(const char* s){return std::find(calls.begin(),calls.end(),s)!=calls.end();}
int main(){
 assert(idle());
 for(uint8_t p:{22,23,24,25,49,64,255})assert(!open(1,p,8,9,8000));
 assert(!open(2,7,8,9,8000) && !open(1,7,7,9,8000) && !open(1,7,8,9,48000));assert(calls.empty());
 failure="install";assert(!open(1,7,8,9,8000));assert(close(1));assert(!called("uninstall"));failure.clear();reset();
 failure="set_pin";assert(!open(1,7,8,9,8000) && installed);failure="stop";calls.clear();assert(!close(1));assert(called("low7") && called("low8") && called("low9") && !called("uninstall"));failure.clear();reset();
 for(const char* fail:{"stop","uninstall","low7","low8","low9","gpio_config","rtc_deinit","hold_dis"}){
  assert(open(1,7,8,9,8000));calls.clear();failure=fail;assert(!close(1));assert(!idle());if(failure!="rtc_deinit")assert(called("low7") && called("low8") && called("low9"));
  int16_t silence[2]{};size_t copied=0;calls.clear();assert(!write(1,silence,1,&copied,40) && calls.empty());failure.clear();reset();
 }
 assert(open(1,7,8,9,8000));assert(!idle());int16_t pcm[512];for(unsigned i=0;i<512;++i)pcm[i]=i;size_t done=99;
 core=1;calls.clear();assert(!close(1) && !write(1,pcm,256,&done,40));assert(calls.empty() && !done);core=0;
 assert(write(1,pcm,256,&done,40) && done==256);assert(accepted.size()==1024 && !memcmp(accepted.data(),pcm,1024));
 accepted.clear();limit=128;assert(write(1,pcm,256,&done,40) && done==256 && accepted.size()==1024);assert(now==7000 && !memcmp(accepted.data(),pcm,1024));
 limit=0;now=0;assert(!write(1,pcm,256,&done,40) && !done && now==40000);
 limit=128;writeError=true;assert(!write(1,pcm,256,&done,40) && done==32);writeError=false;
 badCount=true;assert(!write(1,pcm,256,&done,40));badCount=false;unaligned=true;assert(!write(1,pcm,256,&done,40));unaligned=false;
 assert(!write(1,pcm,257,&done,40) && !write(1,pcm,256,&done,41) && !write(1,pcm,256,&done,0));
 // This shim has no SPI symbols: output cancellation cannot wait on display.
 assert(pendingDisplay && close(1) && pendingDisplay && idle());reset();
 assert(open(1,7,8,9,8000) && close(1));
 puts("Native I2S adapter: exact SDK configuration, bounded copies/partial counts, same-core cleanup, all-pad low faults, no display dependency PASS");
}
