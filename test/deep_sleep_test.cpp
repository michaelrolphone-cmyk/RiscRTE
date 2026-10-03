#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <RiscGpioBankV1.h>
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
struct Reset {};
static bool owner=true,level=true,readOk=true,validOk=true,ready=true,armOk=true,clearOk=true;
static bool holdOk=true,unholdOk=true,pwmOk=true,reassert=false,readAfterArm=true,returnEntry=false;
static unsigned arms,clears,entries,holds,unholds;
static bool receivedPullup;
static Port* current;static void* context;static uint64_t inputToken;
static unsigned busCloses;
static uint64_t now(){return 0;}
static bool busClose(uint8_t){++busCloses;return true;}
static bool busEnd(uint8_t,uint8_t,uint32_t){++busCloses;return true;}
static void teardownBlocked(){
 auto& i=current->i2cs_[0];auto& spi=current->spis_[0];auto& b=current->spiBuses_[0];
 assert(!Port::i2cClose(&i,9) && i.token==9);
 assert(!Port::spiRelease(&spi,10) && spi.token==10 && b.refs==1);
 b.held=&spi;assert(!Port::spiEnd(&spi,10) && b.held==&spi);b.held=nullptr;
 assert(busCloses==0);
}
static bool owned(){return owner;}
static bool open(uint8_t,bool,bool,bool){return true;}
static bool write(uint8_t,bool){return true;}
static bool read(uint8_t,bool*out){*out=level;return readOk;}
static bool pwm(uint8_t,uint32_t,uint16_t,uint16_t){return pwmOk;}
static bool close(uint8_t){return true;}
static bool valid(uint8_t pin){return validOk && pin<22;}
static bool idle(){return ready;}
static bool arm(uint8_t pin,bool high,bool pullup){
 assert(pin==7 && high!=level);++arms;receivedPullup=pullup;
 assert(Port::gpioDeepSleep(context,inputToken,high)==RISC_DEEP_SLEEP_BUSY);
 teardownBlocked();
 if(reassert)level=high;
 readOk=readAfterArm;return armOk;
}
static bool clear(uint8_t pin,bool pullup){assert(pin==7 && pullup==receivedPullup);++clears;return clearOk;}
static void enter(){++entries;if(!returnEntry)throw Reset{};}
static bool hold(uint8_t pin,bool enable){
 assert(pin==6);assert(!Port::gpioWrite(context,2,false));teardownBlocked();
 if(enable){++holds;return holdOk;}++unholds;return unholdOk;
}
struct Fixture {
 Hardware h{};Port p;Port::Gpio& g;
 Fixture():p(make()),g(p.gpios_[0]){
   g.port=&p;g.input=uint64_t(1)<<7;g.output=uint64_t(1)<<6;g.pullup=uint64_t(1)<<7;
   context=&g;current=&p;owner=level=readOk=validOk=ready=armOk=clearOk=holdOk=unholdOk=pwmOk=readAfterArm=true;
   reassert=returnEntry=false;arms=clears=entries=holds=unholds=busCloses=0;
   auto& i=p.i2cs_[0];i.port=&p;i.token=9;i.bus.sda=10;i.bus.scl=11;
   auto& sp=p.spis_[0];sp.port=&p;sp.token=10;sp.physical=2;sp.cs=12;sp.bus.sclk=18;sp.bus.mosi=13;sp.bus.miso=-1;
   p.spiBuses_[0].refs=1;
   assert(Port::gpioClaim(context,7,false,false,true,&inputToken));
   uint64_t output;assert(Port::gpioClaim(context,6,true,false,false,&output) && output==2);
 }
 static Hardware make(){Hardware h{};h.owner=owned;h.now=now;h.i2cClose=busClose;h.spiClose=busClose;h.spiEnd=busEnd;h.gpioOpen=open;h.gpioWrite=write;h.gpioRead=read;h.gpioPwm=pwm;h.gpioClose=close;
  h.deepWakeValid=valid;h.deepReady=idle;h.deepWakeArm=arm;h.deepWakeClear=clear;h.deepSleep=enter;h.deepHold=hold;return h;}
 int32_t sleep(){return Port::gpioDeepSleep(context,inputToken,false);}
 int32_t outputHold(bool enable){return Port::gpioDeepSleepHold(context,2,enable);}
};
int main(){
 // The previous binary prefix ends immediately before each additive member.
 static_assert(GARDEN_GPIO_LIGHT_SLEEP_V1_SIZE==offsetof(garden_gpio_v1,deep_sleep));
 static_assert(GARDEN_GPIO_DEEP_SLEEP_V1_SIZE==offsetof(garden_gpio_v1,deep_sleep_hold));
 static_assert(RISC_GPIO_BANK_LIGHT_SLEEP_V1_SIZE==offsetof(risc_gpio_bank_api_v1,deep_sleep));
 static_assert(RISC_GPIO_BANK_DEEP_SLEEP_V1_SIZE==sizeof(risc_gpio_bank_api_v1));
 {Fixture f;assert(!f.p.quiescent());
  assert(Port::gpioDeepSleep(nullptr,1,false)==RISC_DEEP_SLEEP_INVALID);
  owner=false;assert(f.sleep()==RISC_DEEP_SLEEP_CONTEXT);owner=true;
  assert(Port::gpioDeepSleep(context,0,false)==RISC_DEEP_SLEEP_INVALID);
  assert(Port::gpioDeepSleep(context,99,false)==RISC_DEEP_SLEEP_INVALID);
  assert(Port::gpioDeepSleep(context,2,false)==RISC_DEEP_SLEEP_INVALID);
  Port::Gpio foreign{};foreign.port=&f.p;
  assert(Port::gpioDeepSleep(&foreign,inputToken,false)==RISC_DEEP_SLEEP_INVALID);
  validOk=false;assert(f.sleep()==RISC_DEEP_SLEEP_UNSUPPORTED);validOk=true;
  f.p.hw_.deepSleep=nullptr;assert(f.sleep()==RISC_DEEP_SLEEP_UNSUPPORTED);f.p.hw_.deepSleep=enter;
  ready=false;assert(f.sleep()==RISC_DEEP_SLEEP_BUSY);ready=true;
  f.p.transferring_=true;assert(f.sleep()==RISC_DEEP_SLEEP_BUSY);f.p.transferring_=false;
  f.p.spiBuses_[0].held=&f.p.spis_[0];assert(f.sleep()==RISC_DEEP_SLEEP_BUSY);f.p.spiBuses_[0].held=nullptr;
  level=false;assert(f.sleep()==RISC_DEEP_SLEEP_ACTIVE_WAKE);level=true;
  readOk=false;assert(f.sleep()==RISC_DEEP_SLEEP_PLATFORM);readOk=true;
  assert(!arms && !clears && !entries);
  assert(Port::gpioRelease(context,inputToken));assert(f.sleep()==RISC_DEEP_SLEEP_INVALID);
 }
 {Fixture f;
  assert(Port::gpioDeepSleepHold(nullptr,2,true)==RISC_DEEP_SLEEP_INVALID);
  assert(Port::gpioDeepSleepHold(context,inputToken,true)==RISC_DEEP_SLEEP_INVALID);
  assert(Port::gpioDeepSleepHold(context,99,true)==RISC_DEEP_SLEEP_INVALID);
  owner=false;assert(f.outputHold(true)==RISC_DEEP_SLEEP_CONTEXT);owner=true;
  f.p.hw_.deepHold=nullptr;assert(f.outputHold(true)==RISC_DEEP_SLEEP_UNSUPPORTED);f.p.hw_.deepHold=hold;
  assert(Port::gpioPwm(context,2,1000,0,100));assert(f.outputHold(true)==RISC_DEEP_SLEEP_BUSY);assert(f.sleep()==RISC_DEEP_SLEEP_BUSY);
  assert(Port::gpioWrite(context,2,false));assert(f.outputHold(true)==0 && holds==1);
  assert(f.outputHold(true)==0 && holds==1);assert(f.p.pins_[6].held);
  assert(!Port::gpioWrite(context,2,true));assert(!Port::gpioPwm(context,2,1000,1,100));assert(!Port::gpioRelease(context,2));
  assert(f.outputHold(false)==0 && unholds==1);assert(f.outputHold(false)==0 && unholds==1);
  pwmOk=false;assert(!Port::gpioPwm(context,2,1000,0,100));assert(f.outputHold(true)==RISC_DEEP_SLEEP_BUSY);
  assert(Port::gpioWrite(context,2,false));assert(f.outputHold(true)==0);assert(f.outputHold(false)==0);
 }
 for(unsigned failure=0;failure<3;failure++){
  Fixture f;if(failure==0)armOk=false;if(failure==1)reassert=true;if(failure==2)readAfterArm=false;
  const auto expected=failure==1?RISC_DEEP_SLEEP_ACTIVE_WAKE:RISC_DEEP_SLEEP_PLATFORM;
  assert(f.sleep()==expected && arms==1 && clears==1 && entries==0 && !f.p.sleeping_);
  assert(!f.p.poisoned_ && f.p.pins_[7].token==inputToken && receivedPullup);
  assert(Port::gpioWrite(context,2,false));
 }
 {Fixture f;reassert=true;
  for(unsigned n=0;n<3;n++){level=true;assert(f.sleep()==RISC_DEEP_SLEEP_ACTIVE_WAKE);}
  assert(arms==3 && clears==3 && entries==0);
  f.p.pins_[7].pullup=false;level=true;assert(f.sleep()==RISC_DEEP_SLEEP_ACTIVE_WAKE && !receivedPullup);
 }
 {Fixture f;armOk=false;clearOk=false;assert(f.sleep()==RISC_DEEP_SLEEP_RETAINED);
  assert(f.p.poisoned_ && f.p.sleepRetained_ && !f.p.quiescent());teardownBlocked();
  assert(!Port::gpioRelease(context,inputToken) && !Port::gpioRelease(context,2));
  assert(f.sleep()==RISC_DEEP_SLEEP_RETAINED && f.outputHold(true)==RISC_DEEP_SLEEP_RETAINED);
 }
 {Fixture f;holdOk=false;assert(f.outputHold(true)==RISC_DEEP_SLEEP_PLATFORM && holds==1 && unholds==1);
  assert(!f.p.pins_[6].held && !f.p.poisoned_ && Port::gpioWrite(context,2,false));
 }
 for(bool failedEnable:{false,true}){
  Fixture f;if(failedEnable)holdOk=false;else assert(f.outputHold(true)==0);
  unholdOk=false;assert(f.outputHold(failedEnable)==RISC_DEEP_SLEEP_RETAINED);
  assert(f.p.pins_[6].held && f.p.poisoned_ && f.p.sleepRetained_);teardownBlocked();
  assert(!Port::gpioRelease(context,2) && !Port::gpioWrite(context,2,false));
 }
 {Fixture f;returnEntry=true;assert(f.sleep()==RISC_DEEP_SLEEP_RETAINED);
  assert(entries==1 && arms==1 && clears==0 && f.p.poisoned_ && f.p.sleepRetained_);teardownBlocked();
 }
 {Fixture f;assert(f.outputHold(true)==0);bool after=false;
  try{f.sleep();after=true;}catch(const Reset&){}
  assert(!after && entries==1 && arms==1 && clears==0 && f.p.sleeping_ && !f.p.poisoned_);
  assert(f.p.pins_[6].held && f.p.pins_[7].token==inputToken);
 }
 {Fixture f;level=false;reassert=true;
  assert(Port::gpioDeepSleep(context,inputToken,true)==RISC_DEEP_SLEEP_ACTIVE_WAKE && receivedPullup);
 }
 {Fixture f;f.p.poisoned_=true; // Ordinary bus failures still permit clean teardown.
  assert(Port::i2cClose(&f.p.i2cs_[0],9));
  assert(Port::spiRelease(&f.p.spis_[0],10));assert(busCloses==2);
 }
 // A new CPU owner has fresh claim state; no simulated retention of pointers.
 {Fixture fresh;assert(!fresh.p.sleeping_ && !fresh.p.poisoned_ && !fresh.p.pins_[6].held);}
 puts("Actual CPU deep entry: terminal success, static hold, claimed pulls, refusal/partial rollback, DMA readiness, busy/reentrant and retained failures PASS");
}
