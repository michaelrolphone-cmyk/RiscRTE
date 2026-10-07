#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
static bool owned=true,writeOk=true,closeOk=true,pwmOk=true;
static unsigned writes=0;static uint8_t lastPin=0;static bool lastLevel=false;
static Hardware hardware(){
 Hardware h{};h.owner=[](){return owned;};
 h.gpioOpen=[](uint8_t,bool,bool,bool){return true;};
 h.gpioWrite=[](uint8_t pin,bool level){++writes;lastPin=pin;lastLevel=level;return writeOk;};
 h.gpioClose=[](uint8_t){return closeOk;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){return pwmOk;};
 h.deepHold=[](uint8_t,bool){return true;};return h;
}
int main(){
 Port p(hardware());auto& a=p.gpios_[0];auto& b=p.gpios_[1];
 a.port=b.port=&p;a.output=b.output=a.input=b.input=(uint64_t(1)<<49)-1;
 auto claim=[&](Port::Gpio& c,uint8_t pin,bool output=true){uint64_t t=0;assert(Port::gpioClaim(&c,pin,output,false,false,&t));assert(t);return t;};
 auto reject=[&](Port::Gpio& c,uint64_t t){auto before=writes;assert(!Port::gpioWrite(&c,t,true));assert(writes==before);};
 auto write=[&](Port::Gpio& c,uint64_t t,uint8_t pin){assert(Port::gpioWrite(&c,t,true));assert(lastPin==pin && lastLevel);};
 // Exact full tokens, including high bits, are required on a cache hit.
 auto first=claim(a,12);write(a,first,12);assert(p.gpioWritePins_[first&63]==13);
 reject(b,first);reject(a,0);reject(a,first+(uint64_t(1)<<32));reject(a,UINT64_MAX);
 owned=false;reject(a,first);owned=true;p.poisoned_=true;reject(a,first);p.poisoned_=false;p.sleeping_=true;reject(a,first);p.sleeping_=false;
 // Force two live, same-scope tokens into the same bucket and alternate them.
 while(p.serial_<first+63)assert(p.token());
 auto collision=claim(a,48);assert((collision&63)==(first&63));
 for(unsigned i=0;i<20;++i){write(a,collision,48);write(a,first,12);}
 // Foreign-scope collision must not use the hint belonging to this owner.
 assert(Port::gpioRelease(&a,collision));p.serial_=first+127;
 auto foreign=claim(b,48);assert((foreign&63)==(first&63));write(b,foreign,48);reject(a,foreign);write(a,first,12);
 // Failed release preserves authority; successful release revokes cached token.
 closeOk=false;assert(!Port::gpioRelease(&a,first));write(a,first,12);closeOk=true;
 assert(Port::gpioRelease(&a,first));reject(a,first);auto fresh=claim(a,12);assert(fresh!=first);reject(a,first);write(a,fresh,12);
 // Reacquiring the same pad as input cannot reuse its former output authority.
 assert(Port::gpioRelease(&a,fresh));auto input=claim(a,12,false);reject(a,fresh);reject(a,input);assert(Port::gpioRelease(&a,input));
 fresh=claim(a,12);write(a,fresh,12);
 // Successful/failed PWM and failed static output preserve the original barrier.
 pwmOk=false;assert(!Port::gpioPwm(&a,fresh,1000,40,100));pwmOk=true;assert(p.pins_[12].pwm);
 writeOk=false;assert(!Port::gpioWrite(&a,fresh,false));assert(p.pins_[12].pwm);writeOk=true;
 assert(Port::gpioDeepSleepHold(&a,fresh,true)==RISC_DEEP_SLEEP_BUSY);write(a,fresh,12);assert(!p.pins_[12].pwm);
 assert(Port::gpioDeepSleepHold(&a,fresh,true)==0);reject(a,fresh);assert(!Port::gpioRelease(&a,fresh));
 assert(Port::gpioRetireHeldOutput(&a,fresh));reject(a,fresh);auto reclaimed=claim(a,12);assert(reclaimed!=fresh);reject(a,fresh);write(a,reclaimed,12);
 assert(Port::gpioRelease(&a,reclaimed));assert(Port::gpioRelease(&b,foreign));
 // Exercise every pin/cache entry, plus high-generation and exhaustion edges.
 for(uint8_t pin=0;pin<49;++pin){auto t=claim(a,pin);write(a,t,pin);assert(Port::gpioRelease(&a,t));reject(a,t);}
 p.serial_=(uint64_t(1)<<40);auto high=claim(a,0);write(a,high,0);reject(a,high&UINT32_MAX);assert(Port::gpioRelease(&a,high));
 p.serial_=UINT64_MAX-1;auto last=claim(a,0);assert(last==UINT64_MAX);write(a,last,0);assert(Port::gpioRelease(&a,last));
 uint64_t exhausted=123;assert(!Port::gpioClaim(&a,0,true,false,false,&exhausted));assert(!exhausted);reject(a,last);
 puts("Scoped GPIO write cache: collisions, full tokens, foreign/stale/input/held/retired/owner rejection, reacquire, PWM failure and exhaustion PASS");
}
