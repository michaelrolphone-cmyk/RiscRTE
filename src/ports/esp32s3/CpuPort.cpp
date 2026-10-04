#include "CpuPort.h"
#include <cstring>
namespace RiscCpu {
namespace {
uint64_t pinBit(int pin){return pin>=0 && pin<=48?uint64_t(1)<<pin:0;}
void pinsFor(const RiscBoot::Board::Device& d,uint64_t& input,uint64_t& output,uint64_t& pullup){
  if(!strcmp(d.type,"display.spi")){
    const auto& c=d.config.display;input|=pinBit(c.busy);output|=pinBit(c.dc)|pinBit(c.reset)|pinBit(c.backlight);
    for(unsigned i=0;i<c.power_count;++i)output|=pinBit(c.power_pins[i]);
  } else if(!strcmp(d.type,"touch.i2c")){
    const auto& c=d.config.touch;input|=pinBit(c.irq);output|=pinBit(c.reset);if(c.irq_pull_up)pullup|=pinBit(c.irq);
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
bool Port::providerStorageSafe() const {
  if(!available() || sleepRetained_ || transferring_)return false;
  if(hw_.httpSafe && !hw_.httpSafe())return false;
  for(const auto& pin:pins_)if(pin.held)return false;
  for(const auto& c:i2ss_)if(c.token)return false;
  for(const auto& c:radios_)if(c.closing)return false;
  return true;
}
bool Port::appExitSafe() const {
  if(!restartResourcesSafe())return false;
  if(hw_.maintenanceIdle && !hw_.maintenanceIdle())return false;
  return true;
}
bool Port::restartResourcesSafe() const {
  if(!providerStorageSafe())return false;
  if(hw_.httpIdle && !hw_.httpIdle())return false;
  for(const auto& c:radios_)if(c.active)return false;
  return true;
}
bool Port::quiescent() const {
  if(hw_.httpIdle && !hw_.httpIdle())return false;
  if(hw_.maintenanceIdle && !hw_.maintenanceIdle())return false;
  for(const auto& c:radios_)if(c.token || c.active || c.closing)return false;
  for(const auto& p:pins_)if(p.owner)return false;
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
  } else pinsFor(selected,gpio.input,gpio.output,gpio.pullup);
  gpio.api={1,sizeof(gpio.api),&gpio,gpioClaim,gpioWrite,gpioRead,gpioPwm,gpioRelease,waveform,gpioLightSleep,gpioDeepSleep,gpioDeepSleepHold,gpioLightSleepFor,gpioDeepSleepFor};return true;
}
bool Port::bind(RiscBoot::Runtime& runtime){
  if(bound_ || !available() || !hw_.now || !hw_.sleep || !hw_.gpioOpen || !hw_.gpioWrite || !hw_.gpioRead || !hw_.gpioPwm || !hw_.gpioClose ||
     !hw_.i2cOpen || !hw_.i2cTransfer || !hw_.i2cClose || !hw_.spiOpen || !hw_.spiBegin || !hw_.spiTransfer || !hw_.spiEnd || !hw_.spiClose)return false;
  bound_=true;
  clock_={1,sizeof(clock_),this,[](void* c)->uint64_t{auto& p=*static_cast<Port*>(c);return p.hw_.owner()?p.hw_.now():0;},
    [](void* c,uint32_t ms){auto& p=*static_cast<Port*>(c);if(p.hw_.owner())p.hw_.sleep(ms>5000?5000:ms);}};
  if(!runtime.registerPlatform("platform.clock",1,RiscBoot::Runtime::Scope::Global,0,&clock_))return false;
  if(hw_.httpClient){
    const auto* h=hw_.httpClient;
    if(!hw_.httpIdle || !hw_.httpSafe || h->api_version!=1 || h->struct_size<sizeof(*h) || !h->open || !h->read || !h->info || !h->close)return false;
    http_={1,sizeof(http_),this,httpOpen,httpRead,httpInfo,httpClose};
    if(!runtime.registerPlatform(RISC_HTTP_CLIENT_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&http_))return false;
  }
  const auto& board=runtime.board();
  for(size_t n=0;n<board.deviceCount();++n){
    const auto& d=*board.deviceAt(n);const uint64_t id=d.hardware.instance_id;
    if(runtime.uses(id,"platform.gpio",1)){
      if(gpioCount_==16)return false;
      auto& c=gpios_[gpioCount_++];if(!gpioScope(runtime,d,c) || !runtime.registerPlatform("platform.gpio",1,RiscBoot::Runtime::Scope::Device,id,&c.api))return false;
    }
    if(runtime.uses(id,"platform.i2c.controller",1)){
      if(i2cCount_==2 || strcmp(d.type,"controller.i2c"))return false;
      auto& c=i2cs_[i2cCount_++];c.port=this;c.bus=d.config.i2cController.bus;
      int physical=board.physicalController(c.bus.instance_id);if(physical<0 || physical>1)return false;c.physical=physical;
      c.api={1,sizeof(c.api),&c,i2cOpen,i2cTransfer,i2cClose};
      if(!runtime.registerPlatform("platform.i2c.controller",1,RiscBoot::Runtime::Scope::Device,id,&c.api))return false;
    }
    if(runtime.uses(id,"platform.i2s.controller",1)){
      if(i2sCount_==2 || strcmp(d.type,"audio.i2s") || d.config.audio.pdm_rx ||
         !hw_.i2sOpen || !hw_.i2sWrite || !hw_.i2sClose)return false;
      auto& c=i2ss_[i2sCount_++];c.port=this;c.config=d.config.audio;
      c.api={1,sizeof(c.api),&c,i2sOpen,i2sWrite,i2sRead,i2sClose};
      if(!runtime.registerPlatform("platform.i2s.controller",1,RiscBoot::Runtime::Scope::Device,id,&c.api))return false;
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
      // First CPU-port slice supports generic display SPI transport only. Other
      // SPI protocols need their actual required operations (e.g. idle clocks).
      if(spiCount_==8 || strcmp(d.type,"display.spi"))return false;
      auto& c=spis_[spiCount_++];c.port=this;c.bus=d.config.display.bus;c.cs=d.config.display.cs;
      int physical=board.physicalController(c.bus.instance_id);if(physical<2 || physical>3)return false;c.physical=physical;
      c.api={1,sizeof(c.api),&c,spiClaim,spiBegin,spiTransfer,spiEnd,idleClocks,spiRelease};
      if(!runtime.registerPlatform("spi.bus",1,RiscBoot::Runtime::Scope::Device,id,&c.api))return false;
    }
  }
  return true;
}
bool Port::i2sOpen(void* context,uint8_t unit,bool rx,uint8_t clk,int8_t ws,uint8_t data,uint32_t rate,uint8_t channels,uint64_t* out){
  if(out)*out=0;
  auto& c=*static_cast<I2s*>(context);auto& p=*c.port;
  if(!out || !p.available() || p.transferring_ || c.token || rx || channels!=2 || unit!=c.config.controller ||
     clk!=c.config.bclk || ws!=c.config.ws || data!=c.config.data ||
     (rate!=8000 && rate!=16000 && rate!=22050 && rate!=44100))return false;
  if(!p.reserve(clk,&c))return false;
  if(!p.reserve(ws,&c)){p.unreserve(clk,&c);return false;}
  if(!p.reserve(data,&c)){p.unreserve(clk,&c);p.unreserve(ws,&c);return false;}
  const uint64_t t=p.token();
  if(!t){p.unreserve(clk,&c);p.unreserve(ws,&c);p.unreserve(data,&c);return false;}
  p.transferring_=true;const bool ok=p.hw_.i2sOpen(unit,clk,uint8_t(ws),data,rate);p.transferring_=false;
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
  if(!done || !p.available() || p.transferring_ || c.closing || !token || token!=c.token || !pcm || !frames || frames>256 || !ms || ms>40)return false;
  p.transferring_=true;const bool ok=p.hw_.i2sWrite(c.config.controller,pcm,frames,done,ms);p.transferring_=false;
  if(*done>frames){*done=0;p.poisoned_=true;return false;}
  return ok && *done==frames;
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
  if(!p.available() || p.transferring_ || !token || token!=c.token || c.active || c.closing || !ssid || !password)return false;
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
  if(!p.available() || p.transferring_ || !token || token!=c.token || c.active || c.closing)return false;
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
bool Port::gpioClaim(void* context,uint8_t pin,bool output,bool initial,bool pullup,uint64_t* out){
  if(out)*out=0;
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;
  if(!out || !p.available() || !(pinBit(pin)&(output?c.output:c.input)) || (pullup && (!(pinBit(pin)&c.pullup) || output)) || !p.reserve(pin,&c))return false;
  uint64_t token=p.token();
  if(!token || !p.hw_.gpioOpen(pin,output,initial,pullup)){
    if(!p.hw_.gpioClose(pin))p.poisoned_=true;else p.unreserve(pin,&c);return false;
  }
  p.pins_[pin].token=token;p.pins_[pin].output=output;p.pins_[pin].pullup=pullup;*out=token;return true;
}
bool Port::gpioWrite(void* context,uint64_t token,bool level){
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;if(!p.available() || !token)return false;
  for(unsigned i=0;i<49;++i)if(p.pins_[i].owner==&c && p.pins_[i].token==token && p.pins_[i].output && !p.pins_[i].held){
    if(!p.hw_.gpioWrite(i,level))return false;
    p.pins_[i].pwm=false;return true;
  }
  return false;
}
bool Port::gpioRead(void* context,uint64_t token,bool* level){
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;if(!p.available() || !token || !level)return false;
  for(unsigned i=0;i<49;++i)if(p.pins_[i].owner==&c && p.pins_[i].token==token)return p.hw_.gpioRead(i,level);
  return false;
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
  if(p.sleeping_ || p.transferring_ || (p.hw_.httpIdle && !p.hw_.httpIdle()) ||
     (p.hw_.maintenanceIdle && !p.hw_.maintenanceIdle()))return RISC_LIGHT_SLEEP_BUSY;
  for(const auto& b:p.spiBuses_)if(b.held)return RISC_LIGHT_SLEEP_BUSY;
  for(const auto& c:p.i2ss_)if(c.token)return RISC_LIGHT_SLEEP_BUSY;
  for(const auto& c:p.radios_){if(c.closing)return RISC_LIGHT_SLEEP_RETAINED;if(c.active)return RISC_LIGHT_SLEEP_BUSY;}
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
  if(p.sleeping_ || p.transferring_ || (p.hw_.httpIdle && !p.hw_.httpIdle()) ||
     (p.hw_.maintenanceIdle && !p.hw_.maintenanceIdle()))return RISC_DEEP_SLEEP_BUSY;
  for(const auto& b:p.spiBuses_)if(b.held)return RISC_DEEP_SLEEP_BUSY;
  for(const auto& c:p.i2ss_)if(c.token)return RISC_DEEP_SLEEP_BUSY;
  for(const auto& c:p.radios_){if(c.closing)return RISC_DEEP_SLEEP_RETAINED;if(c.active)return RISC_DEEP_SLEEP_BUSY;}
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
int32_t Port::gpioDeepSleepHold(void* context,uint64_t token,bool enable){
  if(!context)return RISC_DEEP_SLEEP_INVALID;
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;
  if(!p.hw_.owner || !p.hw_.owner())return RISC_DEEP_SLEEP_CONTEXT;
  if(p.poisoned_)return RISC_DEEP_SLEEP_RETAINED;
  if(p.sleeping_ || p.transferring_ || (p.hw_.httpIdle && !p.hw_.httpIdle()) ||
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
bool Port::gpioRelease(void* context,uint64_t token){
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;if(!p.hw_.owner() || p.sleepRetained_ || p.sleeping_ || !token)return false;
  for(unsigned i=0;i<49;++i)if(p.pins_[i].owner==&c && p.pins_[i].token==token){if(p.pins_[i].held || !p.hw_.gpioClose(i))return false;p.unreserve(i,&c);return true;}
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
  if(out)*out=0;
  auto& c=*static_cast<Spi*>(context);auto& p=*c.port;auto& bus=p.spiBuses_[c.physical-2];
  if(!out || !p.available() || c.token || sclk!=c.bus.sclk || mosi!=c.bus.mosi || miso!=c.bus.miso || cs!=c.cs ||
     (bus.refs && bus.instance!=c.bus.instance_id) || !p.reserve(cs,&c))return false;
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
  ++bus.refs;c.token=token;*out=token;return true;
}
bool Port::spiBegin(void* context,uint64_t token,uint32_t hz,uint8_t mode,uint32_t ms){
  auto& c=*static_cast<Spi*>(context);auto& p=*c.port;auto& bus=p.spiBuses_[c.physical-2];
  if(!p.available() || !token || c.token!=token || bus.held || !hz || hz>c.bus.frequency_hz || mode!=c.bus.mode || !ms || ms>1000)return false;
  c.deadline=p.hw_.now()+ms;
  if(!p.hw_.spiBegin(c.physical,c.cs,hz,mode,ms))return false;
  bus.held=&c;return true;
}
bool Port::spiTransfer(void* context,uint64_t token,const uint8_t* tx,uint8_t* rx,size_t count){
  auto& c=*static_cast<Spi*>(context);auto& p=*c.port;auto& bus=p.spiBuses_[c.physical-2];
  uint64_t now=p.hw_.now();
  if(!p.available() || !token || c.token!=token || bus.held!=&c || !count || count>512 || now>=c.deadline)return false;
  return p.hw_.spiTransfer(c.physical,tx,rx,count,uint32_t(c.deadline-now));
}
bool Port::spiEnd(void* context,uint64_t token){
  auto& c=*static_cast<Spi*>(context);auto& p=*c.port;auto& bus=p.spiBuses_[c.physical-2];
  if(!p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || !token || c.token!=token || bus.held!=&c)return false;
  uint64_t now=p.hw_.now();uint32_t remaining=now<c.deadline?uint32_t(c.deadline-now):0;
  if(!p.hw_.spiEnd(c.physical,c.cs,remaining))return false;
  bus.held=nullptr;c.deadline=0;return true;
}
bool Port::spiRelease(void* context,uint64_t token){
  auto& c=*static_cast<Spi*>(context);auto& p=*c.port;auto& bus=p.spiBuses_[c.physical-2];
  if(!p.hw_.owner() || p.sleeping_ || p.sleepRetained_ || !token || c.token!=token || bus.held)return false;
  if(bus.refs==1 && !p.hw_.spiClose(c.physical))return false;
  if(!p.hw_.gpioClose(c.cs))return false;
  p.unreserve(c.cs,&c);
  if(--bus.refs==0){p.unreserve(c.bus.sclk,&bus);p.unreserve(c.bus.mosi,&bus);p.unreserve(c.bus.miso,&bus);bus.instance=0;}
  c.token=0;return true;
}
}
