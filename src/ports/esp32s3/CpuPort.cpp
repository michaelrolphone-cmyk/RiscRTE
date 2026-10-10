#include "CpuPort.h"
#include "HciBounds.h"
#include <cstring>
namespace RiscCpu {
namespace {
uint64_t pinBit(int pin){return pin>=0 && pin<=48?uint64_t(1)<<pin:0;}
void pinsFor(const RiscBoot::Board::Device& d,uint64_t& input,uint64_t& output,uint64_t& pullup){
  if(!strcmp(d.type,"display.spi")){
    const auto& c=d.config.display;input|=pinBit(c.busy);output|=pinBit(c.dc)|pinBit(c.reset)|pinBit(c.backlight);
    for(unsigned i=0;i<c.power_count;++i)output|=pinBit(c.power_pins[i]);
  } else if(!strcmp(d.type,"touch.i2c")){
    const auto& c=d.touch();input|=pinBit(c.irq);output|=pinBit(c.reset);if(c.irq_pull_up)pullup|=pinBit(c.irq);
    if(d.hardware.config_version==2){const auto& v=d.config.touchPowered;output|=pinBit(v.power);if(v.irq_output)output|=pinBit(c.irq);}
  } else if(!strcmp(d.type,"radio.lora")){
    const auto& c=d.lora();input|=pinBit(c.busy)|pinBit(c.irq);output|=pinBit(c.reset);
  } else if(!strcmp(d.type,"peripheral.i2c") || !strcmp(d.type,"power.axp2101")){
    const auto& c=!strcmp(d.type,"peripheral.i2c")?d.config.peripheral:d.config.power.device;
    input|=pinBit(c.irq);if(c.irq_pull_up)pullup|=pinBit(c.irq);
  } else if(!strcmp(d.type,"gpio.bank")){
    const auto& c=d.config.gpio;
    for(unsigned i=0;i<c.count;++i){input|=pinBit(c.pins[i]);output|=pinBit(c.pins[i]);if(c.pull_up)pullup|=pinBit(c.pins[i]);}
  }
}
}
bool Port::reserve(int16_t pin,const void* owner){
  if(pin==-1)return true;
  if(pin<0 || pin>48 || pins_[pin].owner)return false;
  pins_[pin].owner=owner;return true;
}
void Port::unreserve(int16_t pin,const void* owner){if(pin>=0 && pin<=48 && pins_[pin].owner==owner)pins_[pin]={};}
#include "CpuSdmmc.inc"
bool Port::providerStorageSafe() const {
  if(sdmmc_.closing)return false;
  if(!available() || sleepRetained_ || transferring_ || iq_.token)return false;
  for(size_t i=0;i<syncCount_;++i)for(const auto& lock:syncs_[i].locks)if(lock.held)return false;
  if(hw_.httpSafe && !hw_.httpSafe())return false;
  for(const auto& pin:pins_)if(pin.held && !pin.retiredHeld)return false;
  for(const auto& c:i2ss_)if(c.closing)return false;
  for(const auto& c:radios_)if(c.closing)return false;
  if(hci_.closing || (hw_.hciSafe && !hw_.hciSafe()))return false;
  return true;
}
bool Port::appExitSafe() const {
  // Healthy HCI is entirely firmware/provider-owned, with no app callbacks or
  // borrowed app storage; it survives navigation. Restart/sleep still drain it.
  if(!providerStorageSafe() || usb_.token || usb_.closing || (hw_.usbPhyIdle && !hw_.usbPhyIdle()))return false;
  for(const auto& pin:pins_)if(pin.wakeModes)return false;
  if(hw_.httpIdle && !hw_.httpIdle())return false;
  for(const auto& c:i2ss_)if(c.token)return false;
  for(const auto& c:radios_)if(c.active)return false;
  if(hw_.maintenanceIdle && !hw_.maintenanceIdle())return false;
  return true;
}
bool Port::restartResourcesSafe() const {
  if(runtime_ && !runtime_->residentResetSafe())return false;
  if(sdmmc_.token || sdmmc_.closing)return false;
  if(!providerStorageSafe() || usb_.token || usb_.closing || (hw_.usbPhyIdle && !hw_.usbPhyIdle()))return false;
  for(const auto& pin:pins_)if(pin.wakeModes)return false;
  if(hw_.httpIdle && !hw_.httpIdle())return false;
  for(const auto& c:i2ss_)if(c.token)return false;
  for(const auto& c:radios_)if(c.active)return false;
  if(hci_.token || (hw_.hciIdle && !hw_.hciIdle()))return false;
  return true;
}
bool Port::quiescent() const {
  if(sdmmc_.token || sdmmc_.closing)return false;
  if(usb_.token || usb_.closing || (hw_.usbPhyIdle && !hw_.usbPhyIdle()))return false;
  if(iq_.token || iq_.closing)return false;
  for(size_t i=0;i<syncCount_;++i)for(const auto& lock:syncs_[i].locks)if(lock.token)return false;
  if(hw_.httpIdle && !hw_.httpIdle())return false;
  if(hw_.maintenanceIdle && !hw_.maintenanceIdle())return false;
  for(const auto& c:radios_)if(c.token || c.active || c.closing)return false;
  if(hci_.token || hci_.closing || (hw_.hciIdle && !hw_.hciIdle()))return false;
  for(const auto& p:pins_)if(p.owner && !p.retiredHeld)return false;
  return !poisoned_;
}
int32_t Port::httpOpen(void* context,const risc_http_request_v1* request,uint64_t* out){
  if(out)*out=0;
  auto& p=*static_cast<Port*>(context);
  if(!p.available() || p.sleepRetained_ || p.transferring_ || !p.hw_.httpClient)return RISC_HTTP_CLOSED;
  if(!p.providerStorageSafe())return RISC_HTTP_RETAINED;
  p.transferring_=true;const auto result=p.hw_.httpClient->open(p.hw_.httpClient->context,request,out);p.transferring_=false;return result;
}
int32_t Port::httpRead(void* context,uint64_t token,void* out,uint32_t size,uint32_t* count){
  if(count)*count=0;
  auto& p=*static_cast<Port*>(context);
  if(!p.available() || p.sleepRetained_ || p.transferring_ || !p.hw_.httpClient)return RISC_HTTP_CLOSED;
  if(!p.providerStorageSafe())return RISC_HTTP_RETAINED;
  p.transferring_=true;const auto result=p.hw_.httpClient->read(p.hw_.httpClient->context,token,out,size,count);p.transferring_=false;return result;
}
int32_t Port::httpInfo(void* context,uint64_t token,risc_http_response_v1* out){
  auto& p=*static_cast<Port*>(context);
  if(!p.available() || p.sleepRetained_ || p.transferring_ || !p.hw_.httpClient)return RISC_HTTP_CLOSED;
  return p.hw_.httpClient->info(p.hw_.httpClient->context,token,out);
}
int32_t Port::httpClose(void* context,uint64_t token){
  auto& p=*static_cast<Port*>(context);
  if(!p.hw_.owner || !p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || p.transferring_ || !p.hw_.httpClient)return RISC_HTTP_CLOSED;
  p.transferring_=true;const auto result=p.hw_.httpClient->close(p.hw_.httpClient->context,token);p.transferring_=false;return result;
}
bool Port::gpioScope(const RiscBoot::Runtime& runtime,const RiscBoot::Board::Device& selected,Gpio& gpio){
  gpio.port=this;gpio.instance=selected.hardware.instance_id;
  if(!strcmp(selected.type,"controller.gpio")){
    // A GPIO facade is scoped to the selected consumers that explicitly bind it.
    // It cannot claim every CPU pad merely by naming controller0.
    const auto& board=runtime.board();
    for(size_t i=0;i<board.deviceCount();++i){
      const auto& d=*board.deviceAt(i);
      if(!runtime.selected(d.hardware.instance_id))continue;
      for(size_t j=0;j<d.bindingCount;++j)
        if(!strcmp(d.bindings[j].capability,"gpio.bank") && d.bindings[j].instance==gpio.instance)
          pinsFor(d,gpio.input,gpio.output,gpio.pullup);
    }
  } else {
    pinsFor(selected,gpio.input,gpio.output,gpio.pullup);
    if(!strcmp(selected.type,"display.spi") && !runtime.uses(gpio.instance,"spi.bus",1)){
      // Explicit platform.gpio without spi.bus selects one bit-banged owner.
      // Preserve normal SPI scopes; reject selected peers before any I/O.
      const auto& config=selected.config.display;
      const auto& board=runtime.board();
      for(size_t i=0;i<board.deviceCount();++i){
        const auto& other=*board.deviceAt(i);
        if(other.hardware.instance_id==gpio.instance || !runtime.selected(other.hardware.instance_id))continue;
        const auto* bus=board.bus(board.deviceBus(other.hardware.instance_id));
        if(bus && bus->kind==RISC_HW_BUS_SPI &&
           (bus->instance_id==config.bus.instance_id || board.physicalController(bus->instance_id)==board.physicalController(config.bus.instance_id)))return false;
      }
      gpio.output|=pinBit(config.bus.sclk)|pinBit(config.bus.mosi)|pinBit(config.cs);
      gpio.input|=pinBit(config.bus.mosi)|pinBit(config.bus.miso);
      gpio.pullup|=pinBit(config.bus.mosi); // Owned bidirectional probe input.
    }
  }
  gpio.api.base={1,sizeof(garden_gpio_v1),&gpio,gpioClaim,gpioWrite,gpioRead,gpioPwm,gpioRelease,waveform,gpioLightSleep,gpioDeepSleep,gpioDeepSleepHold,gpioLightSleepFor,gpioDeepSleepFor,gpioWakeSource,gpioLightSleepSet,gpioDeepSleepSet,gpioRetireHeldOutput,gpioReadRetiredOutput};
  if(!strcmp(selected.type,"gpio.bank") && hw_.sdmmcOpen){
    if(!hw_.sdmmcRead || !hw_.sdmmcWrite || !hw_.sdmmcSync || !hw_.sdmmcClose)return false;
    gpio.api.base.struct_size=sizeof(gpio.api);gpio.api.sdmmc_tag=RISC_GPIO_SDMMC_TAG_V1;gpio.api.sdmmc_version=1;
    gpio.api.sdmmc={1,sizeof(gpio.api.sdmmc),&gpio,sdmmcOpen,sdmmcRead,sdmmcWrite,sdmmcSync,sdmmcRelease};
  }
  return true;
}
bool Port::bind(RiscBoot::Runtime& runtime){
  if(bound_ || !available() || !hw_.now || !hw_.sleep || !hw_.gpioOpen || !hw_.gpioWrite || !hw_.gpioRead || !hw_.gpioPwm || !hw_.gpioClose ||
     !hw_.i2cOpen || !hw_.i2cTransfer || !hw_.i2cClose || !hw_.spiOpen || !hw_.spiBegin || !hw_.spiTransfer || !hw_.spiEnd || !hw_.spiClose)return false;
  bound_=true;runtime_=&runtime;
  clock_={1,sizeof(clock_),this,[](void* c)->uint64_t{auto& p=*static_cast<Port*>(c);return p.hw_.owner()?p.hw_.now():0;},
    [](void* c,uint32_t ms){auto& p=*static_cast<Port*>(c);if(p.hw_.owner())p.hw_.sleep(ms>5000?5000:ms);}};
  if(!runtime.registerPlatform("platform.clock",1,RiscBoot::Runtime::Scope::Global,0,&clock_))return false;
  if(hw_.usbPhyIdle || hw_.usbPhySuspend || hw_.usbPhyResume){
    if(!hw_.usbPhyIdle || !hw_.usbPhySuspend || !hw_.usbPhyResume)return false;
    usb_.port=this;
    usb_.api={1,sizeof(usb_.api),&usb_,RISC_USB_PHY_ESP32S3_OTG,0,usbPhyOwner,usbPhyClaim,usbPhyRelease};
    if(!runtime.registerPlatform(RISC_USB_PHY_RESOURCE_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&usb_.api))return false;
  }
  if(hw_.realtimeRead || hw_.realtimeSeed){
    if(!hw_.realtimeRead || !hw_.realtimeSeed)return false;
    realtime_={1,sizeof(realtime_),this,[](void* c,risc_realtime_snapshot_v1* out)->int32_t{
      auto& p=*static_cast<Port*>(c);
      if(!p.available() || p.sleepRetained_ || p.transferring_)return RISC_REALTIME_CONTEXT;
      return p.hw_.realtimeRead(out);
    },[](void* c,int64_t seconds,uint32_t nanos)->int32_t{
      auto& p=*static_cast<Port*>(c);
      if(!p.available() || p.sleepRetained_ || p.transferring_)return RISC_REALTIME_CONTEXT;
      return p.hw_.realtimeSeed(seconds,nanos);
    }};
    if(!runtime.registerRealtime(&realtime_))return false;
  }
  // CPU-owned opt-in resource, registered even before an IQ ELF is selected.
  // This lets a native-first update retain an identical validated board graph.
  // It remains provider-only; appPolicies rejects direct raw platform grants.
  if(hw_.radioIqReady){
    if(!hw_.radioIqPrepare || !hw_.radioIqCleanup || !hw_.radioIdle || !hw_.hciIdle || !hw_.hciSafe)return false;
    ++iqCount_;iq_.port=this;
    iq_.api={1,sizeof(iq_.api),&iq_,radioIqClaim,radioIqRelease,0x3FCB0000u,65536u};
    if(!runtime.registerPlatform(RISC_RADIO_IQ_RESOURCE_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&iq_.api))return false;
  }
  if(hw_.httpClient){
    const auto* h=hw_.httpClient;
    if(!hw_.httpIdle || !hw_.httpSafe || h->api_version!=1 || h->struct_size<sizeof(*h) || !h->open || !h->read || !h->info || !h->close)return false;
    http_={1,sizeof(http_),this,httpOpen,httpRead,httpInfo,httpClose};
    if(!runtime.registerPlatform(RISC_HTTP_CLIENT_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&http_))return false;
  }
  const auto& board=runtime.board();
  for(size_t n=0;n<board.deviceCount();++n){
    const auto& d=*board.deviceAt(n);const uint64_t id=d.hardware.instance_id;
    if(runtime.uses(id,RISC_PROVIDER_SYNC_CAPABILITY,RISC_PROVIDER_SYNC_API_V1)){
      if(syncCount_==RuntimeProviders::GraphV2::kMaxModules)return false;
      auto& c=syncs_[syncCount_++];c.port=this;c.instance=id;
      c.api={RISC_PROVIDER_SYNC_API_V1,sizeof(c.api),&c,syncOwner,syncCreate,syncTryLock,syncUnlock,syncDestroy};
      if(!runtime.registerPlatform(RISC_PROVIDER_SYNC_CAPABILITY,RISC_PROVIDER_SYNC_API_V1,RiscBoot::Runtime::Scope::Device,id,&c.api))return false;
    }
    if(runtime.uses(id,"platform.gpio",1)){
      if(gpioCount_==16)return false;
      auto& c=gpios_[gpioCount_++];if(!gpioScope(runtime,d,c) || !runtime.registerPlatform("platform.gpio",1,RiscBoot::Runtime::Scope::Device,id,&c.api.base))return false;
    }
    if(runtime.uses(id,"platform.i2c.controller",1)){
      if(i2cCount_==2 || strcmp(d.type,"controller.i2c"))return false;
      auto& c=i2cs_[i2cCount_++];c.port=this;c.bus=d.config.i2cController.bus;
      int physical=board.physicalController(c.bus.instance_id);if(physical<0 || physical>1)return false;c.physical=physical;
      c.api={1,sizeof(c.api),&c,i2cOpen,i2cTransfer,i2cClose};
      if(!runtime.registerPlatform("platform.i2c.controller",1,RiscBoot::Runtime::Scope::Device,id,&c.api))return false;
    }
    if(runtime.uses(id,"platform.i2s.controller",1)){
      if(i2sCount_==2 || strcmp(d.type,"audio.i2s") || !hw_.i2sClose ||
         (d.config.audio.pdm_rx ? (!hw_.i2sOpenRx || !hw_.i2sRead) : (!hw_.i2sOpen || !hw_.i2sWrite)))return false;
      auto& c=i2ss_[i2sCount_++];c.port=this;c.config=d.config.audio;
      c.api={1,sizeof(c.api),&c,i2sOpen,i2sWrite,i2sRead,i2sClose};
      if(!runtime.registerPlatform("platform.i2s.controller",1,RiscBoot::Runtime::Scope::Device,id,&c.api))return false;
    }
    if(runtime.uses(id,"platform.hci.controller",1)){
      if(hciCount_ || strcmp(d.type,"radio.integrated") ||
         strcmp(d.compatible,"espressif,esp32s3-ble") || d.config.radio.unit!=0 || d.config.radio.features!=1 ||
         !hw_.hciOpen || !hw_.hciSend || !hw_.hciReceive || !hw_.hciClose || !hw_.hciIdle || !hw_.hciSafe)return false;
      ++hciCount_;hci_.port=this;
      hci_.api={{1,sizeof(hci_.api),&hci_,hciOpen,hciSend,hciReceive,hciClose},hciStatus};
      if(!runtime.registerPlatform("platform.hci.controller",1,RiscBoot::Runtime::Scope::Device,id,&hci_.api))return false;
    }
    if(runtime.uses(id,"platform.radio",1)){
      // Only the selected integrated station radio receives this authority.
      // AP features in the board record do not imply backend AP support.
      if(radioCount_ || strcmp(d.type,"radio.integrated") ||
         strcmp(d.compatible,"espressif,esp32s3-wifi") || d.config.radio.unit!=0 ||
         !(d.config.radio.features&1u) || (d.config.radio.features&~3u) ||
         !hw_.radioJoin || !hw_.radioState || !hw_.radioLeave || !hw_.radioAddresses ||
         !hw_.radioScanStart || !hw_.radioScanPoll || !hw_.radioScanCancel || !hw_.radioIdle)return false;
      auto& c=radios_[radioCount_++];c.port=this;c.config=d.config.radio;
      c.api={1,sizeof(c.api),&c,radioClaim,radioJoin,radioState,radioLeave,radioRelease,
             radioStartAp,radioStopAp,radioAddresses,radioScanStart,radioScanPoll,radioScanCancel};
      if(!runtime.registerPlatform("platform.radio",1,RiscBoot::Runtime::Scope::Device,id,&c.api))return false;
    }
    if(runtime.uses(id,"spi.bus",1)){
      // Only explicitly materialized transport types receive their own bus/CS.
      // Other SPI protocols still need their actual operations (e.g. idle clocks).
      const bool radio=!strcmp(d.type,"radio.lora");
      if(spiCount_==8 || (!radio && strcmp(d.type,"display.spi")))return false;
      auto& c=spis_[spiCount_++];c.port=this;
      c.bus=radio?d.lora().bus:d.config.display.bus;
      c.cs=radio?d.lora().cs:d.config.display.cs;
      int physical=board.physicalController(c.bus.instance_id);if(physical<2 || physical>3)return false;c.physical=physical;
      c.api={1,sizeof(c.api),&c,spiClaim,spiBegin,spiTransfer,spiEnd,idleClocks,spiRelease,spiClaimThreeWire};
      if(!runtime.registerPlatform("spi.bus",1,RiscBoot::Runtime::Scope::Device,id,&c.api))return false;
    }
  }
  return true;
}
bool Port::usbPhyOwner(void* context){
  if(!context)return false;
  const auto& c=*static_cast<UsbPhy*>(context);
  return c.port && c.port->available() && !c.port->sleepRetained_;
}
bool Port::usbPhyClaim(void* context,uint64_t* out){
  if(out)*out=0;
  if(!context || !out)return false;
  auto& c=*static_cast<UsbPhy*>(context);if(!c.port)return false;auto& p=*c.port;
  if(!usbPhyOwner(context) || p.transferring_ || c.token || c.closing ||
     !p.hw_.usbPhyIdle || !p.hw_.usbPhySuspend || !p.hw_.usbPhyResume || !p.hw_.usbPhyIdle() ||
     p.pins_[19].owner || p.pins_[20].owner || p.serial_==UINT64_MAX)return false;
  // CPU pin ownership excludes ordinary GPIO/bus clients for the lease.
  // The provider retains SD/SPI access; this is not the storage safety fence.
  if(!p.reserve(19,&c))return false;
  if(!p.reserve(20,&c)){p.unreserve(19,&c);return false;}
  c.token=p.token();*out=c.token;
  p.transferring_=true;const bool ok=p.hw_.usbPhySuspend();p.transferring_=false;
  if(!ok && usbPhyOwner(context) && p.hw_.usbPhyIdle()){
    p.unreserve(19,&c);p.unreserve(20,&c);c.token=0;*out=0;return false;
  }
  c.closing=!ok || !usbPhyOwner(context) || p.hw_.usbPhyIdle();
  return !c.closing;
}
bool Port::usbPhyRelease(void* context,uint64_t token){
  if(!context || !token)return false;
  auto& c=*static_cast<UsbPhy*>(context);if(!c.port)return false;auto& p=*c.port;
  if(!usbPhyOwner(context) || p.transferring_ || c.token!=token || !p.hw_.usbPhyResume || !p.hw_.usbPhyIdle)return false;
  c.closing=true;
  p.transferring_=true;const bool ok=p.hw_.usbPhyResume();p.transferring_=false;
  if(!ok || !usbPhyOwner(context) || !p.hw_.usbPhyIdle())return false;
  p.unreserve(19,&c);p.unreserve(20,&c);c.token=0;c.closing=false;return true;
}
bool Port::i2sOpen(void* context,uint8_t unit,bool rx,uint8_t clk,int8_t ws,uint8_t data,uint32_t rate,uint8_t channels,uint64_t* out){
  if(out)*out=0;
  auto& c=*static_cast<I2s*>(context);auto& p=*c.port;
  if(!out || !p.available() || p.transferring_ || c.token || rx!=bool(c.config.pdm_rx) || unit!=c.config.controller ||
     clk!=c.config.bclk || ws!=c.config.ws || data!=c.config.data ||
     (rx ? (unit!=0 || ws!=-1 || channels!=1 || (rate!=8000 && rate!=16000) || !p.hw_.i2sOpenRx || !p.hw_.i2sRead) :
           (channels!=2 || (rate!=8000 && rate!=16000 && rate!=22050 && rate!=44100) || !p.hw_.i2sOpen || !p.hw_.i2sWrite)) ||
     !p.hw_.i2sClose)return false;
  if(!p.reserve(clk,&c))return false;
  if(!p.reserve(ws,&c)){p.unreserve(clk,&c);return false;}
  if(!p.reserve(data,&c)){p.unreserve(clk,&c);p.unreserve(ws,&c);return false;}
  const uint64_t t=p.token();
  if(!t){p.unreserve(clk,&c);p.unreserve(ws,&c);p.unreserve(data,&c);return false;}
  p.transferring_=true;
  const bool ok=rx?p.hw_.i2sOpenRx(unit,clk,data,rate):p.hw_.i2sOpen(unit,clk,uint8_t(ws),data,rate);
  p.transferring_=false;
  if(!ok){
    // Preserve a cleanup token even on failure if hardware cannot prove idle.
    p.transferring_=true;const bool clean=p.hw_.i2sClose(unit);p.transferring_=false;
    if(clean){p.unreserve(clk,&c);p.unreserve(ws,&c);p.unreserve(data,&c);return false;}
    p.poisoned_=true;
  }
  c.token=t;c.closing=!ok;*out=t;return ok;
}
bool Port::i2sWrite(void* context,uint64_t token,const int16_t* pcm,size_t frames,size_t* done,uint32_t ms){
  if(done)*done=0;
  auto& c=*static_cast<I2s*>(context);auto& p=*c.port;
  if(!done || !p.available() || p.transferring_ || c.closing || c.config.pdm_rx || !token || token!=c.token || !pcm || !frames || frames>256 || !ms || ms>40)return false;
  p.transferring_=true;const bool ok=p.hw_.i2sWrite(c.config.controller,pcm,frames,done,ms);p.transferring_=false;
  if(*done>frames){*done=0;p.poisoned_=true;c.closing=true;return false;}
  if(!ok || *done!=frames){c.closing=true;return false;}
  return true;
}
bool Port::i2sRead(void* context,uint64_t token,int16_t* pcm,size_t frames,size_t* done,uint32_t ms){
  if(done)*done=0;
  auto& c=*static_cast<I2s*>(context);auto& p=*c.port;
  if(!done || !p.available() || p.transferring_ || c.closing || !c.config.pdm_rx || !token || token!=c.token || !pcm || !frames || frames>256 || !ms || ms>40)return false;
  p.transferring_=true;const bool ok=p.hw_.i2sRead(c.config.controller,pcm,frames,done,ms);p.transferring_=false;
  if(*done>frames){*done=0;p.poisoned_=true;c.closing=true;return false;}
  if(!ok){c.closing=true;return false;}
  return true;
}
bool Port::i2sClose(void* context,uint64_t token){
  auto& c=*static_cast<I2s*>(context);auto& p=*c.port;
  // No SPI/display drain: these separately owned controllers can stop even
  // while unrelated display DMA is pending. Poison permits cleanup only.
  if(!p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || p.transferring_ || !token || token!=c.token)return false;
  c.closing=true;
  p.transferring_=true;const bool ok=p.hw_.i2sClose(c.config.controller);p.transferring_=false;
  if(!ok)return false;
  p.unreserve(c.config.bclk,&c);p.unreserve(c.config.ws,&c);p.unreserve(c.config.data,&c);c.token=0;c.closing=false;return true;
}
// HCI is opt-in at open, with a cleanup token retained on partial activation.
bool Port::hciOpen(void* context,uint32_t unit,uint64_t* out){
  if(out)*out=0;
  auto& c=*static_cast<Hci*>(context);auto& p=*c.port;
  if(!out || unit || !p.available() || p.sleepRetained_ || p.transferring_ || c.token || c.closing || p.iq_.token || !p.hw_.hciIdle())return false;
  const uint64_t token=p.token();if(!token)return false;
  p.transferring_=true;const bool ok=p.hw_.hciOpen();p.transferring_=false;
  if(!ok){
    p.transferring_=true;const bool clean=p.hw_.hciClose();p.transferring_=false;
    if(clean && p.hw_.hciIdle())return false;
  }
  c.token=token;c.closing=!ok;*out=token;return ok;
}
bool Port::hciSend(void* context,uint64_t token,uint8_t type,const uint8_t* data,size_t size,uint32_t ms){
  auto& c=*static_cast<Hci*>(context);auto& p=*c.port;
  if(!p.available() || p.sleepRetained_ || p.transferring_ || c.closing || !token || token!=c.token || ms>HciBounds::MaxWaitMs || !HciBounds::tx(type,data,size))return false;
  p.transferring_=true;const bool ok=p.hw_.hciSend(type,data,size,ms);p.transferring_=false;
  if(!ok)c.closing=true;
  return ok;
}
bool Port::hciReceive(void* context,uint64_t token,uint8_t* type,uint8_t* data,size_t capacity,size_t* size,uint32_t ms){
  if(size)*size=0;
  if(type)*type=0;
  auto& c=*static_cast<Hci*>(context);auto& p=*c.port;
  if(!type || !data || !size || capacity<HciBounds::MaxPayload || ms>HciBounds::MaxWaitMs || !p.available() || p.sleepRetained_ || p.transferring_ || c.closing || !token || token!=c.token)return false;
  p.transferring_=true;const bool ok=p.hw_.hciReceive(type,data,HciBounds::MaxPayload,size,ms);p.transferring_=false;
  if(!ok || (*size && !HciBounds::rx(*type,data,*size))){*size=0;*type=0;c.closing=true;return false;}
  return true;
}
bool Port::hciClose(void* context,uint64_t token){
  auto& c=*static_cast<Hci*>(context);auto& p=*c.port;
  if(!p.hw_.owner || !p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || p.transferring_ || !token || token!=c.token)return false;
  c.closing=true;p.transferring_=true;const bool ok=p.hw_.hciClose();p.transferring_=false;
  if(!ok || !p.hw_.hciIdle())return false;
  c.token=0;c.closing=false;return true;
}
bool Port::hciStatus(void* context,uint64_t token,uint8_t* state){
  if(state)*state=RISC_HCI_CONTROLLER_RETAINED;
  auto& c=*static_cast<Hci*>(context);auto& p=*c.port;
  if(!state || !p.hw_.owner || !p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || p.transferring_ || token!=c.token)return false;
  if(!token){
    if(c.closing || !p.hw_.hciIdle() || !p.hw_.hciSafe())return false;
    *state=RISC_HCI_CONTROLLER_OFF;return true;
  }
  if(c.closing || !p.hw_.hciSafe() || p.hw_.hciIdle()){
    c.closing=true;*state=RISC_HCI_CONTROLLER_RETAINED;return true;
  }
  *state=RISC_HCI_CONTROLLER_ON;return true;
}
// The lease is synchronous and owner-task bound. An existing idle logical
// station claim may coexist; neither native modem may start until release.
bool Port::radioIqClaim(void* context,uint64_t* out){
  if(out)*out=0;
  if(!context || !out)return false;
  auto& c=*static_cast<RadioIq*>(context);auto& p=*c.port;
  if(c.token || c.closing || !p.providerStorageSafe() || !p.appExitSafe() ||
     !p.hw_.radioIdle || !p.hw_.radioIdle() || !p.hw_.hciIdle || !p.hw_.hciIdle() || p.hci_.token ||
     !p.hw_.radioIqReady)return false;
  for(const auto& bus:p.spiBuses_)if(bus.held)return false;
  // First prove the raw block is parked, then let the native Runtime perform
  // the vendor PHY calibration that the ELF is deliberately not authorized to
  // import. A refused prepare never produces a cleanup token.
  p.transferring_=true;
  const bool ready=p.hw_.radioIqReady();
  const bool prepared=ready && p.hw_.radioIqPrepare && p.hw_.radioIqPrepare();
  p.transferring_=false;
  if(!prepared)return false;
  const uint64_t token=p.token();
  if(!token){
    p.transferring_=true;const bool cleaned=p.hw_.radioIqCleanup && p.hw_.radioIqCleanup();p.transferring_=false;
    if(!cleaned)p.poisoned_=true;
    return false;
  }
  c.token=token;*out=token;return true;
}
bool Port::radioIqRelease(void* context,uint64_t token){
  if(!context)return false;
  auto& c=*static_cast<RadioIq*>(context);auto& p=*c.port;
  if(!p.hw_.owner || !p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || p.transferring_ ||
     !token || token!=c.token)return false;
  c.closing=true;
  // Failed verification or PHY shutdown retains the exact lease and permits
  // a later retry. The external driver must park and restore before release;
  // native cleanup then returns the calibrated PHY to its prior disabled state.
  p.transferring_=true;
  const bool parked=p.hw_.radioIqReady && p.hw_.radioIqReady();
  const bool cleaned=parked && p.hw_.radioIqCleanup && p.hw_.radioIqCleanup();
  const bool ready=cleaned && p.hw_.radioIqReady && p.hw_.radioIqReady();
  p.transferring_=false;
  if(!ready)return false;
  c.token=0;c.closing=false;return true;
}
// Radio ownership is logical until the first join/scan. Idle provider claims
// may span app handoffs; native activity and failed cleanup never may.
bool Port::radioClaim(void* context,uint64_t* out){
  if(out)*out=0;
  auto& c=*static_cast<Radio*>(context);auto& p=*c.port;
  if(!out || !p.available() || p.transferring_ || c.token || c.active || c.closing || !p.hw_.radioIdle())return false;
  const uint64_t t=p.token();if(!t)return false;c.token=t;*out=t;return true;
}
bool Port::radioJoin(void* context,uint64_t token,const char* ssid,const char* password){
  auto& c=*static_cast<Radio*>(context);auto& p=*c.port;
  if(!p.available() || p.transferring_ || p.iq_.token || !token || token!=c.token || c.active || c.closing || !ssid || !password)return false;
  const size_t sn=strnlen(ssid,33),pn=strnlen(password,64);
  if(!sn || sn>32 || pn>63 || (pn && pn<8))return false;
  c.active=true;p.transferring_=true;const bool ok=p.hw_.radioJoin(ssid,password);p.transferring_=false;
  if(!ok){c.closing=true;(void)radioLeave(context,token);}return ok;
}
bool Port::radioState(void* context,uint64_t token,uint8_t* state,int8_t* rssi){
  if(state)*state=0;
  if(rssi)*rssi=-127;
  auto& c=*static_cast<Radio*>(context);auto& p=*c.port;
  if(!state || !rssi || !p.available() || p.transferring_ || !token || token!=c.token || c.closing)return false;
  if(!c.active || c.scanning)return true;
  p.transferring_=true;const bool ok=p.hw_.radioState(state,rssi);p.transferring_=false;
  if(!ok || *state>2){c.closing=true;*state=0;*rssi=-127;return false;}return true;
}
bool Port::radioLeave(void* context,uint64_t token){
  auto& c=*static_cast<Radio*>(context);auto& p=*c.port;
  // Retained cleanup can be retried, but a terminal sleep failure cannot.
  if(!p.hw_.owner || !p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || p.transferring_ || !token || token!=c.token)return false;
  // A live TLS socket depends on the station interface. Drain it first; a
  // rejected out-of-order leave is not itself failed native radio cleanup.
  if(p.hw_.httpIdle && !p.hw_.httpIdle())return false;
  if(!c.active && !c.closing)return p.hw_.radioIdle();
  c.closing=true;p.transferring_=true;const bool ok=p.hw_.radioLeave();p.transferring_=false;
  if(!ok || !p.hw_.radioIdle())return false;
  c.active=c.closing=c.scanning=false;return true;
}
bool Port::radioRelease(void* context,uint64_t token){
  auto& c=*static_cast<Radio*>(context);
  if(!radioLeave(context,token))return false;
  c.token=0;return true;
}
bool Port::radioStopAp(void* context,uint64_t token){
  auto& c=*static_cast<Radio*>(context);auto& p=*c.port;
  // Station-only backend can truthfully report no AP is active. Preserve old
  // providers' stop_ap -> leave -> release cleanup sequence.
  return p.hw_.owner && p.hw_.owner() && !p.sleeping_ && !p.sleepRetained_ && !p.transferring_ && token && token==c.token;
}
bool Port::radioAddresses(void* context,uint64_t token,uint8_t* station,uint8_t* ap){
  if(station)memset(station,0,12);
  if(ap)memset(ap,0,12);
  auto& c=*static_cast<Radio*>(context);auto& p=*c.port;
  if(!station || !ap || !p.available() || p.transferring_ || !token || token!=c.token || c.closing)return false;
  if(!c.active || c.scanning)return true;
  p.transferring_=true;const bool ok=p.hw_.radioAddresses(station,ap);p.transferring_=false;
  if(!ok){c.closing=true;memset(station,0,12);memset(ap,0,12);}return ok;
}
bool Port::radioScanStart(void* context,uint64_t token){
  auto& c=*static_cast<Radio*>(context);auto& p=*c.port;
  if(!p.available() || p.transferring_ || p.iq_.token || !token || token!=c.token || c.active || c.closing)return false;
  c.active=c.scanning=true;p.transferring_=true;const bool ok=p.hw_.radioScanStart();p.transferring_=false;
  if(!ok){c.closing=true;(void)radioLeave(context,token);}return ok;
}
bool Port::radioScanPoll(void* context,uint64_t token,garden_radio_scan_result_v1* out){
  if(!out || out->struct_size<sizeof(*out))return false;
  *out={};out->struct_size=sizeof(*out);
  auto& c=*static_cast<Radio*>(context);auto& p=*c.port;
  if(!p.available() || p.transferring_ || !token || token!=c.token || c.closing)return false;
  if(!c.scanning)return true;
  garden_radio_scan_result_v1 result{};result.struct_size=sizeof(result);
  p.transferring_=true;const bool ok=p.hw_.radioScanPoll(&result);p.transferring_=false;
  if(!ok || result.count>GARDEN_RADIO_SCAN_MAX || result.state>GARDEN_RADIO_SCAN_FAILED || result.reserved ||
     (result.state!=GARDEN_RADIO_SCAN_DONE && result.count)){c.closing=true;return false;}
  for(unsigned i=0;i<result.count;++i){const auto& e=result.entries[i];
    if(strnlen(e.ssid,sizeof(e.ssid))>32 || e.reserved || (e.auth>GARDEN_RADIO_AUTH_WPA2_WPA3_PSK && e.auth!=GARDEN_RADIO_AUTH_UNSUPPORTED)){c.closing=true;return false;}
  }
  *out=result;out->struct_size=sizeof(*out);return true;
}
bool Port::radioScanCancel(void* context,uint64_t token){
  auto& c=*static_cast<Radio*>(context);auto& p=*c.port;
  if(!p.hw_.owner || !p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || p.transferring_ || !token || token!=c.token)return false;
  if(!c.scanning)return true;
  c.closing=true;p.transferring_=true;const bool ok=p.hw_.radioScanCancel();p.transferring_=false;
  if(!ok || !p.hw_.radioIdle())return false;
  c.active=c.closing=c.scanning=false;return true;
}
bool Port::syncOwner(void* context){
  if(!context)return false;
  const auto& c=*static_cast<Sync*>(context);
  return c.port && c.port->available() && !c.port->sleepRetained_;
}
bool Port::syncCreate(void* context,uint64_t* out){
  if(out)*out=0;
  if(!out || !syncOwner(context))return false;
  auto& c=*static_cast<Sync*>(context);
  for(auto& lock:c.locks)if(!lock.token){
    const uint64_t token=c.port->token();if(!token)return false;
    lock={token,false};*out=token;return true;
  }
  return false;
}
bool Port::syncTryLock(void* context,uint64_t token){
  if(!token || !syncOwner(context))return false;
  auto& c=*static_cast<Sync*>(context);
  for(auto& lock:c.locks)if(lock.token==token){
    if(lock.held)return false;
    lock.held=true;return true;
  }
  return false;
}
bool Port::syncUnlock(void* context,uint64_t token){
  if(!context || !token)return false;
  auto& c=*static_cast<Sync*>(context);
  if(!c.port || !c.port->hw_.owner || !c.port->hw_.owner() || c.port->sleeping_)return false;
  for(auto& lock:c.locks)if(lock.token==token && lock.held){lock.held=false;return true;}
  return false;
}
bool Port::syncDestroy(void* context,uint64_t token){
  if(!context || !token)return false;
  auto& c=*static_cast<Sync*>(context);
  if(!c.port || !c.port->hw_.owner || !c.port->hw_.owner() || c.port->sleeping_)return false;
  for(auto& lock:c.locks)if(lock.token==token && !lock.held){lock={};return true;}
  return false;
}
bool Port::gpioClaim(void* context,uint8_t pin,bool output,bool initial,bool pullup,uint64_t* out){
  if(out)*out=0;
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;
  if(!out || !p.available() || !(pinBit(pin)&(output?c.output:c.input)) || (pullup && (!(pinBit(pin)&c.pullup) || output)))return false;
  if(p.pins_[pin].retiredHeld){
    // A retired static hold remains CPU-owned. Only its original scope may
    // replace it; the backend stages the requested level before unholding.
    if(p.pins_[pin].owner!=&c)return false;
    const uint64_t fresh=p.token();if(!fresh)return false;
    if(!p.hw_.gpioOpen(pin,output,initial,pullup)){p.poisoned_=true;return false;}
    p.pins_[pin]={};p.pins_[pin].owner=&c;p.pins_[pin].token=fresh;
    p.pins_[pin].output=output;p.pins_[pin].pullup=pullup;*out=fresh;return true;
  }
  if(!p.reserve(pin,&c))return false;
  uint64_t token=p.token();
  if(!token || !p.hw_.gpioOpen(pin,output,initial,pullup)){
    if(!p.hw_.gpioClose(pin))p.poisoned_=true;else p.unreserve(pin,&c);return false;
  }
  p.pins_[pin].token=token;p.pins_[pin].output=output;p.pins_[pin].pullup=pullup;*out=token;return true;
}
bool Port::gpioWrite(void* context,uint64_t token,bool level){
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;if(!p.available() || !token)return false;
  auto& hint=p.gpioWritePins_[token & 63u];
  unsigned i=hint?unsigned(hint-1):49;
  if(i==49 || p.pins_[i].owner!=&c || p.pins_[i].token!=token){
    // A hash collision is only a miss. Opaque tokens retain their original
    // monotonic allocation, including exhaustion and stale-token semantics.
    for(i=0;i<49;++i)if(p.pins_[i].owner==&c && p.pins_[i].token==token)break;
    if(i==49)return false;
    hint=static_cast<uint8_t>(i+1);
  }
  auto& pin=p.pins_[i];
  if(!pin.output || pin.held || !p.hw_.gpioWrite(i,level))return false;
  pin.pwm=false;return true;
}
bool Port::gpioRead(void* context,uint64_t token,bool* level){
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;if(!p.available() || !token || !level)return false;
  auto& hint=p.gpioWritePins_[token & 63u];
  unsigned i=hint?unsigned(hint-1):49;
  if(i==49 || p.pins_[i].owner!=&c || p.pins_[i].token!=token){
    // Share the validated pin hint with writes. Full owner/token checks on
    // every hit preserve release, scope, generation and retired-pad rules.
    for(i=0;i<49;++i)if(p.pins_[i].owner==&c && p.pins_[i].token==token)break;
    if(i==49)return false;
    hint=static_cast<uint8_t>(i+1);
  }
  return p.hw_.gpioRead(i,level);
}
bool Port::gpioPwm(void* context,uint64_t token,uint32_t hz,uint16_t duty,uint16_t maximum){
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;if(!p.available() || !token || !hz || hz>40000 || !maximum || duty>maximum)return false;
  for(unsigned i=0;i<49;++i)if(p.pins_[i].owner==&c && p.pins_[i].token==token && p.pins_[i].output && !p.pins_[i].held){
    // Failed PWM can still leave a partially configured timer/channel. Only a
    // subsequent successful static write establishes a safe holdable output.
    p.pins_[i].pwm=true;return p.hw_.gpioPwm(i,hz,duty,maximum);
  }
  return false;
}
int32_t Port::gpioLightSleep(void* context,uint64_t token,bool active,risc_light_sleep_result_v1* out){
  return lightSleepImpl(context,token,active,0,out);
}
int32_t Port::gpioLightSleepFor(void* context,uint64_t token,bool active,uint32_t ms,risc_light_sleep_result_v1* out){
  if(!ms || ms>RISC_TIMED_SLEEP_MAX_MS)return RISC_LIGHT_SLEEP_INVALID;
  return lightSleepImpl(context,token,active,ms,out);
}
int32_t Port::lightSleepImpl(void* context,uint64_t token,bool active,uint32_t ms,risc_light_sleep_result_v1* out){
  if(!context || !out || out->struct_size<sizeof(*out))return RISC_LIGHT_SLEEP_INVALID;
  out->wake_cause=RISC_LIGHT_SLEEP_WAKE_NONE;
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;
  if(!p.hw_.owner || !p.hw_.owner())return RISC_LIGHT_SLEEP_CONTEXT;
  if(p.poisoned_)return RISC_LIGHT_SLEEP_RETAINED;
  if(p.iq_.closing || p.usb_.closing)return RISC_LIGHT_SLEEP_RETAINED;
  if(p.usb_.token || (p.hw_.usbPhyIdle && !p.hw_.usbPhyIdle()))return RISC_LIGHT_SLEEP_BUSY;
  if(p.sleeping_ || p.transferring_ || p.iq_.token || (p.hw_.httpIdle && !p.hw_.httpIdle()) ||
     (p.hw_.maintenanceIdle && !p.hw_.maintenanceIdle()))return RISC_LIGHT_SLEEP_BUSY;
  for(const auto& b:p.spiBuses_)if(b.held)return RISC_LIGHT_SLEEP_BUSY;
  // Failed cleanup outranks healthy activity on either selected I2S unit.
  for(const auto& c:p.i2ss_)if(c.closing)return RISC_LIGHT_SLEEP_RETAINED;
  for(const auto& c:p.i2ss_)if(c.token)return RISC_LIGHT_SLEEP_BUSY;
  for(const auto& c:p.radios_){if(c.closing)return RISC_LIGHT_SLEEP_RETAINED;if(c.active)return RISC_LIGHT_SLEEP_BUSY;}
  if(p.hci_.closing || (p.hw_.hciSafe && !p.hw_.hciSafe()))return RISC_LIGHT_SLEEP_RETAINED;
  if(p.hci_.token || (p.hw_.hciIdle && !p.hw_.hciIdle()))return RISC_LIGHT_SLEEP_BUSY;
  int pin=-1;
  for(unsigned i=0;i<49;++i)if(token && p.pins_[i].owner==&c && p.pins_[i].token==token && !p.pins_[i].output)pin=i;
  if(pin<0)return RISC_LIGHT_SLEEP_INVALID;
  if(!p.hw_.wakeValid || !p.hw_.wakeArm || !p.hw_.lightSleep || !p.hw_.wakeClear || !p.hw_.wakeValid(pin) ||
     (ms && (!p.hw_.timerArm || !p.hw_.timerClear)))return RISC_LIGHT_SLEEP_UNSUPPORTED;
  bool level=false;
  if(!p.hw_.gpioRead(pin,&level))return RISC_LIGHT_SLEEP_PLATFORM;
  if(level==active)return RISC_LIGHT_SLEEP_ACTIVE_WAKE;
  p.sleeping_=true;
  int32_t result=RISC_LIGHT_SLEEP_PLATFORM;
  if(p.hw_.wakeArm(pin,active) && (!ms || p.hw_.timerArm(ms))){
    if(p.hw_.gpioRead(pin,&level)){
      if(level==active)result=RISC_LIGHT_SLEEP_ACTIVE_WAKE;
      else {uint32_t cause=RISC_LIGHT_SLEEP_WAKE_NONE;
        if(p.hw_.lightSleep(&cause)){
          // Old callbacks retain their original GPIO/OTHER result vocabulary.
          out->wake_cause=(!ms && cause==RISC_LIGHT_SLEEP_WAKE_TIMER)?uint32_t(RISC_LIGHT_SLEEP_WAKE_OTHER):cause;
          result=RISC_LIGHT_SLEEP_OK;
        }
      }
    }
  }
  // Even a failed arm can leave partial configuration; cleanup is mandatory.
  const bool timerClean=!ms || p.hw_.timerClear();
  const bool inputClean=p.hw_.wakeClear(pin);
  if(!timerClean || !inputClean){p.poisoned_=p.sleepRetained_=true;result=RISC_LIGHT_SLEEP_RETAINED;}
  p.sleeping_=false;
  return result;
}
int32_t Port::gpioDeepSleep(void* context,uint64_t token,bool active){
  return deepSleepImpl(context,token,active,0);
}
int32_t Port::gpioDeepSleepFor(void* context,uint64_t token,bool active,uint32_t ms){
  if(!ms || ms>RISC_TIMED_SLEEP_MAX_MS)return RISC_DEEP_SLEEP_INVALID;
  return deepSleepImpl(context,token,active,ms);
}
int32_t Port::deepSleepImpl(void* context,uint64_t token,bool active,uint32_t ms){
  if(!context)return RISC_DEEP_SLEEP_INVALID;
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;
  if(!p.hw_.owner || !p.hw_.owner())return RISC_DEEP_SLEEP_CONTEXT;
  if(p.poisoned_)return RISC_DEEP_SLEEP_RETAINED;
  if(p.iq_.closing || p.usb_.closing)return RISC_DEEP_SLEEP_RETAINED;
  if(p.runtime_ && !p.runtime_->residentResetSafe())return RISC_DEEP_SLEEP_BUSY;
  if(p.usb_.token || (p.hw_.usbPhyIdle && !p.hw_.usbPhyIdle()))return RISC_DEEP_SLEEP_BUSY;
  if(p.sleeping_ || p.transferring_ || p.iq_.token || (p.hw_.httpIdle && !p.hw_.httpIdle()) ||
     (p.hw_.maintenanceIdle && !p.hw_.maintenanceIdle()))return RISC_DEEP_SLEEP_BUSY;
  for(const auto& b:p.spiBuses_)if(b.held)return RISC_DEEP_SLEEP_BUSY;
  // Failed cleanup outranks healthy activity on either selected I2S unit.
  for(const auto& c:p.i2ss_)if(c.closing)return RISC_DEEP_SLEEP_RETAINED;
  for(const auto& c:p.i2ss_)if(c.token)return RISC_DEEP_SLEEP_BUSY;
  for(const auto& c:p.radios_){if(c.closing)return RISC_DEEP_SLEEP_RETAINED;if(c.active)return RISC_DEEP_SLEEP_BUSY;}
  if(p.hci_.closing || (p.hw_.hciSafe && !p.hw_.hciSafe()))return RISC_DEEP_SLEEP_RETAINED;
  if(p.hci_.token || (p.hw_.hciIdle && !p.hw_.hciIdle()))return RISC_DEEP_SLEEP_BUSY;
  for(const auto& pin:p.pins_)if(pin.pwm)return RISC_DEEP_SLEEP_BUSY;
  int pin=-1;
  for(unsigned i=0;i<49;++i)if(token && p.pins_[i].owner==&c && p.pins_[i].token==token && !p.pins_[i].output)pin=i;
  if(pin<0)return RISC_DEEP_SLEEP_INVALID;
  if(!p.hw_.deepWakeValid || !p.hw_.deepReady || !p.hw_.deepWakeArm || !p.hw_.deepWakeClear ||
     !p.hw_.deepSleep || !p.hw_.deepWakeValid(pin) ||
     (ms && (!p.hw_.timerArm || !p.hw_.timerClear)))return RISC_DEEP_SLEEP_UNSUPPORTED;
  if(!p.hw_.deepReady())return RISC_DEEP_SLEEP_BUSY;
  bool level=false;
  if(!p.hw_.gpioRead(pin,&level))return RISC_DEEP_SLEEP_PLATFORM;
  if(level==active)return RISC_DEEP_SLEEP_ACTIVE_WAKE;
  const bool pullup=p.pins_[pin].pullup;
  p.sleeping_=true;
  int32_t result=RISC_DEEP_SLEEP_PLATFORM;
  if(p.hw_.deepWakeArm(pin,active,pullup) && (!ms || p.hw_.timerArm(ms))){
    if(p.hw_.gpioRead(pin,&level)){
      if(level==active)result=RISC_DEEP_SLEEP_ACTIVE_WAKE;
      else {
        // Successful hardware entry is terminal. It does not resume this stack,
        // app invocation, provider graph or token namespace after wake.
        p.hw_.deepSleep();
        // A returning backend violates that contract; hardware state is unknown.
        p.poisoned_=p.sleepRetained_=true;
        // Preserve the original untimed terminal-return contract. The timed
        // suffix additionally attempts both source cleanups but stays retained.
        if(!ms){p.sleeping_=false;return RISC_DEEP_SLEEP_RETAINED;}
        result=RISC_DEEP_SLEEP_RETAINED;
      }
    }
  }
  // Arm may have changed pulls/domain policy before failing. Cleanup is required
  // even then. Explicit output holds belong to their callers and are not stolen.
  const bool timerClean=!ms || p.hw_.timerClear();
  const bool inputClean=p.hw_.deepWakeClear(pin,pullup);
  if(!timerClean || !inputClean){
    p.poisoned_=p.sleepRetained_=true;result=RISC_DEEP_SLEEP_RETAINED;
  }
  p.sleeping_=false;
  return result;
}
int32_t Port::gpioWakeSource(void* context,uint64_t token,bool high,uint32_t modes){
  if(!context || !token || modes&~uint32_t(RISC_WAKE_SET_LIGHT|RISC_WAKE_SET_DEEP))return RISC_LIGHT_SLEEP_INVALID;
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;
  if(!p.hw_.owner || !p.hw_.owner())return RISC_LIGHT_SLEEP_CONTEXT;
  if(p.poisoned_ || p.sleepRetained_)return RISC_LIGHT_SLEEP_RETAINED;
  if(p.sleeping_ || p.transferring_)return RISC_LIGHT_SLEEP_BUSY;
  unsigned count=0;for(const auto& pin:p.pins_)if(pin.wakeModes)++count;
  for(unsigned i=0;i<49;++i){auto& pin=p.pins_[i];
    if(pin.owner!=&c || pin.token!=token || pin.output)continue;
    if(!modes){pin.wakeModes=0;pin.wakeHigh=false;return 0;}
    if(pin.wakeModes)return pin.wakeModes==modes && pin.wakeHigh==high?0:RISC_LIGHT_SLEEP_INVALID;
    if(count>=RISC_WAKE_SET_MAX)return RISC_LIGHT_SLEEP_BUSY;
    if((modes&RISC_WAKE_SET_LIGHT) && (!p.hw_.wakeValid || !p.hw_.wakeValid(i)))return RISC_LIGHT_SLEEP_UNSUPPORTED;
    if((modes&RISC_WAKE_SET_DEEP) && (!p.hw_.deepWakeValid || !p.hw_.deepWakeValid(i)))return RISC_LIGHT_SLEEP_UNSUPPORTED;
    pin.wakeModes=uint8_t(modes);pin.wakeHigh=high;return 0;
  }
  return RISC_LIGHT_SLEEP_INVALID;
}
int32_t Port::gpioLightSleepSet(void* c,uint64_t t,bool high,uint32_t ms,risc_light_sleep_result_v1* out){
  return sleepSetImpl(c,t,high,ms,false,out);
}
int32_t Port::gpioDeepSleepSet(void* c,uint64_t t,bool high,uint32_t ms){return sleepSetImpl(c,t,high,ms,true,nullptr);}
int32_t Port::sleepSetImpl(void* context,uint64_t token,bool active,uint32_t ms,bool deep,risc_light_sleep_result_v1* out){
  if(!context || !token || ms>RISC_TIMED_SLEEP_MAX_MS || (!deep && (!out || out->struct_size<sizeof(*out))))return RISC_LIGHT_SLEEP_INVALID;
  if(out)out->wake_cause=RISC_LIGHT_SLEEP_WAKE_NONE;
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;
  if(!p.hw_.owner || !p.hw_.owner())return RISC_LIGHT_SLEEP_CONTEXT;
  if(p.poisoned_ || p.sleepRetained_)return RISC_LIGHT_SLEEP_RETAINED;
  if(p.iq_.closing || p.usb_.closing)return RISC_LIGHT_SLEEP_RETAINED;
  if(deep && p.runtime_ && !p.runtime_->residentResetSafe())return RISC_LIGHT_SLEEP_BUSY;
  if(p.usb_.token || (p.hw_.usbPhyIdle && !p.hw_.usbPhyIdle()))return RISC_LIGHT_SLEEP_BUSY;
  if(p.sleeping_ || p.transferring_ || p.iq_.token || (p.hw_.httpIdle && !p.hw_.httpIdle()) ||
     (p.hw_.maintenanceIdle && !p.hw_.maintenanceIdle()))return RISC_LIGHT_SLEEP_BUSY;
  for(const auto& b:p.spiBuses_)if(b.held)return RISC_LIGHT_SLEEP_BUSY;
  for(const auto& i:p.i2ss_)if(i.closing)return RISC_LIGHT_SLEEP_RETAINED;
  for(const auto& i:p.i2ss_)if(i.token)return RISC_LIGHT_SLEEP_BUSY;
  for(const auto& i:p.radios_){if(i.closing)return RISC_LIGHT_SLEEP_RETAINED;if(i.active)return RISC_LIGHT_SLEEP_BUSY;}
  if(p.hci_.closing || (p.hw_.hciSafe && !p.hw_.hciSafe()))return RISC_LIGHT_SLEEP_RETAINED;
  if(p.hci_.token || (p.hw_.hciIdle && !p.hw_.hciIdle()))return RISC_LIGHT_SLEEP_BUSY;
  if(deep)for(const auto& pin:p.pins_)if(pin.pwm)return RISC_LIGHT_SLEEP_BUSY;
  int anchor=-1;for(unsigned i=0;i<49;++i)if(p.pins_[i].owner==&c && p.pins_[i].token==token && !p.pins_[i].output)anchor=i;
  if(anchor<0)return RISC_LIGHT_SLEEP_INVALID;
  uint64_t mask=0,high=0,pulls=0;unsigned count=0;
  const uint8_t mode=deep?RISC_WAKE_SET_DEEP:RISC_WAKE_SET_LIGHT;
  for(unsigned i=0;i<49;++i){const auto& pin=p.pins_[i];
    if(int(i)!=anchor && !(pin.wakeModes&mode))continue;
    if(!pin.owner || !pin.token || pin.output)return RISC_LIGHT_SLEEP_INVALID;
    if(int(i)==anchor && (pin.wakeModes&mode) && pin.wakeHigh!=active)return RISC_LIGHT_SLEEP_INVALID;
    if(++count>RISC_WAKE_SET_MAX)return RISC_LIGHT_SLEEP_BUSY;
    mask|=pinBit(i);if(int(i)==anchor?active:pin.wakeHigh)high|=pinBit(i);if(pin.pullup)pulls|=pinBit(i);
  }
  if(ms && (!p.hw_.timerArm || !p.hw_.timerClear))return RISC_LIGHT_SLEEP_UNSUPPORTED;
  if(deep){
    if(!p.hw_.deepWakeSetValid || !p.hw_.deepWakeSetArm || !p.hw_.deepWakeSetClear || !p.hw_.deepReady || !p.hw_.deepSleep ||
       !p.hw_.deepWakeSetValid(mask,high))return RISC_LIGHT_SLEEP_UNSUPPORTED;
    if(!p.hw_.deepReady())return RISC_LIGHT_SLEEP_BUSY;
  }else{
    if(!p.hw_.wakeValid || !p.hw_.wakeArm || !p.hw_.wakeClear || !p.hw_.lightSleep)return RISC_LIGHT_SLEEP_UNSUPPORTED;
    for(unsigned i=0;i<49;++i)if((mask&pinBit(i)) && !p.hw_.wakeValid(i))return RISC_LIGHT_SLEEP_UNSUPPORTED;
  }
  auto quiet=[&](){for(unsigned i=0;i<49;++i)if(mask&pinBit(i)){bool level=false;
    if(!p.hw_.gpioRead(i,&level))return int32_t(RISC_LIGHT_SLEEP_PLATFORM);
    if(level==bool(high&pinBit(i)))return int32_t(RISC_LIGHT_SLEEP_ACTIVE_WAKE);
  }return int32_t(0);};
  int32_t result=quiet();if(result)return result;
  p.sleeping_=true;uint64_t attempted=0;bool armOk=true,timerAttempted=false;
  if(deep){attempted=mask;armOk=p.hw_.deepWakeSetArm(mask,high,pulls);}
  else for(unsigned i=0;i<49 && armOk;++i)if(mask&pinBit(i)){attempted|=pinBit(i);armOk=p.hw_.wakeArm(i,bool(high&pinBit(i)));}
  if(armOk && ms){timerAttempted=true;armOk=p.hw_.timerArm(ms);}
  result=RISC_LIGHT_SLEEP_PLATFORM;
  if(armOk){result=quiet();if(!result){
    if(deep){p.hw_.deepSleep();p.poisoned_=p.sleepRetained_=true;result=RISC_LIGHT_SLEEP_RETAINED;}
    else{uint32_t cause=RISC_LIGHT_SLEEP_WAKE_NONE;if(p.hw_.lightSleep(&cause)){out->wake_cause=(!ms && cause==RISC_LIGHT_SLEEP_WAKE_TIMER)?uint32_t(RISC_LIGHT_SLEEP_WAKE_OTHER):cause;}else result=RISC_LIGHT_SLEEP_PLATFORM;}
  }}
  bool clean=!timerAttempted || p.hw_.timerClear();
  if(deep){if(!p.hw_.deepWakeSetClear(mask,high,pulls))clean=false;}
  else for(unsigned i=0;i<49;++i)if((attempted&pinBit(i)) && !p.hw_.wakeClear(i))clean=false;
  if(!clean){p.poisoned_=p.sleepRetained_=true;result=RISC_LIGHT_SLEEP_RETAINED;}
  p.sleeping_=false;return result;
}
int32_t Port::gpioDeepSleepHold(void* context,uint64_t token,bool enable){
  if(!context)return RISC_DEEP_SLEEP_INVALID;
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;
  if(!p.hw_.owner || !p.hw_.owner())return RISC_DEEP_SLEEP_CONTEXT;
  if(p.poisoned_)return RISC_DEEP_SLEEP_RETAINED;
  if(p.iq_.closing || p.usb_.closing)return RISC_DEEP_SLEEP_RETAINED;
  if(p.usb_.token || (p.hw_.usbPhyIdle && !p.hw_.usbPhyIdle()))return RISC_DEEP_SLEEP_BUSY;
  if(p.sleeping_ || p.transferring_ || p.iq_.token || (p.hw_.httpIdle && !p.hw_.httpIdle()) ||
     (p.hw_.maintenanceIdle && !p.hw_.maintenanceIdle()))return RISC_DEEP_SLEEP_BUSY;
  for(unsigned i=0;i<49;++i){
    auto& pin=p.pins_[i];
    if(!token || pin.owner!=&c || pin.token!=token || !pin.output)continue;
    if(!p.hw_.deepHold)return RISC_DEEP_SLEEP_UNSUPPORTED;
    if(pin.pwm)return RISC_DEEP_SLEEP_BUSY;
    if(pin.held==enable)return 0;
    p.sleeping_=true; // Reentrant operations cannot mutate a partially held pad.
    if(p.hw_.deepHold(i,enable)){pin.held=enable;p.sleeping_=false;return 0;}
    if(enable && p.hw_.deepHold(i,false)){p.sleeping_=false;return RISC_DEEP_SLEEP_PLATFORM;}
    pin.held=true;p.poisoned_=p.sleepRetained_=true;p.sleeping_=false;
    return RISC_DEEP_SLEEP_RETAINED;
  }
  return RISC_DEEP_SLEEP_INVALID;
}
bool Port::gpioRetireHeldOutput(void* context,uint64_t token){
  if(!context || !token)return false;
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;
  if(!p.available() || p.sleepRetained_ || p.transferring_)return false;
  for(auto& pin:p.pins_)if(pin.owner==&c && pin.token==token && pin.output && pin.held && !pin.pwm && !pin.wakeModes){
    pin.token=0;pin.retiredHeld=true;return true;
  }
  return false;
}
bool Port::gpioReadRetiredOutput(void* context,uint8_t pin,bool* level){
  if(level)*level=false;
  if(!context || !level || pin>48)return false;
  auto& c=*static_cast<Gpio*>(context);if(!c.port)return false;
  auto& p=*c.port;
  // A copied/forged scope cannot acquire read authority by copying its masks.
  bool scoped=false;for(size_t i=0;i<p.gpioCount_;++i)if(&p.gpios_[i]==&c){scoped=true;break;}
  if(!scoped || !p.available() || p.sleepRetained_ || p.transferring_ ||
     !(c.output&pinBit(pin)) || !p.hw_.gpioRead)return false;
  const auto& held=p.pins_[pin];
  if(held.owner!=&c || held.token || !held.output || !held.held ||
     !held.retiredHeld || held.pwm || held.wakeModes)return false;
  // Fence reentry across the physical read. In particular, a nested fresh
  // claim must not unhold/reconfigure the pad while it is being sampled.
  p.sleeping_=true;bool sampled=false;
  const bool ok=p.hw_.gpioRead(pin,&sampled);p.sleeping_=false;
  if(ok)*level=sampled;
  return ok;
}
bool Port::gpioRelease(void* context,uint64_t token){
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;if(!p.hw_.owner() || p.sleepRetained_ || p.sleeping_ || !token)return false;
  for(unsigned i=0;i<49;++i)if(p.pins_[i].owner==&c && p.pins_[i].token==token){if(p.pins_[i].held || p.pins_[i].wakeModes || !p.hw_.gpioClose(i))return false;p.unreserve(i,&c);return true;}
  return false;
}
bool Port::i2cOpen(void* context,uint8_t controller,uint8_t sda,uint8_t scl,uint32_t hz,uint64_t* out){
  if(out)*out=0;
  auto& c=*static_cast<I2c*>(context);auto& p=*c.port;
  if(!out || !p.available() || c.token || controller!=c.bus.controller || sda!=c.bus.sda || scl!=c.bus.scl || hz!=c.bus.frequency_hz)return false;
  if(!p.reserve(sda,&c))return false;
  if(!p.reserve(scl,&c)){p.unreserve(sda,&c);return false;}
  uint64_t token=p.token();
  if(!token || !p.hw_.i2cOpen(c.physical,sda,scl,hz)){
    if(!p.hw_.i2cClose(c.physical))p.poisoned_=true;else {p.unreserve(sda,&c);p.unreserve(scl,&c);}return false;
  }
  c.token=token;*out=token;return true;
}
bool Port::i2cTransfer(void* context,uint64_t token,uint8_t address,const uint8_t* tx,size_t tn,uint8_t* rx,size_t rn,uint32_t ms){
  auto& c=*static_cast<I2c*>(context);auto& p=*c.port;
  if(!p.available() || !token || c.token!=token || address<8 || address>119 || (!tn && !rn) || tn>512 || rn>512 || (tn && !tx) || (rn && !rx) || !ms || ms>1000)return false;
  if(p.transferring_)return false;
  p.transferring_=true;const bool ok=p.hw_.i2cTransfer(c.physical,address,tx,tn,rx,rn,ms);p.transferring_=false;return ok;
}
bool Port::i2cClose(void* context,uint64_t token){
  auto& c=*static_cast<I2c*>(context);auto& p=*c.port;if(!p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || !token || token!=c.token || !p.hw_.i2cClose(c.physical))return false;
  p.unreserve(c.bus.sda,&c);p.unreserve(c.bus.scl,&c);c.token=0;return true;
}
bool Port::spiClaim(void* context,uint8_t sclk,uint8_t mosi,int8_t miso,uint8_t cs,uint64_t* out){
  return spiClaimImpl(context,sclk,mosi,miso,cs,out,false);
}
bool Port::spiClaimThreeWire(void* context,uint8_t sclk,uint8_t mosi,uint8_t cs,uint64_t* out){
  return spiClaimImpl(context,sclk,mosi,-1,cs,out,true);
}
bool Port::spiClaimImpl(void* context,uint8_t sclk,uint8_t mosi,int8_t miso,uint8_t cs,uint64_t* out,bool threeWire){
  if(out)*out=0;
  auto& c=*static_cast<Spi*>(context);auto& p=*c.port;auto& bus=p.spiBuses_[c.physical-2];
  if(!out || !p.available() || c.token || bus.closing || sclk!=c.bus.sclk || mosi!=c.bus.mosi || miso!=c.bus.miso || cs!=c.cs ||
     (threeWire && !p.hw_.spiBeginThreeWire) || (bus.refs && bus.instance!=c.bus.instance_id) || !p.reserve(cs,&c))return false;
  uint64_t token=p.token();
  if(!token || !p.hw_.gpioOpen(cs,true,true,false)){if(!p.hw_.gpioClose(cs))p.poisoned_=true;else p.unreserve(cs,&c);return false;}
  if(!bus.refs){
    bool reserved=p.reserve(sclk,&bus) && p.reserve(mosi,&bus) && p.reserve(miso,&bus);
    if(!reserved || !p.hw_.spiOpen(c.physical,sclk,mosi,miso)){
      if(reserved && !p.hw_.spiClose(c.physical))p.poisoned_=true;
      if(!p.poisoned_){p.unreserve(sclk,&bus);p.unreserve(mosi,&bus);p.unreserve(miso,&bus);}
      if(!p.hw_.gpioClose(cs))p.poisoned_=true;else p.unreserve(cs,&c);return false;
    }
    bus.instance=c.bus.instance_id;
  }
  ++bus.refs;c.threeWire=threeWire;c.ready=false;c.closing=false;c.token=token;*out=token;return true;
}
bool Port::spiBegin(void* context,uint64_t token,uint32_t hz,uint8_t mode,uint32_t ms){
  auto& c=*static_cast<Spi*>(context);auto& p=*c.port;auto& bus=p.spiBuses_[c.physical-2];
  if(!p.available() || !token || c.token!=token || c.closing || bus.closing || bus.held || !hz || hz>c.bus.frequency_hz || mode!=c.bus.mode || !ms || ms>1000)return false;
  c.deadline=p.hw_.now()+ms;
  if(c.threeWire){
    // Backend mode/pad changes are fallible. Retain the whole transaction
    // before entering it, so even a failed begin has an explicit end path.
    bus.held=&c;c.ready=false;
    c.ready=p.hw_.spiBeginThreeWire(c.physical,c.cs,hz,mode,ms);
    return c.ready;
  }
  if(!p.hw_.spiBegin(c.physical,c.cs,hz,mode,ms))return false;
  bus.held=&c;c.ready=true;return true;
}
bool Port::spiTransfer(void* context,uint64_t token,const uint8_t* tx,uint8_t* rx,size_t count){
  auto& c=*static_cast<Spi*>(context);auto& p=*c.port;auto& bus=p.spiBuses_[c.physical-2];
  uint64_t now=p.hw_.now();
  if(!p.available() || !token || c.token!=token || bus.held!=&c || !count || count>512 || now>=c.deadline)return false;
  if(c.threeWire && (!c.ready || bool(tx)==bool(rx)))return false;
  uint32_t remaining=uint32_t(c.deadline-now);
  if(c.threeWire && remaining>8)remaining=8;
  const bool result=p.hw_.spiTransfer(c.physical,tx,rx,count,remaining);
  if(c.threeWire && !result)c.ready=false;
  return result;
}
bool Port::spiEnd(void* context,uint64_t token){
  auto& c=*static_cast<Spi*>(context);auto& p=*c.port;auto& bus=p.spiBuses_[c.physical-2];
  if(!p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || !token || c.token!=token || bus.held!=&c)return false;
  uint64_t now=p.hw_.now();uint32_t remaining=now<c.deadline?uint32_t(c.deadline-now):0;
  if(c.threeWire){c.ready=false;if(remaining>8)remaining=8;}
  if(!p.hw_.spiEnd(c.physical,c.cs,remaining))return false;
  bus.held=nullptr;c.deadline=0;c.ready=false;return true;
}
bool Port::spiRelease(void* context,uint64_t token){
  auto& c=*static_cast<Spi*>(context);auto& p=*c.port;auto& bus=p.spiBuses_[c.physical-2];
  if(!p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || !token || c.token!=token || bus.held || (bus.closing && bus.closing!=&c))return false;
  if(c.threeWire){c.closing=true;bus.closing=&c;}
  if(bus.refs==1 && !p.hw_.spiClose(c.physical))return false;
  if(!p.hw_.gpioClose(c.cs))return false;
  p.unreserve(c.cs,&c);
  if(--bus.refs==0){p.unreserve(c.bus.sclk,&bus);p.unreserve(c.bus.mosi,&bus);p.unreserve(c.bus.miso,&bus);bus.instance=0;}
  if(bus.closing==&c)bus.closing=nullptr;
  c.token=0;c.threeWire=false;c.ready=false;c.closing=false;return true;
}
}
