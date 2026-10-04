#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <RiscGpioBankV1.h>
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
struct Terminal {};
static bool owner,level,readOk,armOk,timerOk,clearOk,timerClearOk,entryOk,reassert,readAfterArm,returnDeep;
static bool crownArmed,timerArmed,expectTimed;
static unsigned arms,timers,clears,timerClears,entries;
static uint32_t lastDuration,wakeCause;
static Port* current;static void* ctx;static uint64_t token;
static bool owned(){return owner;}
static bool open(uint8_t,bool,bool,bool){return true;}
static bool close(uint8_t){return true;}
static bool write(uint8_t,bool){return true;}
static bool read(uint8_t,bool*out){*out=level;return readOk;}
static bool valid(uint8_t pin){return pin==7;}
static bool ready(){return true;}
static bool arm(uint8_t pin,bool high){
 assert(pin==7 && level!=high);++arms;crownArmed=true;
 risc_light_sleep_result_v1 r{sizeof(r),99};
 assert(Port::gpioLightSleepFor(ctx,token,high,10,&r)==RISC_LIGHT_SLEEP_BUSY);
 assert(Port::gpioDeepSleepFor(ctx,token,high,10)==RISC_DEEP_SLEEP_BUSY);
 if(reassert)level=high;
 readOk=readAfterArm;return armOk;
}
static bool deepArm(uint8_t pin,bool high,bool pullup){assert(pullup);return arm(pin,high);}
static bool timer(uint32_t ms){++timers;lastDuration=ms;timerArmed=true;return timerOk;}
static bool clear(uint8_t pin){assert(pin==7);++clears;if(clearOk)crownArmed=false;return clearOk;}
static bool deepClear(uint8_t pin,bool pullup){assert(pullup);return clear(pin);}
static bool clearTimer(){assert(current->sleeping_);assert(!Port::gpioRelease(ctx,token));++timerClears;if(timerClearOk)timerArmed=false;return timerClearOk;}
static bool light(uint32_t*out){++entries;assert(crownArmed && timerArmed==expectTimed);*out=wakeCause;return entryOk;}
static void deep(){++entries;assert(crownArmed && timerArmed==expectTimed);if(!returnDeep)throw Terminal{};}
static bool transfer(uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){
 risc_light_sleep_result_v1 out{sizeof(out),99};
 assert(Port::gpioLightSleepFor(ctx,token,false,1,&out)==RISC_LIGHT_SLEEP_BUSY);
 assert(Port::gpioDeepSleepFor(ctx,token,false,1)==RISC_DEEP_SLEEP_BUSY);return true;
}
struct Fixture{
 Port p;Port::Gpio& g;risc_light_sleep_result_v1 out{sizeof(out),99};
 Fixture():p(make()),g(p.gpios_[0]){
  owner=level=readOk=armOk=timerOk=clearOk=timerClearOk=entryOk=readAfterArm=true;
  reassert=returnDeep=crownArmed=timerArmed=false;expectTimed=true;
  arms=timers=clears=timerClears=entries=lastDuration=0;wakeCause=RISC_LIGHT_SLEEP_WAKE_TIMER;
  current=&p;ctx=&g;g.port=&p;g.input=1ULL<<7;g.output=1ULL<<6;g.pullup=1ULL<<7;
  assert(Port::gpioClaim(ctx,7,false,false,true,&token));
  uint64_t output;assert(Port::gpioClaim(ctx,6,true,false,false,&output) && output==2);
 }
 static Hardware make(){Hardware h{};h.owner=owned;h.gpioOpen=open;h.gpioClose=close;h.gpioWrite=write;h.gpioRead=read;h.i2cTransfer=transfer;
  h.wakeValid=valid;h.wakeArm=arm;h.wakeClear=clear;h.lightSleep=light;h.deepWakeValid=valid;h.deepReady=ready;
  h.deepWakeArm=deepArm;h.deepWakeClear=deepClear;h.deepSleep=deep;h.timerArm=timer;h.timerClear=clearTimer;return h;}
 int32_t sleep(bool deepMode,uint32_t ms=123){return deepMode?Port::gpioDeepSleepFor(ctx,token,false,ms):Port::gpioLightSleepFor(ctx,token,false,ms,&out);}
 void retained(){assert(p.poisoned_ && p.sleepRetained_ && !p.appExitSafe());assert(!Port::gpioRelease(ctx,token));assert(!Port::gpioWrite(ctx,2,false));}
};
// A new consumer must check the whole optional member before dereferencing it.
static bool timedLight(const garden_gpio_v1* p){return p && p->api_version==1 && p->struct_size>=GARDEN_GPIO_LIGHT_SLEEP_FOR_V1_SIZE && p->light_sleep_for;}
static bool timedDeep(const garden_gpio_v1* p){return p && p->api_version==1 && p->struct_size>=GARDEN_GPIO_DEEP_SLEEP_FOR_V1_SIZE && p->deep_sleep_for;}
int main(){
 static_assert(GARDEN_GPIO_LIGHT_SLEEP_V1_SIZE==offsetof(garden_gpio_v1,deep_sleep));
 static_assert(GARDEN_GPIO_DEEP_SLEEP_V1_SIZE==offsetof(garden_gpio_v1,deep_sleep_hold));
 static_assert(GARDEN_GPIO_DEEP_SLEEP_HOLD_V1_SIZE==offsetof(garden_gpio_v1,light_sleep_for));
 static_assert(GARDEN_GPIO_LIGHT_SLEEP_FOR_V1_SIZE==offsetof(garden_gpio_v1,deep_sleep_for));
 static_assert(RISC_GPIO_BANK_DEEP_SLEEP_V1_SIZE==offsetof(risc_gpio_bank_api_v1,light_sleep_for));
 static_assert(RISC_GPIO_BANK_LIGHT_SLEEP_FOR_V1_SIZE==offsetof(risc_gpio_bank_api_v1,deep_sleep_for));
 {alignas(garden_gpio_v1) unsigned char old[GARDEN_GPIO_DEEP_SLEEP_HOLD_V1_SIZE]{};
  auto* p=reinterpret_cast<garden_gpio_v1*>(old);p->api_version=1;p->struct_size=sizeof(old);
  assert(!timedLight(p) && !timedDeep(p));}
 {garden_gpio_v1 p{};p.api_version=1;p.struct_size=sizeof(p);assert(!timedLight(&p) && !timedDeep(&p));
  p.light_sleep_for=Port::gpioLightSleepFor;p.deep_sleep_for=Port::gpioDeepSleepFor;
  p.struct_size=GARDEN_GPIO_LIGHT_SLEEP_FOR_V1_SIZE;assert(timedLight(&p) && !timedDeep(&p));
  p.struct_size=sizeof(p);assert(timedDeep(&p));}
 for(bool d:{false,true}){
  {Fixture f;for(uint32_t ms:{0u,RISC_TIMED_SLEEP_MAX_MS+1,UINT32_MAX})assert(f.sleep(d,ms)==RISC_LIGHT_SLEEP_INVALID);
   assert(!arms && !timers && !entries && !clears && !timerClears);}
  {Fixture f;owner=false;assert(f.sleep(d)==RISC_LIGHT_SLEEP_CONTEXT);owner=true;
   auto call=[&](void* c,uint64_t t){return d?Port::gpioDeepSleepFor(c,t,false,1):Port::gpioLightSleepFor(c,t,false,1,&f.out);};
   assert(call(nullptr,token)==RISC_LIGHT_SLEEP_INVALID);assert(call(ctx,0)==RISC_LIGHT_SLEEP_INVALID);
   assert(call(ctx,99)==RISC_LIGHT_SLEEP_INVALID);assert(call(ctx,2)==RISC_LIGHT_SLEEP_INVALID);
   Port::Gpio foreign{};foreign.port=&f.p;assert(call(&foreign,token)==RISC_LIGHT_SLEEP_INVALID);
   f.p.transferring_=true;assert(f.sleep(d)==RISC_LIGHT_SLEEP_BUSY);f.p.transferring_=false;
   f.p.spiBuses_[0].held=&f.p.spis_[0];assert(f.sleep(d)==RISC_LIGHT_SLEEP_BUSY);f.p.spiBuses_[0].held=nullptr;
   f.p.hw_.timerArm=nullptr;assert(f.sleep(d)==RISC_LIGHT_SLEEP_UNSUPPORTED);f.p.hw_.timerArm=timer;
   f.p.hw_.timerClear=nullptr;assert(f.sleep(d)==RISC_LIGHT_SLEEP_UNSUPPORTED);f.p.hw_.timerClear=clearTimer;
   level=false;assert(f.sleep(d)==RISC_LIGHT_SLEEP_ACTIVE_WAKE);level=true;
   readOk=false;assert(f.sleep(d)==RISC_LIGHT_SLEEP_PLATFORM);readOk=true;
   auto& bus=f.p.i2cs_[0];bus.port=&f.p;bus.token=9;uint8_t byte=0;
   assert(Port::i2cTransfer(&bus,9,0x34,&byte,1,nullptr,0,10));
   assert(!arms && !clears && !timerClears);
   assert(Port::gpioRelease(ctx,token));assert(f.sleep(d)==RISC_LIGHT_SLEEP_INVALID);}
  for(unsigned failure=0;failure<4;++failure){Fixture f;
   if(failure==0)armOk=false;
   if(failure==1)timerOk=false;
   if(failure==2)reassert=true;
   if(failure==3)readAfterArm=false;
   assert(f.sleep(d)==(failure==2?RISC_LIGHT_SLEEP_ACTIVE_WAKE:RISC_LIGHT_SLEEP_PLATFORM));
   assert(arms==1 && timers==(failure?1u:0u) && clears==1 && timerClears==1 && !entries);
   assert(!timerArmed && !crownArmed && f.p.appExitSafe());}
  for(unsigned failedClear=1;failedClear<4;++failedClear)for(unsigned failure=0;failure<3;++failure){Fixture f;
   if(failure==0)armOk=false;
   if(failure==1)timerOk=false;
   if(failure==2)reassert=true;
   timerClearOk=!(failedClear&1);clearOk=!(failedClear&2);
   assert(f.sleep(d)==RISC_LIGHT_SLEEP_RETAINED && clears==1 && timerClears==1);f.retained();}
  {Fixture f;reassert=true;assert(f.sleep(d)==RISC_LIGHT_SLEEP_ACTIVE_WAKE);reassert=false;level=true;expectTimed=false;
   if(d){try{Port::gpioDeepSleep(ctx,token,false);assert(false);}catch(Terminal&){};}
   else assert(Port::gpioLightSleep(ctx,token,false,&f.out)==0 && f.out.wake_cause==RISC_LIGHT_SLEEP_WAKE_OTHER);
   assert(timers==1 && timerClears==1 && !timerArmed);}
 }
 {Fixture f;assert(Port::gpioLightSleepFor(ctx,token,false,1,nullptr)==RISC_LIGHT_SLEEP_INVALID);
  f.out.struct_size=0;assert(f.sleep(false)==RISC_LIGHT_SLEEP_INVALID && !arms);}
 {Fixture f;f.p.hw_.deepReady=[](){return false;};assert(f.sleep(true)==RISC_DEEP_SLEEP_BUSY && !arms);}
 {Fixture f;f.p.pins_[6].pwm=true;assert(f.sleep(true)==RISC_DEEP_SLEEP_BUSY && !arms);}
 for(bool d:{false,true}){Fixture f;level=false;reassert=true;
  const int32_t result=d?Port::gpioDeepSleepFor(ctx,token,true,1):Port::gpioLightSleepFor(ctx,token,true,1,&f.out);
  assert(result==RISC_LIGHT_SLEEP_ACTIVE_WAKE && clears==1 && timerClears==1 && !timerArmed && !crownArmed);}
 {Fixture f;assert(f.sleep(false)==0 && !timerArmed && !crownArmed);expectTimed=false;
  assert(Port::gpioLightSleep(ctx,token,false,&f.out)==0 && f.out.wake_cause==RISC_LIGHT_SLEEP_WAKE_OTHER);
  assert(timers==1 && timerClears==1 && clears==2 && !timerArmed && !crownArmed);}
 for(unsigned failure=1;failure<4;++failure){Fixture f;timerClearOk=!(failure&1);clearOk=!(failure&2);
  assert(f.sleep(false)==RISC_LIGHT_SLEEP_RETAINED && entries==1 && clears==1 && timerClears==1);f.retained();}
 for(uint32_t ms:{1u,RISC_TIMED_SLEEP_MAX_MS}){Fixture f;assert(f.sleep(false,ms)==0 && lastDuration==ms && f.out.wake_cause==RISC_LIGHT_SLEEP_WAKE_TIMER);
  assert(!timerArmed && !crownArmed && f.p.appExitSafe());}
 {Fixture f;wakeCause=RISC_LIGHT_SLEEP_WAKE_GPIO;assert(f.sleep(false)==0 && f.out.wake_cause==RISC_LIGHT_SLEEP_WAKE_GPIO);}
 {Fixture f;entryOk=false;assert(f.sleep(false,1)==RISC_LIGHT_SLEEP_PLATFORM && f.out.wake_cause==RISC_LIGHT_SLEEP_WAKE_NONE);
  assert(clears==1 && timerClears==1 && !timerArmed && !crownArmed && f.p.appExitSafe());}
 for(unsigned failedClear=0;failedClear<4;++failedClear){Fixture f;returnDeep=true;timerClearOk=!(failedClear&1);clearOk=!(failedClear&2);
  assert(f.sleep(true)==RISC_DEEP_SLEEP_RETAINED && entries==1 && clears==1 && timerClears==1);f.retained();}
 {Fixture f;try{f.sleep(true,RISC_TIMED_SLEEP_MAX_MS);assert(false);}catch(Terminal&){}
  assert(entries==1 && timers==1 && lastDuration==RISC_TIMED_SLEEP_MAX_MS && !clears && !timerClears && f.p.sleeping_);}
 puts("Actual CPU timed sleep: old ABI, absent suffix, bounds, owned input, busy/reentrant, independent partial cleanup, stale timer exclusion and retained/terminal semantics PASS");
}
