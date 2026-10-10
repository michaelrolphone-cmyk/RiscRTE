#include "diagnostics/Performance.h"
#ifdef ESP_PLATFORM
#include "CpuPort.h"
#include "CooperativeDelay.h"
#include "NativeSleep.h"
#include "NativeRetainedWake.h"
#include "NativeI2s.h"
#include "NativeRadio.h"
#include "NativeHci.h"
#include "SleepDiagnostics.h"
#ifdef RISC_ENABLE_RADIO_IQ
#include "NativeRadioIq.h"
#endif
#ifdef RISC_PAIRED_APP_DATA
#include "NativeAppData.h"
#endif
#ifdef RISC_ENABLE_HTTP
#include "NativeHttp.h"
#endif
#ifdef RISC_PAIRED_BANKS
#include "NativeBankStore.h"
#endif
#include <driver/gpio.h>
#include <driver/i2c.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_timer.h>
#include "NativeRealtime.h"
#include <esp_sleep.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#if RISC_ENABLE_USB_PHY
// Readonly, linked opt-in evidence for native candidate validation. The
// volatile read below keeps this exact marker in the enabled target ELF.
extern "C" const uint32_t risc_usb_phy_resource_enabled=1;
#endif
#if RISC_ENABLE_SDMMC
#include "NativeSdmmc.h"
extern "C" __attribute__((used)) const uint32_t risc_sdmmc_host_abi=1;
#endif
namespace RiscCpu { namespace {
bool (*ownerTask)()=nullptr;
struct I2cState { bool installed=false,configured=false;int sda=-1,scl=-1; } i2c[2];
TickType_t ticks(uint32_t ms){return ms?pdMS_TO_TICKS(ms)+1:0;}
spi_host_device_t host(uint8_t physical){return physical==2?SPI2_HOST:SPI3_HOST;}
#include "NativePwmStop.inc"
bool gpioOpen(uint8_t pin,bool output,bool initial,bool pullup){
  return pin<49 && stopPwm(pin) && NativeSleep::openPin(pin,output,initial,pullup);
}
bool gpioWrite(uint8_t pin,bool level){
  return stopPwm(pin) && gpio_set_level(static_cast<gpio_num_t>(pin),level)==ESP_OK;
}
bool gpioRead(uint8_t pin,bool* out){*out=gpio_get_level(static_cast<gpio_num_t>(pin))!=0;return true;}
bool gpioClose(uint8_t pin){
  // A failed post-reset claim may still have a pad held at its retained state.
  // Never claim clean release or unhold it without the requested safe config.
  if(pin>=49 || !NativeSleep::canClose(pin))return false;
  return stopPwm(pin) && gpio_reset_pin(static_cast<gpio_num_t>(pin))==ESP_OK;
}
#include "NativePwm.inc"
bool gpioPwm(uint8_t pin,uint32_t hz,uint16_t duty,uint16_t maximum){return pwmWrite(pin,hz,duty,maximum);}
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
#include "NativeSpi.inc"
bool deepReady(){
#if RISC_ENABLE_SDMMC
  if(!NativeSdmmc::closed())return false;
#endif
  // IDF digital-pad isolation cannot run with an external/PSRAM task stack.
  if(!NativeSleep::stackReady() || !NativeI2s::idle() || !NativeRadio::idle() || !NativeHci::idle())return false;
#ifdef RISC_ENABLE_HTTP
  if(!NativeHttp::idle())return false;
#endif
#ifdef RISC_PAIRED_BANKS
  if(!RiscBankStore::exitSafe())return false;
#ifdef RISC_PAIRED_APP_DATA
  if(!RiscAppData::exitSafe())return false;
#endif
#endif
  for(const auto& state:spi)if(state.held || state.pending)return false;
  return true;
}
bool wakeValid(uint8_t pin){return GPIO_IS_VALID_GPIO(pin);}
bool lightSleep(uint32_t* cause){
#if RISC_ENABLE_SDMMC
  if(!NativeSdmmc::idle())return false;
#endif
  if(!NativeI2s::idle() || !NativeRadio::idle() || !NativeHci::idle())return false;
#ifdef RISC_ENABLE_HTTP
  if(!NativeHttp::idle())return false;
#endif
#ifdef RISC_PAIRED_BANKS
  if(!RiscBankStore::exitSafe())return false;
#ifdef RISC_PAIRED_APP_DATA
  if(!RiscAppData::exitSafe())return false;
#endif
#endif
  for(const auto& state:spi)if(state.held || state.pending)return false;
  return NativeSleep::lightEnter(cause);
}

}
Hardware nativeHardware(bool (*owner)()){
  ownerTask=owner;
  Hardware hardware{[](){return !xPortInIsrContext() && ownerTask && ownerTask();},[]()->uint64_t{return uint64_t(esp_timer_get_time())/1000;},
    [](uint32_t ms){
      RiscPerf::AggregateScope wait(32,ms);
#if RISC_DIAGNOSTIC_ADAPTER
      RiscDiagnostics::poll();
#endif
      vTaskDelay(cooperativeDelayTicks(ms,configTICK_RATE_HZ));
    },gpioOpen,gpioWrite,gpioRead,gpioPwm,gpioClose,i2cOpen,i2cTransfer,i2cClose,
    spiOpen,spiBegin,spiTransfer,spiEnd,spiClose,wakeValid,NativeSleep::lightArm,lightSleep,NativeSleep::lightClear,
    NativeSleep::valid,deepReady,NativeSleep::arm,NativeSleep::clear,[](){NativeRealtime::enter([](){NativeRetainedWake::enter(NativeSleep::enter);});},NativeSleep::hold,NativeSleep::timerArm,NativeSleep::timerClear,NativeI2s::open,NativeI2s::write,NativeI2s::close,
    NativeRadio::join,NativeRadio::state,NativeRadio::leave,NativeRadio::addresses,NativeRadio::scanStart,NativeRadio::scanPoll,NativeRadio::scanCancel,NativeRadio::idle};
  hardware.spiBeginThreeWire=spiBeginThreeWire;
#if RISC_ENABLE_SDMMC
  NativeSdmmc::configure(hardware.owner);
  hardware.sdmmcOpen=NativeSdmmc::open;hardware.sdmmcRead=NativeSdmmc::read;
  hardware.sdmmcWrite=NativeSdmmc::write;hardware.sdmmcSync=NativeSdmmc::sync;
  hardware.sdmmcClose=NativeSdmmc::close;
#endif
#if RISC_ENABLE_USB_PHY
  if(*static_cast<volatile const uint32_t*>(&risc_usb_phy_resource_enabled)==1){
    hardware.usbPhyIdle=RiscDiagnostics::usbPhyIdle;
    hardware.usbPhySuspend=RiscDiagnostics::suspendUsbPhy;
    hardware.usbPhyResume=RiscDiagnostics::resumeUsbPhy;
  }
#endif
  NativeRealtime::configure(hardware.owner);hardware.realtimeRead=NativeRealtime::read;hardware.realtimeSeed=NativeRealtime::seed;
  hardware.hciOpen=NativeHci::open;hardware.hciSend=NativeHci::send;hardware.hciReceive=NativeHci::receive;
  hardware.hciClose=NativeHci::close;hardware.hciIdle=NativeHci::idle;hardware.hciSafe=NativeHci::safe;
#ifdef RISC_ENABLE_RADIO_IQ
  hardware.radioIqReady=NativeRadioIq::ready;
  hardware.radioIqPrepare=NativeRadioIq::prepare;
  hardware.radioIqCleanup=NativeRadioIq::cleanup;
#endif
  hardware.i2sOpenRx=NativeI2s::openRx;hardware.i2sRead=NativeI2s::read;
#ifdef RISC_ENABLE_HTTP
  NativeHttp::configure(hardware.owner,[](){
    uint8_t state=0,station[12]{},ap[12]{};int8_t rssi=0;
    return NativeRadio::state(&state,&rssi) && state==2 && NativeRadio::addresses(station,ap) &&
      (station[0]||station[1]||station[2]||station[3]);
  });
  hardware.httpClient=NativeHttp::api();hardware.httpIdle=NativeHttp::idle;hardware.httpSafe=NativeHttp::safe;
#endif
#ifdef RISC_PAIRED_BANKS
  hardware.maintenanceIdle=[](){return RiscBankStore::exitSafe()
#ifdef RISC_PAIRED_APP_DATA
    && RiscAppData::exitSafe()
#endif
    ;};
#endif
  hardware.deepWakeSetValid=NativeSleep::setValid;hardware.deepWakeSetArm=NativeSleep::setArm;hardware.deepWakeSetClear=NativeSleep::setClear;
  return hardware;
}
}
#endif
