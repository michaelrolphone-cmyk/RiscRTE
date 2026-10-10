#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
static bool owner=true,idle=true,suspendOk=true,resumeOk=true,partial=false;
static unsigned suspends,resumes,transfers;
int main(){
 Hardware h{};h.owner=[](){return owner;};h.now=[]()->uint64_t{return 0;};h.usbPhyIdle=[](){return idle;};
 h.usbPhySuspend=[](){++suspends;if(suspendOk||partial)idle=false;return suspendOk;};
 h.usbPhyResume=[](){++resumes;if(resumeOk)idle=true;return resumeOk;};
 h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){++transfers;return true;};
 Port p(h);auto& c=p.usb_;c.port=&p;
 auto refused=[&](){auto calls=suspends;auto held=c.token;uint64_t token=99;assert(!Port::usbPhyClaim(&c,&token)&&!token&&c.token==held&&calls==suspends);};
 assert(!Port::usbPhyClaim(nullptr,nullptr));owner=false;refused();owner=true;
 for(bool* flag:{&p.poisoned_,&p.sleeping_,&p.sleepRetained_,&p.transferring_}){*flag=true;refused();*flag=false;}
 idle=false;refused();idle=true;
 p.pins_[19].owner=&p;refused();p.pins_[19]={};p.pins_[20].owner=&p;refused();p.pins_[20]={};
 p.serial_=UINT64_MAX;refused();p.serial_=0;
 suspendOk=false;uint64_t token=99;assert(!Port::usbPhyClaim(&c,&token)&&!token&&!c.token&&idle);
 suspendOk=true;assert(Port::usbPhyClaim(&c,&token)&&token&&!idle);const auto first=token;
 assert(p.pins_[19].owner==&c&&p.pins_[20].owner==&c);
 assert(!p.appExitSafe()&&!p.restartResourcesSafe()&&!p.quiescent()&&p.providerStorageSafe());
 refused(); // Existing lease is not overwritten or handed to a second caller.
 auto& g=p.gpios_[0];g.port=&p;p.pins_[4]={&g,90,false};risc_light_sleep_result_v1 wake{sizeof(wake),0};
 auto sleep=[&](int32_t expected){
  assert(Port::gpioLightSleep(&g,90,false,&wake)==expected);
  assert(Port::gpioDeepSleep(&g,90,false)==expected);
  assert(Port::gpioLightSleepFor(&g,90,false,10,&wake)==expected);
  assert(Port::gpioDeepSleepFor(&g,90,false,10)==expected);
  assert(Port::gpioLightSleepSet(&g,90,false,10,&wake)==expected);
  assert(Port::gpioDeepSleepSet(&g,90,false,10)==expected);
  assert(Port::gpioDeepSleepHold(&g,90,true)==expected);
 };
 sleep(RISC_LIGHT_SLEEP_BUSY);
 // A live USB lease must leave the existing owner's SD/SPI transfer functional.
 auto& spi=p.spis_[0];spi.port=&p;spi.token=77;spi.physical=2;spi.ready=true;spi.deadline=10;
 p.spiBuses_[0].held=&spi;uint8_t data=0;
 assert(Port::spiTransfer(&spi,77,&data,&data,1)&&transfers==1);p.spiBuses_[0].held=nullptr;
 auto count=resumes;owner=false;assert(!Port::usbPhyRelease(&c,token));owner=true;
 assert(!Port::usbPhyRelease(&c,token+1)&&resumes==count&&c.token==token);
 resumeOk=false;assert(!Port::usbPhyRelease(&c,token)&&c.closing&&c.token==token);
 sleep(RISC_LIGHT_SLEEP_RETAINED);assert(p.providerStorageSafe());
 resumeOk=true;assert(Port::usbPhyRelease(&c,token)&&!c.token&&!c.closing&&idle);
 assert(!p.pins_[19].owner&&!p.pins_[20].owner&&!Port::usbPhyRelease(&c,token));
 assert(p.appExitSafe()&&p.restartResourcesSafe());
 suspendOk=false;partial=true;token=0;
 assert(!Port::usbPhyClaim(&c,&token)&&token>first&&c.closing&&!idle);
 sleep(RISC_LIGHT_SLEEP_RETAINED);assert(p.providerStorageSafe());
 assert(Port::usbPhyRelease(&c,token)&&!c.token);
 puts("USB PHY: owner/generation/pad exclusion, partial claim, retained release retry, all sleep forms and SD transfer coexistence PASS");
}
