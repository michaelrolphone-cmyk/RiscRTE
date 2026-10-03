#ifdef ESP_PLATFORM
#include "CpuPort.h"
#include "CooperativeDelay.h"
#include <driver/gpio.h>
#include <driver/i2c.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_timer.h>
#include <esp_sleep.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
namespace RiscCpu { namespace {
bool (*ownerTask)()=nullptr;
int pwmPins[4]={-1,-1,-1,-1};
struct I2cState { bool installed=false,configured=false;int sda=-1,scl=-1; } i2c[2];
struct SpiState {
  bool initialized=false,held=false,pending=false;uint32_t hz=0;uint8_t mode=0;
  int sclk=-1,mosi=-1,miso=-1;spi_device_handle_t device=nullptr;
  spi_transaction_t transaction{};
  alignas(4) uint8_t tx[512]{},rx[512]{};
} spi[2];
TickType_t ticks(uint32_t ms){return ms?pdMS_TO_TICKS(ms)+1:0;}
spi_host_device_t host(uint8_t physical){return physical==2?SPI2_HOST:SPI3_HOST;}
bool stopPwm(uint8_t pin){
  for(int i=0;i<4;++i)if(pwmPins[i]==pin){
    if(ledc_stop(LEDC_LOW_SPEED_MODE,static_cast<ledc_channel_t>(i),0)!=ESP_OK)return false;
    esp_rom_gpio_connect_out_signal(pin,SIG_GPIO_OUT_IDX,false,false);pwmPins[i]=-1;
  }
  return true;
}
bool gpioOpen(uint8_t pin,bool output,bool initial,bool pullup){
  if(!GPIO_IS_VALID_GPIO(pin) || (output && !GPIO_IS_VALID_OUTPUT_GPIO(pin)) || !stopPwm(pin))return false;
  // Load output latch before changing direction, including the initial CS HIGH.
  if(output && gpio_set_level(static_cast<gpio_num_t>(pin),initial)!=ESP_OK)return false;
  gpio_config_t config{};config.pin_bit_mask=uint64_t(1)<<pin;
  config.mode=output?GPIO_MODE_OUTPUT:GPIO_MODE_INPUT;
  config.pull_up_en=pullup?GPIO_PULLUP_ENABLE:GPIO_PULLUP_DISABLE;
  config.pull_down_en=GPIO_PULLDOWN_DISABLE;config.intr_type=GPIO_INTR_DISABLE;
  return gpio_config(&config)==ESP_OK;
}
bool gpioWrite(uint8_t pin,bool level){
  return stopPwm(pin) && gpio_set_level(static_cast<gpio_num_t>(pin),level)==ESP_OK;
}
bool gpioRead(uint8_t pin,bool* out){*out=gpio_get_level(static_cast<gpio_num_t>(pin))!=0;return true;}
bool gpioClose(uint8_t pin){return stopPwm(pin) && gpio_reset_pin(static_cast<gpio_num_t>(pin))==ESP_OK;}
bool gpioPwm(uint8_t pin,uint32_t hz,uint16_t duty,uint16_t maximum){
  int slot=-1;
  for(int i=0;i<4;++i)if(pwmPins[i]==pin)slot=i;
  if(slot<0)for(int i=0;i<4;++i)if(pwmPins[i]<0){slot=i;break;}
  if(slot<0)return false;
  ledc_timer_config_t timer{};timer.speed_mode=LEDC_LOW_SPEED_MODE;timer.duty_resolution=LEDC_TIMER_10_BIT;
  timer.timer_num=static_cast<ledc_timer_t>(slot);timer.freq_hz=hz;timer.clk_cfg=LEDC_AUTO_CLK;
  if(ledc_timer_config(&timer)!=ESP_OK)return false;
  ledc_channel_config_t channel{};channel.gpio_num=pin;channel.speed_mode=LEDC_LOW_SPEED_MODE;
  channel.channel=static_cast<ledc_channel_t>(slot);channel.intr_type=LEDC_INTR_DISABLE;
  channel.timer_sel=timer.timer_num;channel.duty=uint32_t(duty)*1023/maximum;
  pwmPins[slot]=pin; // Retain partial configuration until gpioClose can stop it.
  return ledc_channel_config(&channel)==ESP_OK;
}
bool i2cOpen(uint8_t physical,uint8_t sda,uint8_t scl,uint32_t hz){
  auto& state=i2c[physical];if(state.installed)return false;
  state.sda=sda;state.scl=scl;
  i2c_config_t config{};config.mode=I2C_MODE_MASTER;config.sda_io_num=sda;config.scl_io_num=scl;
  config.sda_pullup_en=GPIO_PULLUP_ENABLE;config.scl_pullup_en=GPIO_PULLUP_ENABLE;config.master.clk_speed=hz;
  if(i2c_param_config(static_cast<i2c_port_t>(physical),&config)!=ESP_OK)return false;
  state.configured=true;
  if(i2c_driver_install(static_cast<i2c_port_t>(physical),I2C_MODE_MASTER,0,0,0)!=ESP_OK)return false;
  state.installed=true;return true;
}
bool i2cTransfer(uint8_t physical,uint8_t address,const uint8_t* tx,size_t tn,uint8_t* rx,size_t rn,uint32_t ms){
  if(!i2c[physical].installed)return false;
  const auto port=static_cast<i2c_port_t>(physical);esp_err_t result;
  if(tn && rn)result=i2c_master_write_read_device(port,address,tx,tn,rx,rn,ticks(ms));
  else if(tn)result=i2c_master_write_to_device(port,address,tx,tn,ticks(ms));
  else result=i2c_master_read_from_device(port,address,rx,rn,ticks(ms));
  return result==ESP_OK;
}
bool i2cClose(uint8_t physical){
  auto& state=i2c[physical];
  // IDF4 param_config enables the peripheral before install allocates its
  // driver. Failed install can leave that clock enabled without a deletable
  // driver object. No public API safely clears the SDK ownership state then.
  // Retain pins/controller and require restart; never claim clean release.
  if(state.configured && !state.installed)return false;
  if(state.installed){if(i2c_driver_delete(static_cast<i2c_port_t>(physical))!=ESP_OK)return false;state.installed=false;state.configured=false;}
  if(state.sda>=0 && !gpioClose(state.sda))return false;
  if(state.scl>=0 && !gpioClose(state.scl))return false;
  state={};return true;
}
bool drain(SpiState& state,uint32_t ms){
  if(!state.pending)return true;
  spi_transaction_t* completed=nullptr;
  if(spi_device_get_trans_result(state.device,&completed,ticks(ms))!=ESP_OK)return false;
  if(completed!=&state.transaction)return false;
  state.pending=false;return true;
}
bool spiOpen(uint8_t physical,int16_t sclk,int16_t mosi,int16_t miso){
  auto& state=spi[physical-2];if(state.initialized)return false;
  state.sclk=sclk;state.mosi=mosi;state.miso=miso;
  spi_bus_config_t config{};config.sclk_io_num=sclk;config.mosi_io_num=mosi;config.miso_io_num=miso;
  config.quadwp_io_num=-1;config.quadhd_io_num=-1;config.max_transfer_sz=512;
  if(spi_bus_initialize(host(physical),&config,SPI_DMA_CH_AUTO)!=ESP_OK)return false;
  state.initialized=true;return true;
}
bool spiBegin(uint8_t physical,uint8_t cs,uint32_t hz,uint8_t mode,uint32_t){
  auto& state=spi[physical-2];if(!state.initialized || state.held || state.pending)return false;
  if(state.device && (state.hz!=hz || state.mode!=mode)){
    if(spi_bus_remove_device(state.device)!=ESP_OK)return false;
    state.device=nullptr;
  }
  if(!state.device){
    spi_device_interface_config_t config{};config.clock_speed_hz=hz;config.mode=mode;config.spics_io_num=-1;config.queue_size=1;
    if(spi_bus_add_device(host(physical),&config,&state.device)!=ESP_OK)return false;
    state.hz=hz;state.mode=mode;
  }
  // IDF4 acquire_bus accepts only an unbounded wait. The CPU port owns this
  // controller exclusively and serializes every device on its owner task, so
  // no SDK bus acquisition is needed. Queued transfers retain bounded waits.
  if(!gpioWrite(cs,false))return false;
  state.held=true;return true;
}
bool spiTransfer(uint8_t physical,const uint8_t* tx,uint8_t* rx,size_t count,uint32_t ms){
  auto& state=spi[physical-2];if(!state.held || state.pending || count>sizeof(state.tx))return false;
  if(tx)memcpy(state.tx,tx,count);else memset(state.tx,0xff,count);
  state.transaction={};state.transaction.length=count*8;state.transaction.tx_buffer=state.tx;state.transaction.rx_buffer=state.rx;
  const int64_t start=esp_timer_get_time();
  if(spi_device_queue_trans(state.device,&state.transaction,ticks(ms))!=ESP_OK)return false;
  state.pending=true;
  const uint32_t elapsed=uint32_t((esp_timer_get_time()-start)/1000);
  if(!drain(state,elapsed<ms?ms-elapsed:0))return false;
  if(rx)memcpy(rx,state.rx,count);
  return true;
}
bool spiEnd(uint8_t physical,uint8_t cs,uint32_t ms){
  auto& state=spi[physical-2];if(!state.held || !drain(state,ms) || !gpioWrite(cs,true))return false;
  state.held=false;return true;
}
bool spiClose(uint8_t physical){
  auto& state=spi[physical-2];if(state.held || state.pending)return false;
  if(state.device){if(spi_bus_remove_device(state.device)!=ESP_OK)return false;
    state.device=nullptr;}
  if(state.initialized){if(spi_bus_free(host(physical))!=ESP_OK)return false;state.initialized=false;}
  for(int pin:{state.sclk,state.mosi,state.miso})if(pin>=0 && !gpioClose(pin))return false;
  state={};return true;
}
bool wakeValid(uint8_t pin){return GPIO_IS_VALID_GPIO(pin);}
bool wakeArm(uint8_t pin,bool active){
  return gpio_wakeup_enable(static_cast<gpio_num_t>(pin),active?GPIO_INTR_HIGH_LEVEL:GPIO_INTR_LOW_LEVEL)==ESP_OK &&
    esp_sleep_enable_gpio_wakeup()==ESP_OK;
}
bool lightSleep(uint32_t* cause){
  for(const auto& state:spi)if(state.held || state.pending)return false;
  if(esp_light_sleep_start()!=ESP_OK)return false;
  *cause=esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_GPIO?RISC_LIGHT_SLEEP_WAKE_GPIO:RISC_LIGHT_SLEEP_WAKE_OTHER;
  return true;
}
bool wakeClear(uint8_t pin){
  const bool pinOk=gpio_wakeup_disable(static_cast<gpio_num_t>(pin))==ESP_OK;
  const esp_err_t cleared=esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  const bool sourceOk=cleared==ESP_OK || cleared==ESP_ERR_INVALID_STATE; // already disabled
  return pinOk && sourceOk;
}

}
Hardware nativeHardware(bool (*owner)()){
  ownerTask=owner;
  return {[](){return !xPortInIsrContext() && ownerTask && ownerTask();},[]()->uint64_t{return uint64_t(esp_timer_get_time())/1000;},
    [](uint32_t ms){vTaskDelay(cooperativeDelayTicks(ms,configTICK_RATE_HZ));},gpioOpen,gpioWrite,gpioRead,gpioPwm,gpioClose,i2cOpen,i2cTransfer,i2cClose,
    spiOpen,spiBegin,spiTransfer,spiEnd,spiClose,wakeValid,wakeArm,lightSleep,wakeClear};
}
}
#endif
