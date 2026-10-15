#pragma once
// ESP-IDF6 native channel adapter. The runtime exclusively owns both directions
// of each admitted controller; no Arduino/other caller may share these units.
#include "NativeSleep.h"
#include <RiscI2sStandardConfigV1.h>
#include <driver/i2s_std.h>
#include <driver/i2s_pdm.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdint>

namespace RiscCpu { namespace NativeI2s {
struct State {
  i2s_chan_handle_t tx=nullptr,rx=nullptr;
  bool txEnabled=false,rxEnabled=false,opened=false,closing=false,pdm=false;
  int clock=-1,word=-1,data=-1,master=-1,input=-1,core=-1;
};
static State states[2];
inline bool low(int pin) {
  return pin<0 || NativeSleep::openPin(uint8_t(pin),true,false,false);
}
inline bool input(int pin) {
  return pin<0 || NativeSleep::openPin(uint8_t(pin),false,false,false);
}
inline bool close(uint8_t unit) {
  if(unit>1)return false;
  auto& s=states[unit];
  if(s.core>=0 && s.core!=xPortGetCoreID())return false;
  s.closing=true;s.opened=false;
  // Disable both DMA directions before disconnecting pads or deleting either
  // channel. Keep each successful transition so a retained retry is idempotent.
  bool stopped=true;
  if(s.txEnabled) {
    if(i2s_channel_disable(s.tx)==ESP_OK)s.txEnabled=false;else stopped=false;
  }
  if(s.rxEnabled) {
    if(i2s_channel_disable(s.rx)==ESP_OK)s.rxEnabled=false;else stopped=false;
  }
  // Disable does not stop MCLK. Explicitly stage every output LOW and detach
  // its matrix routing, including RTC/deep-held pads. RX is always input-only.
  bool quiet=low(s.data);
  if(!low(s.clock))quiet=false;
  if(!low(s.word))quiet=false;
  if(!low(s.master))quiet=false;
  if(!input(s.input))quiet=false;
  bool removed=true;
  if(stopped) {
    if(s.tx) {if(i2s_del_channel(s.tx)==ESP_OK)s.tx=nullptr;else removed=false;}
    if(s.rx) {if(i2s_del_channel(s.rx)==ESP_OK)s.rx=nullptr;else removed=false;}
  } else removed=false;
  // Pad failure retains the complete owner record even if DMA was deleted.
  if(!stopped || !quiet || !removed)return false;
  s={};return true;
}
inline bool allocate(uint8_t unit,uint8_t directions) {
  auto& s=states[unit];
  i2s_chan_config_t config=I2S_CHANNEL_DEFAULT_CONFIG(unit,I2S_ROLE_MASTER);
  config.dma_desc_num=2;config.dma_frame_num=256;
  config.auto_clear_after_cb=(directions&1)!=0;
  // IDF6 can return a valid TX handle even if subsequent RX allocation fails.
  // Store outputs directly in retained state, including on a non-OK return.
  return i2s_new_channel(&config,(directions&1)?&s.tx:nullptr,
                        (directions&2)?&s.rx:nullptr)==ESP_OK;
}
inline bool enable(State& s) {
  // Start the duplex receiver before the transmitter supplies its shared clock.
  if(s.rx) {if(i2s_channel_enable(s.rx)!=ESP_OK)return false;s.rxEnabled=true;}
  if(s.tx) {if(i2s_channel_enable(s.tx)!=ESP_OK)return false;s.txEnabled=true;}
  s.opened=true;return true;
}
inline bool initializeStandard(uint8_t unit,uint8_t directions,uint32_t rate) {
  auto& s=states[unit];
  if(!low(s.data)||!low(s.clock)||!low(s.word)||!low(s.master)||!input(s.input))return false;
  if(!allocate(unit,directions))return false;
  i2s_std_config_t config{};
  config.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(rate);
  config.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
  config.gpio_cfg.mclk=static_cast<gpio_num_t>(s.master);
  config.gpio_cfg.bclk=static_cast<gpio_num_t>(s.clock);
  config.gpio_cfg.ws=static_cast<gpio_num_t>(s.word);
  config.gpio_cfg.dout=static_cast<gpio_num_t>(s.data);
  config.gpio_cfg.din=static_cast<gpio_num_t>(s.input);
  // The paired channels receive identical slots/clocks/pins. IDF6 routes the
  // second channel to the first channel's clocks for full-duplex operation.
  if(s.tx && i2s_channel_init_std_mode(s.tx,&config)!=ESP_OK)return false;
  if(s.rx && i2s_channel_init_std_mode(s.rx,&config)!=ESP_OK)return false;
  return enable(s);
}
inline bool open(uint8_t unit,uint8_t clock,uint8_t word,uint8_t data,uint32_t rate) {
  if(unit>1 || states[unit].clock>=0 || states[unit].tx || states[unit].rx ||
     clock>48 || word>48 || data>48 || !GPIO_IS_VALID_OUTPUT_GPIO(clock) ||
     !GPIO_IS_VALID_OUTPUT_GPIO(word) || !GPIO_IS_VALID_OUTPUT_GPIO(data) ||
     clock==word || clock==data || word==data ||
     (rate!=8000 && rate!=16000 && rate!=22050 && rate!=44100))return false;
  auto& s=states[unit];s.clock=clock;s.word=word;s.data=data;s.core=xPortGetCoreID();
  return initializeStandard(unit,1,rate);
}
inline bool openRx(uint8_t unit,uint8_t clock,uint8_t data,uint32_t rate) {
  if(unit!=0 || states[unit].clock>=0 || states[unit].tx || states[unit].rx ||
     clock>48 || data>48 || !GPIO_IS_VALID_OUTPUT_GPIO(clock) || !GPIO_IS_VALID_GPIO(data) ||
     clock==data || (rate!=8000 && rate!=16000))return false;
  auto& s=states[unit];s.clock=clock;s.input=data;s.pdm=true;s.core=xPortGetCoreID();
  if(!input(s.input)||!low(s.clock)||!allocate(unit,2))return false;
  i2s_pdm_rx_config_t config{};
  config.clk_cfg=I2S_PDM_RX_CLK_DEFAULT_CONFIG(rate);
  // Fixed 128x decimation preserves 1.024/2.048 MHz clock at 8/16 kHz PCM.
  config.clk_cfg.dn_sample_mode=I2S_PDM_DSR_16S;
  config.slot_cfg=I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_MONO);
  config.gpio_cfg.clk=static_cast<gpio_num_t>(s.clock);
  config.gpio_cfg.din=static_cast<gpio_num_t>(s.input);
  if(i2s_channel_init_pdm_rx_mode(s.rx,&config)!=ESP_OK)return false;
  return enable(s);
}
inline bool openStandard(const risc_hw_i2s_standard_v1& wire,uint8_t directions,uint32_t rate) {
  const unsigned unit=wire.controller;
  if(unit>1 || states[unit].clock>=0 || states[unit].tx || states[unit].rx || !directions || directions>3 ||
     (rate!=8000&&rate!=16000&&rate!=22050&&rate!=32000&&rate!=44100&&rate!=48000))return false;
  const int pins[]={wire.mclk,wire.bclk,wire.ws,wire.data_out,wire.data_in};
  for(unsigned i=0;i<5;i++) {
    if(pins[i]<0){if(i<3||(i==3&&(directions&1))||(i==4&&(directions&2)))return false;continue;}
    if(pins[i]>48 || (i==4?!GPIO_IS_VALID_GPIO(pins[i]):!GPIO_IS_VALID_OUTPUT_GPIO(pins[i])))return false;
    for(unsigned j=0;j<i;j++)if(pins[j]==pins[i])return false;
  }
  auto& s=states[unit];s.clock=wire.bclk;s.word=wire.ws;s.master=wire.mclk;s.core=xPortGetCoreID();
  s.data=(directions&1)?wire.data_out:-1;s.input=(directions&2)?wire.data_in:-1;
  return initializeStandard(uint8_t(unit),directions,rate);
}
inline bool read(uint8_t unit,int16_t* pcm,size_t frames,size_t* done,uint32_t ms) {
  if(done)*done=0;
  if(!done || unit>1 || !states[unit].opened || !states[unit].rxEnabled || states[unit].closing ||
     states[unit].core!=xPortGetCoreID() || !pcm || !frames || frames>256 || !ms || ms>40)return false;
  auto* bytes=reinterpret_cast<uint8_t*>(pcm);
  const size_t frameBytes=states[unit].pdm?2:4,size=frames*frameBytes;
  size_t copied=0;const int64_t deadline=esp_timer_get_time()+int64_t(ms)*1000;
  // Zero-timeout calls bound the whole transfer instead of every DMA buffer.
  // IDF6 reports empty queues as TIMEOUT, with any valid partial count retained.
  for(unsigned attempt=0;attempt<42 && copied<size;++attempt) {
    size_t n=0;const esp_err_t result=i2s_channel_read(states[unit].rx,bytes+copied,size-copied,&n,0);
    if(n>size-copied || n%frameBytes)return false;
    copied+=n;*done=copied/frameBytes;
    if(result!=ESP_OK && result!=ESP_ERR_TIMEOUT)return false;
    if(copied==size || esp_timer_get_time()>=deadline)return true;
    vTaskDelay(1);
    if(esp_timer_get_time()>=deadline)return true;
  }
  return true;
}
inline bool write(uint8_t unit,const int16_t* pcm,size_t frames,size_t* done,uint32_t ms) {
  if(done)*done=0;
  if(!done || unit>1 || !states[unit].opened || !states[unit].txEnabled || states[unit].closing ||
     states[unit].core!=xPortGetCoreID() || !pcm || !frames || frames>256 || !ms || ms>40)return false;
  const auto* bytes=reinterpret_cast<const uint8_t*>(pcm);const size_t size=frames*4;
  size_t copied=0;const int64_t deadline=esp_timer_get_time()+int64_t(ms)*1000;
  // The runtime serializes all callers. IDF's channel semaphore is uncontended,
  // every SDK wait is zero, and one deadline allows at most one tick rounding.
  for(unsigned attempt=0;attempt<42 && copied<size;++attempt) {
    size_t n=0;const esp_err_t result=i2s_channel_write(states[unit].tx,bytes+copied,size-copied,&n,0);
    if(n>size-copied || n%4)return false;
    copied+=n;*done=copied/4;
    if(result!=ESP_OK && result!=ESP_ERR_TIMEOUT)return false;
    if(copied==size)return true;
    if(esp_timer_get_time()>=deadline)return false;
    vTaskDelay(1);
    if(esp_timer_get_time()>=deadline)return false;
  }
  return copied==size;
}
inline bool idle() {
  for(const auto& s:states)if(s.tx || s.rx || s.clock>=0)return false;
  return true;
}
} }
