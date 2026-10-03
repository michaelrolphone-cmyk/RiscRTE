#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
static bool owner=true,level=true,armOk=true,clearOk=true,enterOk=true,reassert=false;
static unsigned arms=0,clears=0,sleeps=0;static int32_t nested=0;
static Port* port;static void* ctx;static uint64_t claim;
static bool owned(){return owner;}
static bool read(uint8_t pin,bool*out){assert(pin==7);*out=level;return true;}
static bool valid(uint8_t pin){return pin==7;}
static bool arm(uint8_t pin,bool high){assert(pin==7 && !high);++arms;if(reassert)level=false;return armOk;}
static bool clear(uint8_t pin){assert(pin==7);++clears;return clearOk;}
static bool enter(uint32_t*out){++sleeps;risc_light_sleep_result_v1 r{sizeof(r),99};nested=Port::gpioLightSleep(ctx,claim,false,&r);*out=RISC_LIGHT_SLEEP_WAKE_GPIO;return enterOk;}
static bool closePin(uint8_t){return true;}
static bool transfer(uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){risc_light_sleep_result_v1 r{sizeof(r),99};assert(Port::gpioLightSleep(ctx,claim,false,&r)==RISC_LIGHT_SLEEP_BUSY);return true;}
int main(){
 Hardware h{};h.owner=owned;h.gpioRead=read;h.gpioClose=closePin;h.wakeValid=valid;h.wakeArm=arm;h.lightSleep=enter;h.wakeClear=clear;h.i2cTransfer=transfer;
 Port p(h);port=&p;auto& g=p.gpios_[0];g.port=&p;ctx=&g;claim=42;p.pins_[7]={&g,claim,false};
 risc_light_sleep_result_v1 out{sizeof(out),99};
 auto sleep=[&](){return Port::gpioLightSleep(ctx,claim,false,&out);};
 assert(!p.quiescent());for(int i=0;i<3;++i){assert(sleep()==0 && out.wake_cause==RISC_LIGHT_SLEEP_WAKE_GPIO);assert(nested==RISC_LIGHT_SLEEP_BUSY);}
 assert(arms==3 && clears==3 && sleeps==3 && p.pins_[7].token==42);
 owner=false;assert(sleep()==RISC_LIGHT_SLEEP_CONTEXT);owner=true;
 assert(Port::gpioLightSleep(ctx,41,false,&out)==RISC_LIGHT_SLEEP_INVALID);
 Port::Gpio foreign{};foreign.port=&p;assert(Port::gpioLightSleep(&foreign,claim,false,&out)==RISC_LIGHT_SLEEP_INVALID);
 p.pins_[7].output=true;assert(sleep()==RISC_LIGHT_SLEEP_INVALID);p.pins_[7].output=false;
 level=false;assert(sleep()==RISC_LIGHT_SLEEP_ACTIVE_WAKE && arms==3);level=true;
 p.spiBuses_[0].held=&p.spis_[0];assert(sleep()==RISC_LIGHT_SLEEP_BUSY);p.spiBuses_[0].held=nullptr;
 auto& bus=p.i2cs_[0];bus.port=&p;bus.token=9;uint8_t byte=0;assert(Port::i2cTransfer(&bus,9,0x34,&byte,1,nullptr,0,10));
 reassert=true;assert(sleep()==RISC_LIGHT_SLEEP_ACTIVE_WAKE && sleeps==3 && clears==4);reassert=false;level=true;
 armOk=false;assert(sleep()==RISC_LIGHT_SLEEP_PLATFORM && clears==5);armOk=true;
 enterOk=false;assert(sleep()==RISC_LIGHT_SLEEP_PLATFORM && clears==6);enterOk=true;
 out.struct_size=0;assert(sleep()==RISC_LIGHT_SLEEP_INVALID);out.struct_size=sizeof(out);
 assert(Port::gpioRelease(ctx,claim));assert(sleep()==RISC_LIGHT_SLEEP_INVALID);p.pins_[7]={&g,43,false};claim=43;
 clearOk=false;assert(sleep()==RISC_LIGHT_SLEEP_RETAINED);assert(p.poisoned_ && p.pins_[7].token==43 && !p.quiescent());
 assert(!Port::gpioRelease(ctx,claim));assert(sleep()==RISC_LIGHT_SLEEP_RETAINED);
 puts("Actual CPU light sleep: repeated wake, owner/input/stale/foreign tokens, active wake, held SPI/reentrant I2C, failed arm/entry, cleanup retention PASS");
}
