#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
namespace {
bool owned=true,openOk=true,readOk=true,physical=false;
unsigned reads=0,mutations=0;
uint8_t lastRead=255;
void (*onRead)()=nullptr;
Port* active=nullptr;
uint64_t oldToken=0;
constexpr uint64_t bit(unsigned pin){return uint64_t(1)<<pin;}
Hardware hardware(){
 Hardware h{};h.owner=[](){return owned;};
 h.gpioOpen=[](uint8_t,bool,bool,bool){++mutations;return openOk;};
 h.gpioWrite=[](uint8_t,bool){++mutations;return true;};
 h.gpioClose=[](uint8_t){++mutations;return true;};
 h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){++mutations;return true;};
 h.deepHold=[](uint8_t,bool){++mutations;return true;};
 h.gpioRead=[](uint8_t pin,bool* level){
   ++reads;lastRead=pin;if(onRead)onRead();*level=physical;return readOk;
 };
 return h;
}
void scopes(Port& p){
 p.gpioCount_=2;
 auto& a=p.gpios_[0];a.port=&p;a.output=bit(0)|bit(14)|bit(48);a.input=bit(6);
 auto& b=p.gpios_[1];b.port=&p;b.output=bit(8);b.input=bit(7);
}
bool same(const Port::Pin& a,const Port::Pin& b){
 return a.owner==b.owner && a.token==b.token && a.output==b.output &&
   a.pullup==b.pullup && a.held==b.held && a.pwm==b.pwm &&
   a.wakeHigh==b.wakeHigh && a.wakeModes==b.wakeModes && a.retiredHeld==b.retiredHeld;
}
void unchangedRead(Port& p,void* context,uint8_t pin,bool expected,bool expectedLevel=false){
 Port::Pin saved[49];for(unsigned i=0;i<49;++i)saved[i]=p.pins_[i];
 const auto serial=p.serial_;const auto beforeMutations=mutations;
 const auto poison=p.poisoned_,sleep=p.sleeping_,retained=p.sleepRetained_,transfer=p.transferring_;
 bool value=true;assert(Port::gpioReadRetiredOutput(context,pin,&value)==expected);
 assert(value==(expected && expectedLevel));assert(mutations==beforeMutations && p.serial_==serial);
 for(unsigned i=0;i<49;++i)assert(same(saved[i],p.pins_[i]));
 assert(p.poisoned_==poison && p.sleeping_==sleep && p.sleepRetained_==retained && p.transferring_==transfer);
}
void rejected(Port& p,void* context,uint8_t pin){
 const auto before=reads;unchangedRead(p,context,pin,false);assert(reads==before);
}
uint64_t claim(Port::Gpio& scope,uint8_t pin,bool output=true){
 uint64_t token=0;assert(Port::gpioClaim(&scope,pin,output,true,false,&token));assert(token);return token;
}
void retired(Port::Gpio& scope,uint64_t token){
 assert(Port::gpioDeepSleepHold(&scope,token,true)==0);
 assert(Port::gpioRetireHeldOutput(&scope,token));
}
void revoked(Port::Gpio& scope,uint64_t token){
 const auto before=reads;const auto mutationBefore=mutations;bool value=false;
 assert(!Port::gpioRead(&scope,token,&value));assert(!Port::gpioWrite(&scope,token,true));
 assert(!Port::gpioPwm(&scope,token,1000,40,100));assert(!Port::gpioRelease(&scope,token));
 assert(Port::gpioDeepSleepHold(&scope,token,false)==
   (scope.port->sleeping_?RISC_DEEP_SLEEP_BUSY:RISC_DEEP_SLEEP_INVALID));
 assert(!Port::gpioRetireHeldOutput(&scope,token));
 assert(reads==before && mutations==mutationBefore);
}
void reentry(){
 auto& p=*active;auto& a=p.gpios_[0];assert(p.sleeping_);
 rejected(p,&a,14);uint64_t token=123;
 assert(!Port::gpioClaim(&a,14,true,false,false,&token) && !token);
 assert(!Port::gpioClaim(&a,0,true,false,false,&token) && !token);
 revoked(a,oldToken);
}
}
int main(){
 Port p(hardware());scopes(p);auto& a=p.gpios_[0];auto& b=p.gpios_[1];
 rejected(p,nullptr,14);rejected(p,&a,49);rejected(p,&a,255);
 rejected(p,&a,14); // An authorized but never-claimed pin is not retired.
 Port::Gpio empty{};rejected(p,&empty,14);
 const auto beforeNull=reads;assert(!Port::gpioReadRetiredOutput(&a,14,nullptr));assert(reads==beforeNull);
 oldToken=claim(a,14);rejected(p,&a,14); // Active static output.
 assert(Port::gpioPwm(&a,oldToken,1000,40,100));rejected(p,&a,14);
 assert(Port::gpioWrite(&a,oldToken,true));
 assert(Port::gpioDeepSleepHold(&a,oldToken,true)==0);rejected(p,&a,14); // Held but not retired.
 assert(Port::gpioRetireHeldOutput(&a,oldToken));revoked(a,oldToken);
 assert(p.quiescent() && p.providerStorageSafe() && p.appExitSafe());
 const auto baselineReads=reads;
 physical=true;unchangedRead(p,&a,14,true,true);assert(reads==baselineReads+1 && lastRead==14);
 physical=false;unchangedRead(p,&a,14,true,false);assert(reads==baselineReads+2 && lastRead==14);
 physical=true;readOk=false;unchangedRead(p,&a,14,false);assert(reads==baselineReads+3);readOk=true;
 unchangedRead(p,&a,14,true,true); // A failure neither caches HIGH nor loses custody.
 rejected(p,&b,14);Port::Gpio copy=a;rejected(p,&copy,14);
 auto held=p.pins_[14];p.pins_[14].owner=&copy;rejected(p,&copy,14);p.pins_[14]=held;
 p.gpios_[2]=a;rejected(p,&p.gpios_[2],14); // Not a registered scope.
 rejected(p,&a,8);rejected(p,&a,13);rejected(p,&a,6);
 // Even an inconsistent custody record cannot widen the output mask.
 p.pins_[8]=held;rejected(p,&a,8);p.pins_[8]={};
 a.output&=~bit(14);rejected(p,&a,14);a.output|=bit(14);
 owned=false;rejected(p,&a,14);owned=true;
 p.hw_.owner=nullptr;rejected(p,&a,14);p.hw_.owner=hardware().owner;
 p.poisoned_=true;rejected(p,&a,14);p.poisoned_=false;
 p.sleeping_=true;rejected(p,&a,14);p.sleeping_=false;
 p.transferring_=true;rejected(p,&a,14);p.transferring_=false;
 p.sleepRetained_=true;rejected(p,&a,14);p.sleepRetained_=false;
 p.hw_.gpioRead=nullptr;rejected(p,&a,14);p.hw_.gpioRead=hardware().gpioRead;
 for(unsigned invalid=0;invalid<7;++invalid){
   p.pins_[14]=held;
   switch(invalid){
     case 0:p.pins_[14].owner=&b;break;
     case 1:p.pins_[14].token=oldToken;break;
     case 2:p.pins_[14].output=false;break;
     case 3:p.pins_[14].held=false;break;
     case 4:p.pins_[14].retiredHeld=false;break;
     case 5:p.pins_[14].pwm=true;break;
     case 6:p.pins_[14].wakeModes=RISC_WAKE_SET_DEEP;break;
   }
   rejected(p,&a,14);
 }
 p.pins_[14]=held;
 active=&p;onRead=reentry;unchangedRead(p,&a,14,true,true);onRead=nullptr;
 assert(!p.sleeping_);revoked(a,oldToken);
 const auto input=claim(a,6,false);rejected(p,&a,6);
 p.pins_[6].wakeModes=RISC_WAKE_SET_LIGHT;rejected(p,&a,6);p.pins_[6].wakeModes=0;
 assert(Port::gpioRelease(&a,input));
 // A fresh same-scope claim ends read authority immediately; the old token stays dead.
 const auto fresh=claim(a,14);assert(fresh!=oldToken);rejected(p,&a,14);revoked(a,oldToken);
 retired(a,fresh);unchangedRead(p,&a,14,true,true);revoked(a,fresh);
 // The bounded pad endpoints work without any token allocation by the read.
 for(const uint8_t pin:{uint8_t(0),uint8_t(48)}){
   const auto token=claim(a,pin);retired(a,token);physical=pin==48;
   unchangedRead(p,&a,pin,true,physical);assert(lastRead==pin);revoked(a,token);
 }
 const auto serial=p.serial_;p.serial_=UINT64_MAX;unchangedRead(p,&a,14,true,physical);p.serial_=serial;
 // Reset creates fresh CPU custody, even if hardware still has a held pad.
 Port reset(hardware());scopes(reset);auto& resetScope=reset.gpios_[0];
 rejected(reset,&resetScope,14);copy=a;copy.port=&reset;rejected(reset,&copy,14);
 // Reset has no retired record until its own claim/hold/retire cycle.
 const auto resetToken=claim(resetScope,14);rejected(reset,&resetScope,14);
 retired(resetScope,resetToken);unchangedRead(reset,&resetScope,14,true,physical);
 // Failed reclaim preserves custody, poisons the port, and disables readback.
 openOk=false;uint64_t failed=123;
 assert(!Port::gpioClaim(&a,14,true,false,false,&failed) && !failed);openOk=true;
 assert(p.poisoned_ && same(held,p.pins_[14]));rejected(p,&a,14);
 puts("Retired output readback: physical HIGH/LOW/failure, exact scope, revoked tokens, lifecycle gates, reentry, reclaim, reset and no mutation PASS");
}
