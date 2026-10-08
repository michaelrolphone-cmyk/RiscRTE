#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace NativeShim {
using TickType_t=uint32_t;
using spi_host_device_t=int;
using gpio_num_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,SPI2_HOST=1,SPI3_HOST=2,SPI_DMA_CH_AUTO=3;
constexpr int SPI_DEVICE_3WIRE=1<<2,SPI_DEVICE_HALFDUPLEX=1<<4,SPICOMMON_BUSFLAG_GPIO_PINS=1<<2;
constexpr int GPIO_MODE_INPUT=1,GPIO_MODE_INPUT_OUTPUT=3,FSPID_OUT_IDX=103,SPI3_D_OUT_IDX=68,SIG_GPIO_OUT_IDX=256;
struct spi_bus_config_t {int sclk_io_num,mosi_io_num,miso_io_num,quadwp_io_num,quadhd_io_num,max_transfer_sz;uint32_t flags;};
struct spi_device_interface_config_t {int clock_speed_hz;uint8_t mode;int spics_io_num,queue_size;uint32_t flags;};
struct spi_transaction_t {size_t length,rxlength;const void* tx_buffer;void* rx_buffer;};
struct Device {bool live=false;spi_device_interface_config_t config{};};
using spi_device_handle_t=Device*;
enum class Fault {None,Initialize,Add,Remove,Pullup,Pulldown,Direction,Queue,Drain,CsLow,CsHigh,Free,ClosePin};
inline struct Model {
  Fault fault=Fault::None;int faultPin=-1;unsigned calls=0,opens=0,adds=0,removes=0,queues=0,drains=0,frees=0;
  int64_t us=0;uint32_t queueDelay=0;TickType_t queueWait=0,drainWait=0;
  bool owner=true,initialized=false,level[49]{},pullup[49]{},outputEnabled[49]{};
  int direction[49]{},signal[49]{},inputSignal[49]{};Device device{};spi_transaction_t* pending=nullptr;
  spi_bus_config_t bus{};std::vector<std::string> events;std::vector<uint8_t> rxData;
} model;
inline bool fails(Fault fault){++model.calls;return model.fault==fault;}
inline TickType_t ticks(uint32_t ms){return ms?ms+1:0;} // Native helper with a modeled 1 ms RTOS tick.
inline spi_host_device_t host(uint8_t physical){return physical==2?SPI2_HOST:SPI3_HOST;}
inline int64_t esp_timer_get_time(){return model.us;}
inline int spi_bus_initialize(spi_host_device_t physical,const spi_bus_config_t* config,int dma){
  assert(!model.initialized && dma==SPI_DMA_CH_AUTO);++model.opens;model.bus=*config;
  if(fails(Fault::Initialize))return ESP_FAIL;
  model.initialized=true;model.direction[config->mosi_io_num]=GPIO_MODE_INPUT_OUTPUT;
  model.outputEnabled[config->mosi_io_num]=true;
  model.inputSignal[config->mosi_io_num]=physical==SPI2_HOST?FSPID_OUT_IDX:SPI3_D_OUT_IDX;
  return ESP_OK;
}
inline int spi_bus_add_device(spi_host_device_t,const spi_device_interface_config_t* config,spi_device_handle_t* out){
  assert(model.initialized && !model.device.live && config->spics_io_num==-1 && config->queue_size==1);++model.adds;
  if(fails(Fault::Add))return ESP_FAIL;
  model.device.live=true;model.device.config=*config;*out=&model.device;return ESP_OK;
}
inline int spi_bus_remove_device(spi_device_handle_t device){
  assert(device==&model.device && device->live && !model.pending);++model.removes;
  if(fails(Fault::Remove))return ESP_FAIL;
  device->live=false;return ESP_OK;
}
inline int gpio_set_direction(gpio_num_t pin,int mode){
  if(fails(Fault::Direction))return ESP_FAIL;
  model.direction[pin]=mode;model.outputEnabled[pin]=(mode==GPIO_MODE_INPUT_OUTPUT);
  // Pinned S3 gpio_ll_output_disable disconnects the output matrix too.
  // gpio_output_enable also connects SIG_GPIO_OUT_IDX before native TX routing.
  model.signal[pin]=SIG_GPIO_OUT_IDX;return ESP_OK;
}
inline void esp_rom_gpio_connect_out_signal(uint32_t pin,uint32_t signal,bool inverted,bool oeInverted){
  ++model.calls;assert(!inverted && !oeInverted);model.signal[pin]=signal;
  // ROM routing also enables the pad output. Omitting this effect hid the
  // original receive-direction bug, which drove the GPIO latch during RX.
  model.outputEnabled[pin]=true;model.direction[pin]|=2;
}
inline int gpio_pulldown_dis(gpio_num_t){return fails(Fault::Pulldown)?ESP_FAIL:ESP_OK;}
inline int gpio_pullup_en(gpio_num_t pin){if(fails(Fault::Pullup))return ESP_FAIL;model.pullup[pin]=true;return ESP_OK;}
inline int gpio_pullup_dis(gpio_num_t pin){if(fails(Fault::Pullup))return ESP_FAIL;model.pullup[pin]=false;return ESP_OK;}
inline bool gpioWrite(uint8_t pin,bool value){
  if(fails(value?Fault::CsHigh:Fault::CsLow))return false;
  model.level[pin]=value;model.events.push_back(value?"high":"low");return true;
}
inline bool gpioClose(uint8_t pin){const bool failure=fails(Fault::ClosePin);return !(failure && (model.faultPin<0 || model.faultPin==pin));}
inline int spi_device_queue_trans(spi_device_handle_t device,spi_transaction_t* transaction,TickType_t wait){
  assert(device==&model.device && device->live && !model.pending);++model.queues;model.queueWait=wait;
  if(fails(Fault::Queue))return ESP_FAIL;
  if(device->config.flags&SPI_DEVICE_3WIRE){
    assert(device->config.flags&SPI_DEVICE_HALFDUPLEX);
    assert(bool(transaction->tx_buffer)!=bool(transaction->rx_buffer));
    if(transaction->tx_buffer){
      assert(transaction->length>0 && transaction->rxlength==0);
      assert(model.direction[model.bus.mosi_io_num]==GPIO_MODE_INPUT_OUTPUT && model.outputEnabled[model.bus.mosi_io_num]);
      assert(model.signal[model.bus.mosi_io_num]==FSPID_OUT_IDX || model.signal[model.bus.mosi_io_num]==SPI3_D_OUT_IDX);
    } else {
      assert(transaction->length==0 && transaction->rxlength>0);
      assert(model.direction[model.bus.mosi_io_num]==GPIO_MODE_INPUT && !model.outputEnabled[model.bus.mosi_io_num] && model.pullup[model.bus.mosi_io_num]);
      assert(model.inputSignal[model.bus.mosi_io_num]==FSPID_OUT_IDX || model.inputSignal[model.bus.mosi_io_num]==SPI3_D_OUT_IDX);
      assert(model.signal[model.bus.mosi_io_num]==SIG_GPIO_OUT_IDX);
    }
  } else assert(transaction->tx_buffer && transaction->rx_buffer && transaction->length>0);
  model.pending=transaction;model.us+=int64_t(model.queueDelay)*1000;model.events.push_back("queue");return ESP_OK;
}
inline int spi_device_get_trans_result(spi_device_handle_t device,spi_transaction_t** result,TickType_t wait){
  assert(device==&model.device && device->live && model.pending);++model.drains;model.drainWait=wait;
  if(fails(Fault::Drain))return ESP_FAIL;
  auto* t=model.pending;
  if(t->rx_buffer){const size_t n=(t->rxlength?t->rxlength:t->length)/8;
    if(model.rxData.empty())std::memset(t->rx_buffer,0xa5,n);
    else {assert(n<=model.rxData.size());std::memcpy(t->rx_buffer,model.rxData.data(),n);}
  }
  *result=t;model.pending=nullptr;model.events.push_back("drain");return ESP_OK;
}
inline int spi_bus_free(spi_host_device_t){
  assert(model.initialized && !model.device.live && !model.pending);++model.frees;
  if(fails(Fault::Free))return ESP_FAIL;
  model.initialized=false;return ESP_OK;
}
#include "ports/esp32s3/NativeSpi.inc"
}
