#pragma once
#include "bootstrap/Runtime.h"
#include <GardenPlatformV1.h>
#include <TWatchPlatformV1.h>
#include <RiscPlatformClockV1.h>
namespace RiscCpu {
// Lowest hardware boundary. Production uses ESP-IDF; host models emulate only
// pins, controllers and register/byte transfers, not driver/capability behavior.
struct Hardware {
  bool (*owner)(); uint64_t (*now)(); void (*sleep)(uint32_t);
  bool (*gpioOpen)(uint8_t,bool,bool,bool);
  bool (*gpioWrite)(uint8_t,bool); bool (*gpioRead)(uint8_t,bool*);
  bool (*gpioPwm)(uint8_t,uint32_t,uint16_t,uint16_t); bool (*gpioClose)(uint8_t);
  bool (*i2cOpen)(uint8_t,uint8_t,uint8_t,uint32_t);
  bool (*i2cTransfer)(uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t);
  bool (*i2cClose)(uint8_t);
  bool (*spiOpen)(uint8_t,int16_t,int16_t,int16_t);
  bool (*spiBegin)(uint8_t,uint8_t,uint32_t,uint8_t,uint32_t);
  bool (*spiTransfer)(uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t);
  bool (*spiEnd)(uint8_t,uint8_t,uint32_t); bool (*spiClose)(uint8_t);
};
class Port final {
 public:
  explicit Port(Hardware hardware):hw_(hardware){}
  Port(const Port&)=delete; Port& operator=(const Port&)=delete;
  bool bind(RiscBoot::Runtime&);
  bool quiescent() const;
 private:
  struct Gpio { Port* port=nullptr; uint64_t instance=0,input=0,output=0,pullup=0; garden_gpio_v1 api{}; } gpios_[16];
  struct I2c { Port* port=nullptr; risc_hw_bus_v1 bus{}; uint8_t physical=0; uint64_t token=0; twatch_i2c_controller_v1 api{}; } i2cs_[2];
  struct Spi { Port* port=nullptr; risc_hw_bus_v1 bus{}; uint8_t physical=0,cs=0; uint64_t token=0,deadline=0; garden_spi_v1 api{}; } spis_[8];
  struct SpiBus { uint64_t instance=0; unsigned refs=0; Spi* held=nullptr; } spiBuses_[2];
  struct Pin { const void* owner=nullptr; uint64_t token=0; bool output=false; } pins_[49];
  Hardware hw_; uint64_t serial_=0; bool bound_=false,poisoned_=false;
  size_t gpioCount_=0,i2cCount_=0,spiCount_=0;
  risc_platform_clock_api_v1 clock_{};
  bool available() const { return hw_.owner && hw_.owner() && !poisoned_; }
  uint64_t token(){return serial_==UINT64_MAX?0:++serial_;}
  bool reserve(int16_t,const void*); void unreserve(int16_t,const void*);
  bool gpioScope(const RiscBoot::Runtime&,const RiscBoot::Board::Device&,Gpio&);
  static bool gpioClaim(void*,uint8_t,bool,bool,bool,uint64_t*);
  static bool gpioWrite(void*,uint64_t,bool); static bool gpioRead(void*,uint64_t,bool*);
  static bool gpioPwm(void*,uint64_t,uint32_t,uint16_t,uint16_t);
  static bool gpioRelease(void*,uint64_t); static bool waveform(void*,uint64_t,const uint32_t*,size_t){return false;}
  static bool i2cOpen(void*,uint8_t,uint8_t,uint8_t,uint32_t,uint64_t*);
  static bool i2cTransfer(void*,uint64_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t);
  static bool i2cClose(void*,uint64_t);
  static bool spiClaim(void*,uint8_t,uint8_t,int8_t,uint8_t,uint64_t*);
  static bool spiBegin(void*,uint64_t,uint32_t,uint8_t,uint32_t);
  static bool spiTransfer(void*,uint64_t,const uint8_t*,uint8_t*,size_t);
  static bool spiEnd(void*,uint64_t); static bool spiRelease(void*,uint64_t);
  static bool idleClocks(void*,uint64_t,uint32_t,uint16_t){return false;}
};
Hardware nativeHardware(bool (*owner)());
}
