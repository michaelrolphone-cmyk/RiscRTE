#pragma once
// Generic standard signed16 stereo TX and mono PDM RX. No board/audio policy.
// Exclusive runtime owner: no Arduino/other IDF caller may share these units.
#include "NativeSleep.h"
#include <driver/i2s.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdint>
namespace RiscCpu { namespace NativeI2s {
struct State { bool installed=false,closing=false,rx=false; int clock=-1,word=-1,data=-1,core=-1; };
static State states[2];
inline bool low(int pin){
  if(pin<0)return true;
  // Reuse staged digital configuration then RTC deinit/hold release, including
  // pads retained across deep wake. A held-high pad is not successful silence.
  return NativeSleep::openPin(uint8_t(pin),true,false,false);
}
inline bool close(uint8_t unit){
  if(unit>1)return false;
  auto& s=states[unit];if(s.installed && s.core!=xPortGetCoreID())return false;const auto port=static_cast<i2s_port_t>(unit);
  s.closing=true;
  // Stop only our I2S GDMA channel, without waiting for SPI/display completion.
  // Stop precedes pad disconnection and DMA storage destruction.
  const bool stopped=!s.installed || i2s_stop(port)==ESP_OK;
  // RX data belongs to the external transmitter: never drive it LOW. Restore
  // digital input with no pulls, including RTC/deep-hold release, on all paths.
  bool quiet=s.rx?(s.data<0 || NativeSleep::openPin(uint8_t(s.data),false,false,false)):low(s.data);
  if(!low(s.clock))quiet=false;
  if(!low(s.word))quiet=false;
  bool removed=!s.installed;
  if(s.installed && stopped){
    removed=i2s_driver_uninstall(port)==ESP_OK;
    if(removed)s.installed=false;
  }
  if(!stopped || !quiet || !removed)return false;
  s={};return true;
}
inline bool open(uint8_t unit,uint8_t clock,uint8_t word,uint8_t data,uint32_t rate){
  if(unit>1 || states[unit].installed || states[unit].clock>=0 ||
     clock>48 || word>48 || data>48 || !GPIO_IS_VALID_OUTPUT_GPIO(clock) || !GPIO_IS_VALID_OUTPUT_GPIO(word) || !GPIO_IS_VALID_OUTPUT_GPIO(data) ||
     clock==word || clock==data || word==data || (rate!=8000 && rate!=16000 && rate!=22050 && rate!=44100))return false;
  auto& s=states[unit];s.clock=clock;s.word=word;s.data=data;
  if(!low(data) || !low(clock) || !low(word))return false;
  i2s_config_t config{};config.mode=static_cast<i2s_mode_t>(I2S_MODE_MASTER|I2S_MODE_TX);
  config.sample_rate=rate;config.bits_per_sample=I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format=I2S_CHANNEL_FMT_RIGHT_LEFT;config.communication_format=I2S_COMM_FORMAT_STAND_I2S;
  config.dma_buf_count=2;config.dma_buf_len=256;config.use_apll=false;config.tx_desc_auto_clear=true;
  const auto port=static_cast<i2s_port_t>(unit);
  // IDF unwinds returned install errors. Its internal allocation-assert paths
  // remain SDK limits; host fault tests do not claim recovery from those paths.
  if(i2s_driver_install(port,&config,0,nullptr)!=ESP_OK)return false;
  s.installed=true;s.core=xPortGetCoreID();
  i2s_pin_config_t pins{};pins.mck_io_num=I2S_PIN_NO_CHANGE;pins.bck_io_num=clock;
  pins.ws_io_num=word;pins.data_out_num=data;pins.data_in_num=I2S_PIN_NO_CHANGE;
  return i2s_set_pin(port,&pins)==ESP_OK;
}
inline bool openRx(uint8_t unit,uint8_t clock,uint8_t data,uint32_t rate){
  if(unit!=0 || states[unit].installed || states[unit].clock>=0 ||
     clock>48 || data>48 || !GPIO_IS_VALID_OUTPUT_GPIO(clock) || !GPIO_IS_VALID_GPIO(data) ||
     clock==data || (rate!=8000 && rate!=16000))return false;
  auto& s=states[unit];s.clock=clock;s.data=data;s.rx=true;
  if(!NativeSleep::openPin(data,false,false,false) || !low(clock))return false;
  i2s_config_t config{};config.mode=static_cast<i2s_mode_t>(I2S_MODE_MASTER|I2S_MODE_RX|I2S_MODE_PDM);
  config.sample_rate=rate;config.bits_per_sample=I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format=I2S_CHANNEL_FMT_ONLY_LEFT;config.communication_format=I2S_COMM_FORMAT_STAND_I2S;
  config.dma_buf_count=2;config.dma_buf_len=256;config.use_apll=false;
  const auto port=static_cast<i2s_port_t>(unit);
  if(i2s_driver_install(port,&config,0,nullptr)!=ESP_OK)return false;
  s.installed=true;s.core=xPortGetCoreID();
  // Fixed 128x decimation gives a 1.024/2.048 MHz PDM clock at 8/16 kHz.
  // Configure before connecting output pads. IDF4 PDM clock is its WS signal;
  // the immutable transport record still calls this single clock `bclk`.
  if(i2s_set_pdm_rx_down_sample(port,I2S_PDM_DSR_16S)!=ESP_OK)return false;
  i2s_pin_config_t pins{};pins.mck_io_num=I2S_PIN_NO_CHANGE;pins.bck_io_num=I2S_PIN_NO_CHANGE;
  pins.ws_io_num=clock;pins.data_out_num=I2S_PIN_NO_CHANGE;pins.data_in_num=data;
  return i2s_set_pin(port,&pins)==ESP_OK;
}
inline bool read(uint8_t unit,int16_t* pcm,size_t frames,size_t* done,uint32_t ms){
  if(done)*done=0;
  if(!done || unit!=0 || !states[unit].installed || !states[unit].rx || states[unit].closing ||
     states[unit].core!=xPortGetCoreID() || !pcm || !frames || frames>256 || !ms || ms>40)return false;
  auto* bytes=reinterpret_cast<uint8_t*>(pcm);const size_t size=frames*2;
  size_t copied=0;const int64_t deadline=esp_timer_get_time()+int64_t(ms)*1000;
  // IDF's RX mutex is uncontended by the exclusive runtime-owner contract.
  // Queue timeouts are zero; one deadline covers all partial copies.
  for(unsigned attempt=0;attempt<42 && copied<size;++attempt){
    size_t n=0;const esp_err_t result=i2s_read(static_cast<i2s_port_t>(unit),bytes+copied,size-copied,&n,0);
    if(n>size-copied || n%2)return false;
    copied+=n;*done=copied/2;
    if(result!=ESP_OK)return false;
    if(copied==size)return true;
    if(esp_timer_get_time()>=deadline)return true;
    vTaskDelay(1);
    if(esp_timer_get_time()>=deadline)return true;
  }
  // A bounded RX wait succeeds with its partial count, including zero.
  return true;
}
inline bool write(uint8_t unit,const int16_t* pcm,size_t frames,size_t* done,uint32_t ms){
  if(done)*done=0;
  if(!done || unit>1 || !states[unit].installed || states[unit].rx || states[unit].closing || states[unit].core!=xPortGetCoreID() || !pcm || !frames || frames>256 || !ms || ms>40)return false;
  const auto* bytes=reinterpret_cast<const uint8_t*>(pcm);const size_t size=frames*4;
  size_t copied=0;const int64_t deadline=esp_timer_get_time()+int64_t(ms)*1000;
  // IDF's timeout applies per DMA buffer; instead all queue waits are zero.
  // The uncontended TX mutex is owned exclusively by this serialized runtime.
  // Bound the whole copy by one deadline, plus at most one RTOS tick rounding.
  for(unsigned attempt=0;attempt<42 && copied<size;++attempt){
    size_t n=0;const esp_err_t result=i2s_write(static_cast<i2s_port_t>(unit),bytes+copied,size-copied,&n,0);
    if(n>size-copied || n%4)return false;
    copied+=n;*done=copied/4;
    if(result!=ESP_OK)return false;
    if(copied==size)return true;
    if(esp_timer_get_time()>=deadline)return false;
    vTaskDelay(1);
    if(esp_timer_get_time()>=deadline)return false;
  }
  return copied==size;
}
inline bool idle(){for(const auto& s:states)if(s.installed || s.clock>=0)return false;return true;}
} }
