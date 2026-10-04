#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
static bool owned=true,openOk=true,closeOk=true,writeOk=true;
static size_t accepted=256;static unsigned opens=0,writes=0,closes=0;
int main(){
 Hardware h{};h.owner=[](){return owned;};
 h.i2sOpen=[](uint8_t unit,uint8_t clock,uint8_t word,uint8_t data,uint32_t rate){assert(unit==1 && clock==7 && word==8 && data==9 && rate==8000);++opens;return openOk;};
 h.i2sWrite=[](uint8_t unit,const int16_t*,size_t frames,size_t* done,uint32_t ms){assert(unit==1 && frames==256 && ms==40);++writes;*done=accepted;return writeOk;};
 h.i2sClose=[](uint8_t unit){assert(unit==1);++closes;return closeOk;};
 Port p(h);auto& c=p.i2ss_[0];c.port=&p;c.config={sizeof(c.config),1,0,7,8,9,0};
 auto open=[&](uint64_t* t){return Port::i2sOpen(&c,1,false,7,8,9,8000,2,t);};
 uint64_t token=55;
 assert(!Port::i2sOpen(&c,0,false,7,8,9,8000,2,&token) && !token);
 assert(!Port::i2sOpen(&c,1,true,7,8,9,8000,2,&token));
 assert(!Port::i2sOpen(&c,1,false,6,8,9,8000,2,&token));
 assert(!Port::i2sOpen(&c,1,false,7,8,9,8000,1,&token));
 assert(!Port::i2sOpen(&c,1,false,7,8,9,48000,2,&token));
 owned=false;assert(!open(&token));owned=true;assert(!opens);
 p.pins_[8].owner=&p;assert(!open(&token) && !p.pins_[7].owner);p.pins_[8]={};
 openOk=false;assert(!open(&token) && !token && p.quiescent());openOk=true;
 assert(open(&token) && token);const auto first=token;uint64_t other=0;assert(!open(&other));
 assert(!p.quiescent() && !p.appExitSafe());
 auto& g=p.gpios_[0];g.port=&p;p.pins_[4]={&g,90,false};risc_light_sleep_result_v1 out{sizeof(out),0};
 assert(Port::gpioLightSleep(&g,90,false,&out)==RISC_LIGHT_SLEEP_BUSY);
 assert(Port::gpioDeepSleep(&g,90,false)==RISC_DEEP_SLEEP_BUSY);p.pins_[4]={};
 int16_t samples[512]{};size_t done=99;
 assert(!Port::i2sWrite(&c,token+1,samples,256,&done,40) && !done);
 assert(!Port::i2sWrite(&c,token,samples,257,&done,40));
 assert(!Port::i2sWrite(&c,token,samples,256,&done,41));
 owned=false;assert(!Port::i2sClose(&c,token));owned=true;
 // An outstanding display transaction is independent. Cleanup must not drain it.
 p.spiBuses_[0].held=&p.spis_[0];assert(Port::i2sWrite(&c,token,samples,256,&done,40) && done==256);
 accepted=32;assert(!Port::i2sWrite(&c,token,samples,256,&done,40) && done==32);
 writeOk=false;assert(!Port::i2sWrite(&c,token,samples,256,&done,40));writeOk=true;
 closeOk=false;assert(!Port::i2sClose(&c,token) && c.token==token && p.pins_[7].owner==&c);
 const auto before=writes;assert(!Port::i2sWrite(&c,token,samples,256,&done,40) && writes==before);
 closeOk=true;assert(Port::i2sClose(&c,token));assert(p.spiBuses_[0].held==&p.spis_[0]);p.spiBuses_[0].held=nullptr;
 assert(p.quiescent() && p.appExitSafe() && !Port::i2sClose(&c,token));
 assert(open(&token) && token!=first);accepted=257;assert(!Port::i2sWrite(&c,token,samples,256,&done,40) && !done && p.poisoned_);
 assert(Port::i2sClose(&c,token));
 Port retained(h);auto& r=retained.i2ss_[0];r.port=&retained;r.config=c.config;
 openOk=closeOk=false;token=0;assert(!Port::i2sOpen(&r,1,false,7,8,9,8000,2,&token) && token && retained.poisoned_);
 assert(r.token==token && retained.pins_[7].owner==&r && !retained.appExitSafe());
 closeOk=true;assert(Port::i2sClose(&r,token) && !r.token && !retained.pins_[7].owner);
 puts("Actual CPU I2S: exact typed claims, owner/tokens/bounds, partial transfer, sleep barrier, display-independent close and failed-open retention PASS");
}
