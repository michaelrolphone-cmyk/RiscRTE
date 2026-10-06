#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
static bool owner=true,radio=true,hci=true,hciSafe=true,http=true,maintenance=true,proof=true;
static unsigned checks=0,starts=0;
int main(){
 Hardware h{};h.owner=[](){return owner;};h.radioIdle=[](){return radio;};h.hciIdle=[](){return hci;};h.hciSafe=[](){return hciSafe;};
 h.httpIdle=[](){return http;};h.httpSafe=[](){return http;};h.maintenanceIdle=[](){return maintenance;};
 h.radioIqReady=[](){++checks;return proof;};
 h.radioJoin=[](const char*,const char*){++starts;return true;};h.radioScanStart=[](){++starts;return true;};h.hciOpen=[](){++starts;return true;};
 Port p(h);auto& c=p.iq_;c.port=&p;auto& r=p.radios_[0];r.port=&p;r.token=10;auto& b=p.hci_;b.port=&p;
 auto refused=[&](){uint64_t t=99;assert(!Port::radioIqClaim(&c,&t) && !t && !c.token);};
 assert(!Port::radioIqClaim(nullptr,nullptr));
 for(bool* state:{&owner,&radio,&hci,&hciSafe,&http,&maintenance}){*state=false;refused();*state=true;}
 for(bool* state:{&p.poisoned_,&p.sleeping_,&p.sleepRetained_,&p.transferring_,&r.active,&r.closing,&b.closing}){*state=true;refused();*state=false;}
 b.token=4;refused();b.token=0;p.i2ss_[0].token=3;refused();p.i2ss_[0].token=0;
 p.pins_[2].held=true;refused();p.pins_[2].held=false;p.pins_[2].wakeModes=1;refused();p.pins_[2].wakeModes=0;
 p.spiBuses_[0].held=&p.spis_[0];refused();p.spiBuses_[0].held=nullptr;
 proof=false;refused();proof=true;
 uint64_t t=0;assert(Port::radioIqClaim(&c,&t) && t && c.token==t);const auto first=t;
 assert(!p.appExitSafe() && !p.providerStorageSafe() && !p.restartResourcesSafe() && !p.quiescent());
 uint64_t duplicate=99;assert(!Port::radioIqClaim(&c,&duplicate) && !duplicate);
 assert(!Port::radioJoin(&r,10,"test","test-pass") && !Port::radioScanStart(&r,10));
 assert(!Port::hciOpen(&b,0,&duplicate) && !duplicate && !starts);
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
 owner=false;assert(!Port::radioIqRelease(&c,t));owner=true;
 assert(!Port::radioIqRelease(&c,t+1) && c.token==t && !c.closing);
 proof=false;assert(!Port::radioIqRelease(&c,t) && c.token==t && c.closing);
 sleep(RISC_LIGHT_SLEEP_RETAINED);
 assert(!p.appExitSafe() && !p.providerStorageSafe() && !p.restartResourcesSafe());
 assert(!Port::radioJoin(&r,10,"test","test-pass") && !Port::radioScanStart(&r,10));
 proof=true;assert(Port::radioIqRelease(&c,t) && !c.token && !c.closing);
 assert(!Port::radioIqRelease(&c,t));assert(p.appExitSafe() && p.restartResourcesSafe());
 // Logical Wi-Fi owner survived; another capture receives a fresh generation.
 assert(r.token==10 && Port::radioIqClaim(&c,&t) && t>first);assert(Port::radioIqRelease(&c,t));
 p.serial_=UINT64_MAX;refused();assert(!starts && checks>=5);
 puts("IQ resource: fail-closed proof, owner/context, idle station coexistence, modem exclusion, exit/restart/all sleep forms, retained cleanup retry PASS");
}
