// Real selected ESP-IDF6 I2S headers, with fault-injected host SDK behavior.
#include "ports/esp32s3/NativeI2s.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
static_assert(ESP_IDF_VERSION_MAJOR>=6,"This test must compile the native backend");
using namespace RiscCpu::NativeI2s;

struct i2s_channel_obj_t {bool live=false,ready=false,enabled=false,tx=false;unsigned unit=0;};
static i2s_channel_obj_t channels[2][2];
uint64_t native_sleep_test_output_mask=native_sleep_test_gpio_mask;
static int core=0;
static int64_t now=0,tickUs=1000;
static unsigned calls=0,failCall=0;
static std::vector<std::string> events;
static uint8_t expectedUnit=0,expectedDirections=3;
static uint32_t expectedRate=16000;
static bool expectedPdm=false,expectedLegacy=false;
static size_t chunk=1024,available=1024,offset=0;
static bool badCount=false,unaligned=false;
static esp_err_t ioResult=ESP_OK;
static uint8_t source[1024];
static std::vector<uint8_t> written;
static esp_err_t event(const std::string& name) {
  events.push_back(name);return ++calls==failCall?ESP_FAIL:ESP_OK;
}
static bool called(const std::string& name) {
  return std::find(events.begin(),events.end(),name)!=events.end();
}
int64_t esp_timer_get_time(){return now;}
void vTaskDelay(TickType_t ticks){assert(ticks==1);now+=tickUs;}
int xPortGetCoreID(){return core;}
extern "C" bool rtc_gpio_is_valid_gpio(gpio_num_t pin){return pin<=21;}
extern "C" esp_err_t rtc_gpio_deinit(gpio_num_t pin){return event("rtc"+std::to_string(pin));}
extern "C" esp_err_t gpio_hold_dis(gpio_num_t pin){return event("hold"+std::to_string(pin));}
extern "C" esp_err_t gpio_set_level(gpio_num_t pin,uint32_t value) {
  // Neither standard RX nor PDM microphone data is ever driven.
  assert(pin!=42 && pin!=11 && value==0);return event("low"+std::to_string(pin));
}
extern "C" esp_err_t gpio_config(const gpio_config_t* config) {
  assert(config->pull_up_en==GPIO_PULLUP_DISABLE && config->pull_down_en==GPIO_PULLDOWN_DISABLE && config->intr_type==GPIO_INTR_DISABLE);
  bool inputPin=config->pin_bit_mask==uint64_t(1)<<42 || config->pin_bit_mask==uint64_t(1)<<11;
  assert(config->mode==(inputPin?GPIO_MODE_INPUT:GPIO_MODE_INPUT_OUTPUT));
  return event(inputPin?"input":"output");
}
extern "C" esp_err_t i2s_new_channel(const i2s_chan_config_t* config,i2s_chan_handle_t* tx,i2s_chan_handle_t* rx) {
  assert(config->id==expectedUnit && config->role==I2S_ROLE_MASTER);
  assert(config->dma_desc_num==2 && config->dma_frame_num==256 && !config->allow_pd && !config->intr_priority);
  assert(config->auto_clear_after_cb==bool(expectedDirections&1) && !config->auto_clear_before_cb);
  assert(config->tx_destination==I2S_DESTINATION_DMA && config->rx_destination==I2S_DESTINATION_DMA);
  assert(bool(tx)==bool(expectedDirections&1) && bool(rx)==bool(expectedDirections&2));
  if(event("new")!=ESP_OK)return ESP_FAIL;
  if(tx) {
    if(event("allocate_tx")!=ESP_OK)return ESP_FAIL;
    auto& ch=channels[expectedUnit][0];assert(!ch.live);ch={true,false,false,true,expectedUnit};*tx=&ch;
  }
  if(rx) {
    // A failed second allocation deliberately returns the existing TX handle.
    if(event("allocate_rx")!=ESP_OK)return ESP_FAIL;
    auto& ch=channels[expectedUnit][1];assert(!ch.live);ch={true,false,false,false,expectedUnit};*rx=&ch;
  }
  return ESP_OK;
}
extern "C" esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t h,const i2s_std_config_t* config) {
  assert(h && h->live && !h->ready && !expectedPdm);
  assert(config->clk_cfg.sample_rate_hz==expectedRate && config->clk_cfg.clk_src==I2S_CLK_SRC_DEFAULT);
  assert(config->clk_cfg.mclk_multiple==I2S_MCLK_MULTIPLE_256 && !config->clk_cfg.ext_clk_freq_hz);
  assert(config->slot_cfg.data_bit_width==I2S_DATA_BIT_WIDTH_16BIT && config->slot_cfg.slot_bit_width==I2S_SLOT_BIT_WIDTH_AUTO);
  assert(config->slot_cfg.slot_mode==I2S_SLOT_MODE_STEREO && config->slot_cfg.slot_mask==I2S_STD_SLOT_BOTH);
  assert(config->slot_cfg.ws_width==16 && !config->slot_cfg.ws_pol && config->slot_cfg.bit_shift);
  assert(config->slot_cfg.left_align && !config->slot_cfg.big_endian && !config->slot_cfg.bit_order_lsb);
  const auto& pin=config->gpio_cfg;
  if(expectedLegacy)assert(pin.mclk==-1 && pin.bclk==7 && pin.ws==8 && pin.dout==9 && pin.din==-1);
  else assert(pin.mclk==16 && pin.bclk==41 && pin.ws==45 && pin.dout==((expectedDirections&1)?40:-1) && pin.din==((expectedDirections&2)?42:-1));
  assert(!pin.invert_flags.mclk_inv && !pin.invert_flags.bclk_inv && !pin.invert_flags.ws_inv);
  auto result=event(h->tx?"init_tx":"init_rx");if(result==ESP_OK)h->ready=true;return result;
}
extern "C" esp_err_t i2s_channel_init_pdm_rx_mode(i2s_chan_handle_t h,const i2s_pdm_rx_config_t* config) {
  assert(h && h->live && !h->ready && !h->tx && h->unit==0 && expectedPdm);
  assert(config->clk_cfg.sample_rate_hz==expectedRate && config->clk_cfg.clk_src==I2S_CLK_SRC_DEFAULT);
  assert(config->clk_cfg.dn_sample_mode==I2S_PDM_DSR_16S && config->clk_cfg.bclk_div==8);
  assert(config->slot_cfg.data_bit_width==I2S_DATA_BIT_WIDTH_16BIT && config->slot_cfg.slot_mode==I2S_SLOT_MODE_MONO);
  assert(config->slot_cfg.slot_mask==I2S_PDM_SLOT_LEFT && config->slot_cfg.data_fmt==I2S_PDM_DATA_FMT_PCM);
  assert(config->gpio_cfg.clk==10 && config->gpio_cfg.din==11 && !config->gpio_cfg.invert_flags.clk_inv);
  auto result=event("init_pdm");if(result==ESP_OK)h->ready=true;return result;
}
extern "C" esp_err_t i2s_channel_enable(i2s_chan_handle_t h) {
  assert(h && h->live && h->ready && !h->enabled);
  auto result=event(h->tx?"enable_tx":"enable_rx");if(result==ESP_OK)h->enabled=true;return result;
}
extern "C" esp_err_t i2s_channel_disable(i2s_chan_handle_t h) {
  assert(h && h->live && h->enabled);
  auto result=event(h->tx?"disable_tx":"disable_rx");if(result==ESP_OK)h->enabled=false;return result;
}
extern "C" esp_err_t i2s_del_channel(i2s_chan_handle_t h) {
  assert(h && h->live && !h->enabled);
  assert(!channels[h->unit][0].enabled && !channels[h->unit][1].enabled);
  auto result=event(h->tx?"delete_tx":"delete_rx");if(result==ESP_OK)h->live=false;return result;
}
extern "C" esp_err_t i2s_channel_write(i2s_chan_handle_t h,const void* data,size_t bytes,size_t* done,uint32_t ms) {
  assert(h && h->live && h->enabled && h->tx && bytes<=1024 && !ms);event("write");
  *done=badCount?bytes+4:unaligned?1:std::min(bytes,chunk);
  if(!badCount && !unaligned) {
    const auto* b=static_cast<const uint8_t*>(data);written.insert(written.end(),b,b+*done);
  }
  return ioResult!=ESP_OK?ioResult:(*done<bytes?ESP_ERR_TIMEOUT:ESP_OK);
}
extern "C" esp_err_t i2s_channel_read(i2s_chan_handle_t h,void* data,size_t bytes,size_t* done,uint32_t ms) {
  assert(h && h->live && h->enabled && !h->tx && bytes<=(expectedPdm?512u:1024u) && !ms);event("read");
  *done=badCount?bytes+4:unaligned?1:std::min(std::min(bytes,chunk),available-offset);
  if(!badCount && !unaligned){memcpy(data,source+offset,*done);offset+=*done;}
  return ioResult!=ESP_OK?ioResult:(*done<bytes?ESP_ERR_TIMEOUT:ESP_OK);
}
static risc_hw_i2s_standard_v1 wire() {
  risc_hw_i2s_standard_v1 w{};w.struct_size=sizeof(w);w.controller=expectedUnit;
  w.mclk=16;w.bclk=41;w.ws=45;w.data_out=40;w.data_in=42;return w;
}
static bool openExpected() {
  return expectedPdm?openRx(expectedUnit,10,11,expectedRate):
    expectedLegacy?open(expectedUnit,7,8,9,expectedRate):openStandard(wire(),expectedDirections,expectedRate);
}
static void reset() {
  core=0;failCall=0;assert(close(0) && close(1) && idle());
  for(const auto& unit:channels)for(const auto& h:unit)assert(!h.live);
  calls=0;events.clear();now=0;tickUs=1000;chunk=available=1024;offset=0;
  badCount=unaligned=false;ioResult=ESP_OK;written.clear();
}
static void noIo() {
  int16_t samples[2]{};size_t done=55;auto before=calls;
  assert(!read(expectedUnit,samples,1,&done,40) && !done);
  assert(!write(expectedUnit,samples,1,&done,40) && !done && calls==before);
}
static void faults() {
  assert(openExpected());unsigned openCalls=calls;reset();
  for(unsigned fault=1;fault<=openCalls;fault++) {
    failCall=fault;assert(!openExpected() && !idle());noIo();
    assert(!openExpected());core=1;auto before=calls;assert(!close(expectedUnit) && before==calls);core=0;
    reset();
  }
  assert(openExpected());calls=0;assert(close(expectedUnit));unsigned closeCalls=calls;reset();
  for(unsigned fault=1;fault<=closeCalls;fault++) {
    assert(openExpected());calls=0;events.clear();failCall=fault;
    assert(!close(expectedUnit) && !idle());noIo();
    // Even after deletion, a failed pad transition retains same-core custody.
    core=1;auto before=calls;assert(!close(expectedUnit) && before==calls);core=0;
    reset();
  }
}
static void transfers() {
  assert(openExpected());int16_t pcm[512]{};size_t done=55;
  if(expectedDirections&1) {
    chunk=128;assert(write(expectedUnit,reinterpret_cast<int16_t*>(source),256,&done,40));
    assert(done==256 && now==7000 && written.size()==1024 && !memcmp(written.data(),source,1024));
    now=0;chunk=0;assert(!write(expectedUnit,pcm,256,&done,40) && !done && now==40000);
    chunk=128;ioResult=ESP_FAIL;assert(!write(expectedUnit,pcm,256,&done,40) && done==32);ioResult=ESP_OK;
  } else assert(!write(expectedUnit,pcm,1,&done,40) && !done);
  if(expectedDirections&2) {
    size_t bytes=expectedPdm?512:1024;chunk=bytes/8;offset=0;now=0;
    assert(read(expectedUnit,pcm,256,&done,40) && done==256 && now==7000 && !memcmp(pcm,source,bytes));
    now=0;chunk=0;assert(read(expectedUnit,pcm,256,&done,40) && !done && now==40000);
    now=0;chunk=64;offset=0;available=64;
    assert(read(expectedUnit,pcm,256,&done,40) && done==(expectedPdm?32u:16u) && now==40000);
    available=1024;offset=0;ioResult=ESP_FAIL;
    assert(!read(expectedUnit,pcm,256,&done,40) && done==(expectedPdm?32u:16u));ioResult=ESP_OK;
  } else assert(!read(expectedUnit,pcm,1,&done,40) && !done);
  for(unsigned fault=0;fault<2;fault++) {
    badCount=fault==0;unaligned=fault==1;
    if(expectedDirections&1)assert(!write(expectedUnit,pcm,256,&done,40) && !done);
    if(expectedDirections&2)assert(!read(expectedUnit,pcm,256,&done,40) && !done);
  }
  badCount=unaligned=false;
  for(unsigned ms:{0,41,100}) {
    assert(!read(expectedUnit,pcm,1,&done,ms) && !done);
    assert(!write(expectedUnit,pcm,1,&done,ms) && !done);
  }
  for(unsigned frames:{0,257}) {
    assert(!read(expectedUnit,pcm,frames,&done,40) && !done);
    assert(!write(expectedUnit,pcm,frames,&done,40) && !done);
  }
  assert(!read(expectedUnit,nullptr,1,&done,40) && !write(expectedUnit,nullptr,1,&done,40));
  assert(!read(expectedUnit,pcm,1,nullptr,40) && !write(expectedUnit,pcm,1,nullptr,40));
  core=1;noIo();auto before=calls;assert(!close(expectedUnit) && before==calls);core=0;
  chunk=0;tickUs=10000;now=0;
  if(expectedDirections&2)assert(read(expectedUnit,pcm,1,&done,35) && !done && now==40000);
  now=0;
  if(expectedDirections&1)assert(!write(expectedUnit,pcm,1,&done,35) && !done && now==40000);
  // The secondary attempt bound also terminates if a faulty timer stalls.
  now=0;tickUs=0;before=calls;
  if(expectedDirections&2)assert(read(expectedUnit,pcm,1,&done,40) && calls-before==42);
  before=calls;
  if(expectedDirections&1)assert(!write(expectedUnit,pcm,1,&done,40) && calls-before==42);
  reset();
}
int main() {
  for(unsigned i=0;i<sizeof(source);i++)source[i]=uint8_t(i);
  assert(idle());
  for(uint8_t unit:{0,1})for(uint8_t directions:{1,2,3})for(unsigned rate:{8000,16000,22050,32000,44100,48000}) {
    expectedUnit=unit;expectedDirections=directions;expectedRate=rate;
    transfers();faults();
  }
  expectedLegacy=true;expectedDirections=1;
  for(uint8_t unit:{0,1})for(unsigned rate:{8000,16000,22050,44100}) {
    expectedUnit=unit;expectedRate=rate;transfers();faults();
  }
  expectedLegacy=false;expectedPdm=true;expectedUnit=0;expectedDirections=2;
  for(unsigned rate:{8000,16000}){expectedRate=rate;transfers();faults();}
  // Mono data needs input validity only; no output drive authority is required.
  native_sleep_test_output_mask&=~(uint64_t(1)<<11);assert(openExpected());reset();native_sleep_test_output_mask=native_sleep_test_gpio_mask;
  // Independent PDM on unit0 and standard TX on unit1 retain distinct handles.
  assert(openExpected());expectedPdm=false;expectedLegacy=true;expectedUnit=1;expectedDirections=1;expectedRate=8000;
  assert(openExpected() && close(0) && !idle() && channels[1][0].live);reset();expectedLegacy=false;
  // Every invalid request fails before touching any pad, channel or timer.
  for(uint8_t pin:{22,23,24,25,49,64,255}) {
    assert(!open(1,pin,8,9,8000) && !openRx(0,pin,11,8000) && !openRx(0,10,pin,8000));
    auto w=wire();w.mclk=pin;assert(!openStandard(w,3,16000));
  }
  auto w=wire();w.mclk=-1;assert(!openStandard(w,3,16000));w=wire();w.data_in=w.ws;assert(!openStandard(w,3,16000));
  w=wire();w.controller=2;assert(!openStandard(w,3,16000));
  assert(!openStandard(wire(),0,16000) && !openStandard(wire(),4,16000) && !openStandard(wire(),3,12345));
  assert(!open(2,7,8,9,8000) && !open(1,7,7,9,8000) && !open(1,7,8,9,48000));
  assert(!openRx(1,10,11,8000) && !openRx(0,10,10,8000) && !openRx(0,10,11,44100));
  assert(!calls && idle());
  // Explicit partial-allocation and partially-enabled duplex retention evidence.
  expectedUnit=1;expectedDirections=3;expectedRate=16000;
  assert(openExpected());auto alloc=std::find(events.begin(),events.end(),"allocate_rx")-events.begin()+1;reset();
  failCall=unsigned(alloc);assert(!openExpected() && channels[1][0].live && !channels[1][1].live && !idle());reset();
  assert(openExpected());calls=0;events.clear();failCall=1;
  assert(!close(1) && called("disable_tx") && called("disable_rx") && !called("delete_tx") && !called("delete_rx"));reset();
  puts("Native IDF6 I2S: real SDK headers; stereo duplex/six rates, legacy TX, mono 128x PDM, zero-wait timeout/partial copies, all startup/cleanup faults, partial allocation, same-core retention and pad safety PASS");
}
