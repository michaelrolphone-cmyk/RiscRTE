#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <RiscGpioBankV1.h>
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
struct Terminal{};
static uint64_t armed,levels,highMask,pullMask,attempted,cleared;
static unsigned arms,entries,timers,timerClears;static int failArm,failRead;
static bool owner,cleanOk,timerOk,clearTimerOk,entryOk,returnDeep,reassert;
static Port* current;static void* context;static uint64_t anchor;
static bool owned(){return owner;}
static bool open(uint8_t,bool,bool,bool){return true;}static bool close(uint8_t){return true;}
static bool valid(uint8_t pin){return pin<22;}
static bool read(uint8_t pin,bool* out){*out=(levels&(1ULL<<pin))!=0;return int(pin)!=failRead;}
static bool arm(uint8_t pin,bool high){++arms;attempted|=1ULL<<pin;armed|=1ULL<<pin;if(high)highMask|=1ULL<<pin;
 risc_light_sleep_result_v1 out{sizeof(out),99};assert(Port::gpioLightSleepSet(context,anchor,false,0,&out)==RISC_LIGHT_SLEEP_BUSY);
 assert(Port::gpioWakeSource(context,anchor,false,1)==RISC_LIGHT_SLEEP_BUSY);
 if(reassert)levels^=1ULL<<pin;
 return int(pin)!=failArm;}
static bool clear(uint8_t pin){cleared|=1ULL<<pin;armed&=~(1ULL<<pin);return cleanOk;}
static bool setValid(uint64_t mask,uint64_t high){return mask && !(high&~mask) && !(mask>>22);}
static bool setArm(uint64_t mask,uint64_t high,uint64_t pulls){armed=mask;highMask=high;pullMask=pulls;++arms;attempted=mask;if(reassert)levels^=1ULL<<7;return failArm<0;}
static bool setClear(uint64_t mask,uint64_t,uint64_t){cleared=mask;armed=0;return cleanOk;}
static bool ready(){return true;}static bool timer(uint32_t n){assert(n && n<=RISC_TIMED_SLEEP_MAX_MS);++timers;return timerOk;}
static bool timerClear(){++timerClears;return clearTimerOk;}
static bool light(uint32_t* out){++entries;assert(armed==((1ULL<<7)|(1ULL<<14)));assert(highMask==(1ULL<<14));*out=RISC_LIGHT_SLEEP_WAKE_GPIO;return entryOk;}
static void deep(){++entries;assert(armed==((1ULL<<7)|(1ULL<<14)) && pullMask==(1ULL<<7));if(!returnDeep)throw Terminal{};}
struct Fixture{Port p;Port::Gpio &a,&b;uint64_t motion,output;risc_light_sleep_result_v1 out{sizeof(out),99};
 static Hardware hw(){Hardware h{};h.owner=owned;h.gpioOpen=open;h.gpioClose=close;h.gpioRead=read;h.wakeValid=valid;h.wakeArm=arm;h.wakeClear=clear;h.lightSleep=light;h.deepWakeValid=valid;h.deepWakeSetValid=setValid;h.deepWakeSetArm=setArm;h.deepWakeSetClear=setClear;h.deepReady=ready;h.deepSleep=deep;h.timerArm=timer;h.timerClear=timerClear;return h;}
 Fixture():p(hw()),a(p.gpios_[0]),b(p.gpios_[1]){owner=cleanOk=timerOk=clearTimerOk=entryOk=true;returnDeep=reassert=false;failArm=failRead=-1;arms=entries=timers=timerClears=0;armed=highMask=pullMask=attempted=cleared=0;levels=1ULL<<7;current=&p;context=&a;a.port=b.port=&p;a.input=a.pullup=1ULL<<7;b.input=1ULL<<14;b.output=1ULL<<15;assert(Port::gpioClaim(&a,7,false,false,true,&anchor));assert(Port::gpioClaim(&b,14,false,false,false,&motion));assert(Port::gpioClaim(&b,15,true,false,false,&output));}
 void enroll(){assert(Port::gpioWakeSource(&b,motion,true,3)==0);assert(!p.appExitSafe() && !p.restartResourcesSafe() && p.providerStorageSafe());}
 int run(bool d,uint32_t ms=100){return d?Port::gpioDeepSleepSet(&a,anchor,false,ms):Port::gpioLightSleepSet(&a,anchor,false,ms,&out);}
 void release(){assert(Port::gpioWakeSource(&b,motion,true,0)==0);assert(p.appExitSafe());}
};
int main(){
 static_assert(GARDEN_GPIO_DEEP_SLEEP_FOR_V1_SIZE==offsetof(garden_gpio_v1,wake_source));
 static_assert(RISC_GPIO_BANK_DEEP_SLEEP_FOR_V1_SIZE==offsetof(risc_gpio_bank_api_v1,wake_source));
 static_assert(RiscBoot::Runtime::MaxAppPolicyGrants==16);
 {Fixture f;assert(Port::gpioWakeSource(nullptr,f.motion,true,3)==-1);assert(Port::gpioWakeSource(&f.a,f.motion,true,3)==-1);assert(Port::gpioWakeSource(&f.b,f.output,true,3)==-1);assert(Port::gpioWakeSource(&f.b,f.motion,true,4)==-1);owner=false;assert(Port::gpioWakeSource(&f.b,f.motion,true,3)==-2);owner=true;f.enroll();assert(Port::gpioWakeSource(&f.b,f.motion,true,3)==0);assert(Port::gpioWakeSource(&f.b,f.motion,false,3)==-1);assert(!Port::gpioRelease(&f.b,f.motion));f.release();f.release();assert(Port::gpioRelease(&f.b,f.motion));assert(Port::gpioWakeSource(&f.b,f.motion,true,3)==-1);}
 for(bool d:{false,true}){
  {Fixture f;f.enroll();assert(f.run(d,RISC_TIMED_SLEEP_MAX_MS+1)==-1 && !arms);levels|=1ULL<<14;assert(f.run(d)==-4 && !arms);levels=0;assert(f.run(d)==-4 && !arms);levels=1ULL<<7;failRead=14;assert(f.run(d)==-5 && !arms);failRead=-1;f.p.hci_.token=1;assert(f.run(d)==-3 && !arms);f.p.hci_.closing=true;assert(f.run(d)==-6 && !arms);}
  for(unsigned failure=0;failure<4;failure++){Fixture f;f.enroll();if(failure==0)failArm=7;if(failure==1)failArm=14;if(failure==2)timerOk=false;if(failure==3)reassert=true;assert(f.run(d)==(failure==3?-4:-5));assert(!entries && cleared==attempted && !armed);assert(timerClears==(failure>=2));f.release();}
  for(unsigned failures=1;failures<4;failures++){Fixture f;f.enroll();timerOk=false;cleanOk=!(failures&1);clearTimerOk=!(failures&2);assert(f.run(d)==-6);assert(f.p.sleepRetained_ && !f.p.appExitSafe());assert(Port::gpioWakeSource(&f.b,f.motion,true,0)==-6);assert(!Port::gpioRelease(&f.b,f.motion));assert(cleared==attempted && timerClears==1);}
  {Fixture f;f.enroll();f.p.hw_.deepWakeSetValid=[](uint64_t,uint64_t){return false;};if(d)assert(f.run(d)==-7 && !arms);}
 }
 {Fixture f;f.enroll();for(unsigned i=0;i<3;i++){assert(f.run(false)==0 && entries==i+1 && f.out.wake_cause==1);f.release();f.enroll();}assert(!armed);f.release();}
 {Fixture f;f.enroll();entryOk=false;assert(f.run(false,0)==-5 && !timers && !timerClears && cleared==attempted);f.release();}
 {Fixture f;f.enroll();returnDeep=true;assert(f.run(true,0)==-6 && !timers && !timerClears && cleared==attempted);assert(f.p.sleepRetained_);}
 {Fixture f;f.enroll();try{f.run(true);assert(false);}catch(Terminal&){}assert(entries==1 && !cleared && !timerClears && f.p.sleeping_);}
 {Fixture f;f.b.input=(1ULL<<22)-1;for(unsigned i=0;i<8;++i){uint64_t t;assert(Port::gpioClaim(&f.b,i,false,false,false,&t) || i==7);if(i!=7)assert(Port::gpioWakeSource(&f.b,t,false,1)==0);}f.enroll();assert(f.run(false)==-3 && !arms);uint64_t t;assert(Port::gpioClaim(&f.b,8,false,false,false,&t));assert(Port::gpioWakeSource(&f.b,t,false,1)==-3);}
 puts("Owned wake set: ABI, foreign/stale/output claims, bounds, polarity, both inputs/timer, cleanup isolation, retry, exit barriers, radio exclusion and terminal retention PASS");
}
