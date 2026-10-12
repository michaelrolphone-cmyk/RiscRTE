#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
#include <vector>
using namespace RiscCpu;

namespace {
struct Terminal {};
enum Event { Prepare, Arm, Timer, Enter, TimerClear, Clear, Cancel };
std::vector<Event> trace;
Port* current;
void* context;
uint64_t crown, motion, backlight;
bool owner, prepared, prepareOk, cancelOk, armOk, timerOk, clearOk, timerClearOk;
bool readOk, activeWake, activateInPrepare, failReadAfterArm, returnTerminal;
bool outputHeld, outputHigh, hooks;
unsigned sdkEntries;
bool owned(){return owner;}
bool open(uint8_t,bool,bool,bool){return true;}
bool close(uint8_t){return true;}
bool write(uint8_t pin,bool high){assert(pin==45);outputHigh=high;return true;}
bool read(uint8_t pin,bool* high){*high=pin==21 && !activeWake;return readOk;}
bool valid(uint8_t pin){return pin==21 || pin==14;}
bool ready(){return true;}
bool hold(uint8_t pin,bool enable){assert(pin==45);outputHeld=enable;return true;}
void guarded(){
  assert(current->sleeping_);
  assert(Port::gpioDeepSleep(context,crown,false)==(current->poisoned_?RISC_DEEP_SLEEP_RETAINED:RISC_DEEP_SLEEP_BUSY));
  assert(!Port::gpioWrite(context,backlight,true));
}
bool prepare(){
  guarded();trace.push_back(Prepare);prepared=true;
  if(activateInPrepare)activeWake=true;
  return prepareOk;
}
bool cancel(){
  guarded();trace.push_back(Cancel);
  if(cancelOk)prepared=false;
  return cancelOk;
}
bool arm(uint8_t pin,bool high,bool pullup){
  assert(pin==21 && !high && pullup);guarded();
  assert(!hooks || prepared);trace.push_back(Arm);
  if(failReadAfterArm)readOk=false;
  return armOk;
}
bool clear(uint8_t pin,bool pullup){assert(pin==21 && pullup);guarded();trace.push_back(Clear);return clearOk;}
bool setValid(uint64_t mask,uint64_t high){return mask==((1ULL<<21)|(1ULL<<14)) && high==(1ULL<<14);}
bool setArm(uint64_t mask,uint64_t high,uint64_t pulls){assert(setValid(mask,high) && pulls==(1ULL<<21));return arm(21,false,true);}
bool setClear(uint64_t mask,uint64_t high,uint64_t pulls){assert(setValid(mask,high) && pulls==(1ULL<<21));return clear(21,true);}
bool timer(uint32_t ms){assert(ms==100);guarded();trace.push_back(Timer);return timerOk;}
bool clearTimer(){guarded();trace.push_back(TimerClear);return timerClearOk;}
void terminal(){
  guarded();trace.push_back(Enter);assert(!hooks || prepared);
  // The old native callback returned here when its pause attempt timed out.
  if(returnTerminal)return;
  ++sdkEntries;throw Terminal{};
}
struct Fixture {
  Port port;
  Port::Gpio& gpio;
  bool set, timed;
  static Hardware hardware(){
    Hardware h{};h.owner=owned;h.gpioOpen=open;h.gpioClose=close;h.gpioRead=read;h.gpioWrite=write;
    h.deepWakeValid=valid;h.deepReady=ready;h.deepWakeArm=arm;h.deepWakeClear=clear;h.deepSleep=terminal;h.deepHold=hold;
    h.deepWakeSetValid=setValid;h.deepWakeSetArm=setArm;h.deepWakeSetClear=setClear;h.timerArm=timer;h.timerClear=clearTimer;
    h.deepPrepare=prepare;h.deepCancel=cancel;return h;
  }
  Fixture(bool useSet,bool useTimer):port(hardware()),gpio(port.gpios_[0]),set(useSet),timed(useTimer){
    owner=prepareOk=cancelOk=armOk=timerOk=clearOk=timerClearOk=readOk=hooks=true;
    prepared=activeWake=activateInPrepare=failReadAfterArm=returnTerminal=outputHeld=outputHigh=false;
    sdkEntries=0;trace.clear();current=&port;context=&gpio;gpio.port=&port;
    gpio.input=(1ULL<<21)|(1ULL<<14);gpio.output=1ULL<<45;gpio.pullup=1ULL<<21;
    assert(Port::gpioClaim(context,21,false,false,true,&crown));
    assert(Port::gpioClaim(context,14,false,false,false,&motion));
    assert(Port::gpioClaim(context,45,true,false,false,&backlight));
    if(set)assert(Port::gpioWakeSource(context,motion,true,RISC_WAKE_SET_DEEP)==0);
    assert(Port::gpioDeepSleepHold(context,backlight,true)==0 && outputHeld);
  }
  int run(){
    if(set)return Port::gpioDeepSleepSet(context,crown,false,timed?100:0);
    return timed?Port::gpioDeepSleepFor(context,crown,false,100):Port::gpioDeepSleep(context,crown,false);
  }
  void restore(){
    assert(!prepared && !port.sleeping_ && !port.poisoned_ && !port.sleepRetained_);
    assert(Port::gpioDeepSleepHold(context,backlight,false)==0 && !outputHeld);
    assert(Port::gpioWrite(context,backlight,true) && outputHigh);
  }
  void retained(){
    assert(port.poisoned_ && port.sleepRetained_ && !port.appExitSafe());
    assert(Port::gpioDeepSleepHold(context,backlight,false)==RISC_DEEP_SLEEP_RETAINED && outputHeld);
    assert(!Port::gpioWrite(context,backlight,true) && !outputHigh);
    assert(!Port::gpioRelease(context,crown));
  }
};
unsigned count(Event event){unsigned n=0;for(auto e:trace)if(e==event)++n;return n;}
}

int main(){
  for(bool set:{false,true})for(bool timed:{false,true}){
    // Reproduce the original dark-session hazard with the old callback shape.
    {Fixture f(set,timed);hooks=false;f.port.hw_.deepPrepare=nullptr;f.port.hw_.deepCancel=nullptr;returnTerminal=true;
      assert(f.run()==RISC_DEEP_SLEEP_RETAINED && !sdkEntries);f.retained();
      assert(count(Enter)==1 && count(Prepare)==0 && count(Cancel)==0);
    }
    // A clean prepare refusal cannot reach terminal sleep and permits unblank.
    {Fixture f(set,timed);prepareOk=false;
      assert(f.run()==RISC_DEEP_SLEEP_BUSY && !sdkEntries);
      assert((trace==std::vector<Event>{Prepare,Cancel}));f.restore();
    }
    // Failure of partial-prepare rollback is still a real retention barrier.
    {Fixture f(set,timed);prepareOk=cancelOk=false;
      assert(f.run()==RISC_DEEP_SLEEP_RETAINED && !sdkEntries);
      assert((trace==std::vector<Event>{Prepare,Cancel}));f.retained();
    }
    // Missing half of the transaction fails closed before either callback.
    for(bool missingPrepare:{false,true}){
      Fixture f(set,timed);if(missingPrepare)f.port.hw_.deepPrepare=nullptr;else f.port.hw_.deepCancel=nullptr;
      assert(f.run()==RISC_DEEP_SLEEP_UNSUPPORTED && trace.empty());f.restore();
    }
    // Eligibility/quiet refusals do not pause the native participant.
    {Fixture f(set,timed);activeWake=true;assert(f.run()==RISC_DEEP_SLEEP_ACTIVE_WAKE && trace.empty());f.restore();}
    {Fixture f(set,timed);readOk=false;assert(f.run()==RISC_DEEP_SLEEP_PLATFORM && trace.empty());f.restore();}
    {Fixture f(set,timed);f.port.transferring_=true;assert(f.run()==RISC_DEEP_SLEEP_BUSY && trace.empty());f.port.transferring_=false;f.restore();}
    // Arm failure, an edge during preparation, and the final read failure all
    // cancel preparation after wake-source cleanup, with caller output custody.
    for(unsigned failure=0;failure<3;++failure){
      Fixture f(set,timed);armOk=failure!=0;activateInPrepare=failure==1;failReadAfterArm=failure==2;
      assert(f.run()==(failure==1?RISC_DEEP_SLEEP_ACTIVE_WAKE:RISC_DEEP_SLEEP_PLATFORM));
      assert(count(Prepare)==1 && count(Cancel)==1 && count(Clear)==1 && !count(Enter));
      assert(trace.front()==Prepare && trace.back()==Cancel && !sdkEntries);f.restore();
    }
    if(timed){Fixture f(set,timed);timerOk=false;
      assert(f.run()==RISC_DEEP_SLEEP_PLATFORM && count(Timer)==1 && count(TimerClear)==1);
      assert(trace.back()==Cancel);f.restore();
    }
    // Every rollback is attempted, and one failed stage retains the session.
    for(unsigned failure=0;failure<(timed?3u:2u);++failure){
      Fixture f(set,timed);activateInPrepare=true;
      if(failure==0)cancelOk=false;else if(failure==1)clearOk=false;else timerClearOk=false;
      assert(f.run()==RISC_DEEP_SLEEP_RETAINED && count(Cancel)==1 && count(Clear)==1);
      if(timed)assert(count(TimerClear)==1);
      f.retained();
    }
    // Clean cancellation allows a fresh retry; successful entry is terminal.
    {Fixture f(set,timed);prepareOk=false;assert(f.run()==RISC_DEEP_SLEEP_BUSY);prepareOk=true;trace.clear();
      try{f.run();assert(false);}catch(const Terminal&){}
      assert(sdkEntries==1 && prepared && f.port.sleeping_ && !f.port.poisoned_);
      assert(trace.front()==Prepare && trace.back()==Enter && count(Cancel)==0 && count(Clear)==0 && count(TimerClear)==0);
    }
    // A truly returning terminal backend stays retained and is never resumed.
    {Fixture f(set,timed);returnTerminal=true;
      assert(f.run()==RISC_DEEP_SLEEP_RETAINED && count(Prepare)==1 && count(Enter)==1 && count(Cancel)==0);
      assert(prepared && !sdkEntries);f.retained();
    }
  }
  std::puts("Deep preparation: legacy dark-session reproduction, single/timed/set refusal, every rollback, output restoration, retry and terminal retention PASS");
}
