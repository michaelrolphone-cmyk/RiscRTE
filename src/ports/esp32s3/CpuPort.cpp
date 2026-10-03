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
bool Port::quiescent() const {
  for(const auto& p:pins_)if(p.owner)return false;
  return !poisoned_;
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
  gpio.api={1,sizeof(gpio.api),&gpio,gpioClaim,gpioWrite,gpioRead,gpioPwm,gpioRelease,waveform};return true;
}
bool Port::bind(RiscBoot::Runtime& runtime){
  if(bound_ || !available() || !hw_.now || !hw_.sleep || !hw_.gpioOpen || !hw_.gpioWrite || !hw_.gpioRead || !hw_.gpioPwm || !hw_.gpioClose ||
     !hw_.i2cOpen || !hw_.i2cTransfer || !hw_.i2cClose || !hw_.spiOpen || !hw_.spiBegin || !hw_.spiTransfer || !hw_.spiEnd || !hw_.spiClose)return false;
  bound_=true;
  clock_={1,sizeof(clock_),this,[](void* c)->uint64_t{auto& p=*static_cast<Port*>(c);return p.hw_.owner()?p.hw_.now():0;},
    [](void* c,uint32_t ms){auto& p=*static_cast<Port*>(c);if(p.hw_.owner())p.hw_.sleep(ms>5000?5000:ms);}};
  if(!runtime.registerPlatform("platform.clock",1,RiscBoot::Runtime::Scope::Global,0,&clock_))return false;
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
bool Port::gpioClaim(void* context,uint8_t pin,bool output,bool initial,bool pullup,uint64_t* out){
  if(out)*out=0;
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;
  if(!out || !p.available() || !(pinBit(pin)&(output?c.output:c.input)) || (pullup && (!(pinBit(pin)&c.pullup) || output)) || !p.reserve(pin,&c))return false;
  uint64_t token=p.token();
  if(!token || !p.hw_.gpioOpen(pin,output,initial,pullup)){
    if(!p.hw_.gpioClose(pin))p.poisoned_=true;else p.unreserve(pin,&c);return false;
  }
  p.pins_[pin].token=token;p.pins_[pin].output=output;*out=token;return true;
}
bool Port::gpioWrite(void* context,uint64_t token,bool level){
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;if(!p.available() || !token)return false;
  for(unsigned i=0;i<49;++i)if(p.pins_[i].owner==&c && p.pins_[i].token==token && p.pins_[i].output)return p.hw_.gpioWrite(i,level);
  return false;
}
bool Port::gpioRead(void* context,uint64_t token,bool* level){
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;if(!p.available() || !token || !level)return false;
  for(unsigned i=0;i<49;++i)if(p.pins_[i].owner==&c && p.pins_[i].token==token)return p.hw_.gpioRead(i,level);
  return false;
}
bool Port::gpioPwm(void* context,uint64_t token,uint32_t hz,uint16_t duty,uint16_t maximum){
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;if(!p.available() || !token || !hz || hz>40000 || !maximum || duty>maximum)return false;
  for(unsigned i=0;i<49;++i)if(p.pins_[i].owner==&c && p.pins_[i].token==token && p.pins_[i].output)return p.hw_.gpioPwm(i,hz,duty,maximum);
  return false;
}
bool Port::gpioRelease(void* context,uint64_t token){
  auto& c=*static_cast<Gpio*>(context);auto& p=*c.port;if(!p.hw_.owner() || !token)return false;
  for(unsigned i=0;i<49;++i)if(p.pins_[i].owner==&c && p.pins_[i].token==token){if(!p.hw_.gpioClose(i))return false;p.unreserve(i,&c);return true;}
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
  return p.hw_.i2cTransfer(c.physical,address,tx,tn,rx,rn,ms);
}
bool Port::i2cClose(void* context,uint64_t token){
  auto& c=*static_cast<I2c*>(context);auto& p=*c.port;if(!p.hw_.owner() || !token || token!=c.token || !p.hw_.i2cClose(c.physical))return false;
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
  if(!p.hw_.owner() || !token || c.token!=token || bus.held!=&c)return false;
  uint64_t now=p.hw_.now();uint32_t remaining=now<c.deadline?uint32_t(c.deadline-now):0;
  if(!p.hw_.spiEnd(c.physical,c.cs,remaining))return false;
  bus.held=nullptr;c.deadline=0;return true;
}
bool Port::spiRelease(void* context,uint64_t token){
  auto& c=*static_cast<Spi*>(context);auto& p=*c.port;auto& bus=p.spiBuses_[c.physical-2];
  if(!p.hw_.owner() || !token || c.token!=token || bus.held)return false;
  if(bus.refs==1 && !p.hw_.spiClose(c.physical))return false;
  if(!p.hw_.gpioClose(c.cs))return false;
  p.unreserve(c.cs,&c);
  if(--bus.refs==0){p.unreserve(c.bus.sclk,&bus);p.unreserve(c.bus.mosi,&bus);p.unreserve(c.bus.miso,&bus);bus.instance=0;}
  c.token=0;return true;
}
}
