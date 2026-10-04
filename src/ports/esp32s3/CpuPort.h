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
  bool (*wakeValid)(uint8_t)=nullptr;
  bool (*wakeArm)(uint8_t,bool)=nullptr;
  bool (*lightSleep)(uint32_t*)=nullptr;
  bool (*wakeClear)(uint8_t)=nullptr;
  bool (*deepWakeValid)(uint8_t)=nullptr;
  bool (*deepReady)()=nullptr;
  bool (*deepWakeArm)(uint8_t,bool,bool)=nullptr;
  bool (*deepWakeClear)(uint8_t,bool)=nullptr;
  void (*deepSleep)()=nullptr;
  bool (*deepHold)(uint8_t,bool)=nullptr;
  bool (*timerArm)(uint32_t)=nullptr;
  bool (*timerClear)()=nullptr;
  // Additive standard signed16 stereo TX; buffers are copied before return.
  bool (*i2sOpen)(uint8_t,uint8_t,uint8_t,uint8_t,uint32_t)=nullptr;
  bool (*i2sWrite)(uint8_t,const int16_t*,size_t,size_t*,uint32_t)=nullptr;
  bool (*i2sClose)(uint8_t)=nullptr;
  // Exclusive station radio, lazily initialized by join/scan. No credentials
  // may persist outside RAM; leave must prove all native activity is stopped.
  bool (*radioJoin)(const char*,const char*)=nullptr;
  bool (*radioState)(uint8_t*,int8_t*)=nullptr;
  bool (*radioLeave)()=nullptr;
  bool (*radioAddresses)(uint8_t*,uint8_t*)=nullptr;
  bool (*radioScanStart)()=nullptr;
  bool (*radioScanPoll)(garden_radio_scan_result_v1*)=nullptr;
  bool (*radioScanCancel)()=nullptr;
  bool (*radioIdle)()=nullptr;
};
class Port final {
 public:
  explicit Port(Hardware hardware):hw_(hardware){}
  Port(const Port&)=delete; Port& operator=(const Port&)=delete;
  bool bind(RiscBoot::Runtime&);
  bool quiescent() const;
  // Ordinary live provider claims may survive app handoff. Poison, sleep entry
  // or retained output holds may not outlive the invocation that owns policy.
  bool appExitSafe() const;
  // Healthy station/scan activity blocks exit but does not revoke provider KV.
  // Actual native cleanup failure retains the existing storage safety barrier.
  bool providerStorageSafe() const;
 private:
  struct Gpio { Port* port=nullptr; uint64_t instance=0,input=0,output=0,pullup=0; garden_gpio_v1 api{}; } gpios_[16];
  struct I2c { Port* port=nullptr; risc_hw_bus_v1 bus{}; uint8_t physical=0; uint64_t token=0; twatch_i2c_controller_v1 api{}; } i2cs_[2];
  struct Spi { Port* port=nullptr; risc_hw_bus_v1 bus{}; uint8_t physical=0,cs=0; uint64_t token=0,deadline=0; garden_spi_v1 api{}; } spis_[8];
  struct SpiBus { uint64_t instance=0; unsigned refs=0; Spi* held=nullptr; } spiBuses_[2];
  struct I2s { Port* port=nullptr; tw_hw_audio_v1 config{}; uint64_t token=0; bool closing=false; twatch_i2s_controller_v1 api{}; } i2ss_[2];
  struct Radio { Port* port=nullptr; risc_hw_radio_v1 config{}; uint64_t token=0;
    bool active=false,closing=false,scanning=false; garden_radio_v1 api{}; } radios_[1];
  struct Pin { const void* owner=nullptr; uint64_t token=0; bool output=false,pullup=false,held=false,pwm=false; } pins_[49];
  Hardware hw_; uint64_t serial_=0; bool bound_=false,poisoned_=false,sleeping_=false,sleepRetained_=false,transferring_=false;
  size_t gpioCount_=0,i2cCount_=0,spiCount_=0,i2sCount_=0,radioCount_=0;
  risc_platform_clock_api_v1 clock_{};
  bool available() const { return hw_.owner && hw_.owner() && !poisoned_ && !sleeping_; }
  uint64_t token(){return serial_==UINT64_MAX?0:++serial_;}
  bool reserve(int16_t,const void*); void unreserve(int16_t,const void*);
  bool gpioScope(const RiscBoot::Runtime&,const RiscBoot::Board::Device&,Gpio&);
  static bool gpioClaim(void*,uint8_t,bool,bool,bool,uint64_t*);
  static bool gpioWrite(void*,uint64_t,bool); static bool gpioRead(void*,uint64_t,bool*);
  static bool gpioPwm(void*,uint64_t,uint32_t,uint16_t,uint16_t);
  static int32_t gpioLightSleep(void*,uint64_t,bool,risc_light_sleep_result_v1*);
  static int32_t gpioDeepSleep(void*,uint64_t,bool);
  static int32_t gpioDeepSleepHold(void*,uint64_t,bool);
  static int32_t gpioLightSleepFor(void*,uint64_t,bool,uint32_t,risc_light_sleep_result_v1*);
  static int32_t gpioDeepSleepFor(void*,uint64_t,bool,uint32_t);
  static int32_t lightSleepImpl(void*,uint64_t,bool,uint32_t,risc_light_sleep_result_v1*);
  static int32_t deepSleepImpl(void*,uint64_t,bool,uint32_t);
  static bool gpioRelease(void*,uint64_t); static bool waveform(void*,uint64_t,const uint32_t*,size_t){return false;}
  static bool i2cOpen(void*,uint8_t,uint8_t,uint8_t,uint32_t,uint64_t*);
  static bool i2cTransfer(void*,uint64_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t);
  static bool i2cClose(void*,uint64_t);
  static bool spiClaim(void*,uint8_t,uint8_t,int8_t,uint8_t,uint64_t*);
  static bool spiBegin(void*,uint64_t,uint32_t,uint8_t,uint32_t);
  static bool spiTransfer(void*,uint64_t,const uint8_t*,uint8_t*,size_t);
  static bool spiEnd(void*,uint64_t); static bool spiRelease(void*,uint64_t);
  static bool i2sOpen(void*,uint8_t,bool,uint8_t,int8_t,uint8_t,uint32_t,uint8_t,uint64_t*);
  static bool i2sWrite(void*,uint64_t,const int16_t*,size_t,size_t*,uint32_t);
  static bool i2sRead(void*,uint64_t,int16_t*,size_t,size_t* done,uint32_t){if(done)*done=0;return false;}
  static bool i2sClose(void*,uint64_t);
  static bool radioClaim(void*,uint64_t*);
  static bool radioJoin(void*,uint64_t,const char*,const char*);
  static bool radioState(void*,uint64_t,uint8_t*,int8_t*);
  static bool radioLeave(void*,uint64_t);
  static bool radioRelease(void*,uint64_t);
  static bool radioStartAp(void*,uint64_t,const char*,const char*,const uint8_t*,const uint8_t*,const uint8_t*){return false;}
  static bool radioStopAp(void*,uint64_t);
  static bool radioAddresses(void*,uint64_t,uint8_t*,uint8_t*);
  static bool radioScanStart(void*,uint64_t);
  static bool radioScanPoll(void*,uint64_t,garden_radio_scan_result_v1*);
  static bool radioScanCancel(void*,uint64_t);
  static bool idleClocks(void*,uint64_t,uint32_t,uint16_t){return false;}
};
Hardware nativeHardware(bool (*owner)());
}
