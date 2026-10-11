#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
static bool owned=true,readOk=true,closeOk=true;
static unsigned reads;static uint8_t lastPin;
static Hardware hardware(){
 Hardware h{};h.owner=[](){return owned;};h.gpioOpen=[](uint8_t,bool,bool,bool){return true;};
 h.gpioWrite=[](uint8_t,bool){return true;};h.gpioClose=[](uint8_t){return closeOk;};
 h.gpioRead=[](uint8_t pin,bool* out){++reads;lastPin=pin;*out=(pin&1)!=0;return readOk;};
 h.deepHold=[](uint8_t,bool){return true;};return h;
}
int main(){
 Port p(hardware());auto&a=p.gpios_[0];auto&b=p.gpios_[1];a.port=b.port=&p;
 a.input=a.output=b.input=b.output=(uint64_t(1)<<49)-1;
 auto claim=[&](Port::Gpio&g,uint8_t pin,bool output=false){uint64_t t=0;assert(Port::gpioClaim(&g,pin,output,true,false,&t));return t;};
 auto read=[&](Port::Gpio&g,uint64_t t,uint8_t pin){bool v=false;assert(Port::gpioRead(&g,t,&v));assert(lastPin==pin&&v==bool(pin&1));};
 auto reject=[&](Port::Gpio&g,uint64_t t){unsigned n=reads;bool v=false;assert(!Port::gpioRead(&g,t,&v));assert(n==reads);};
 auto first=claim(a,40);read(a,first,40);assert(p.gpioWritePins_[first&63]==41);
 reject(b,first);reject(a,0);reject(a,first+(uint64_t(1)<<32));reject(a,UINT64_MAX);
 unsigned n=reads;assert(!Port::gpioRead(&a,first,nullptr));assert(reads==n);
 owned=false;reject(a,first);owned=true;p.poisoned_=true;reject(a,first);p.poisoned_=false;
 p.sleeping_=true;reject(a,first);p.sleeping_=false;
 p.serial_=first+63;auto collision=claim(a,41);
 for(unsigned i=0;i<100;++i){read(a,collision,41);read(a,first,40);}
 assert(Port::gpioRelease(&a,collision));p.serial_=first+127;auto foreign=claim(b,41);
 read(b,foreign,41);reject(a,foreign);read(a,first,40);
 closeOk=false;assert(!Port::gpioRelease(&a,first));read(a,first,40);closeOk=true;
 assert(Port::gpioRelease(&a,first));reject(a,first);auto next=claim(a,40,true);reject(a,first);read(a,next,40);
 readOk=false;bool v=false;n=reads;assert(!Port::gpioRead(&a,next,&v));assert(reads==n+1);readOk=true;
 assert(Port::gpioDeepSleepHold(&a,next,true)==0);read(a,next,40); // Reading a held output is valid.
 assert(Port::gpioRetireHeldOutput(&a,next));reject(a,next);auto reclaimed=claim(a,40,true);reject(a,next);read(a,reclaimed,40);
 assert(Port::gpioRelease(&a,reclaimed));assert(Port::gpioRelease(&b,foreign));
 for(uint8_t pin=0;pin<49;++pin){auto t=claim(a,pin);read(a,t,pin);assert(Port::gpioRelease(&a,t));reject(a,t);}
 p.serial_=uint64_t(1)<<40;auto high=claim(a,42);read(a,high,42);reject(a,high&UINT32_MAX);assert(Port::gpioRelease(&a,high));
 p.serial_=UINT64_MAX-1;auto last=claim(a,42);read(a,last,42);assert(Port::gpioRelease(&a,last));reject(a,last);
 uint64_t exhausted=3;assert(!Port::gpioClaim(&a,42,false,false,false,&exhausted));assert(!exhausted);
 puts("GPIO read: scope/full-token collisions, invalidation, retained release, held/retired pads, hardware refusal and exhaustion PASS");
}
