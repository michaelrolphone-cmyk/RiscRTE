#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
static bool owned=true,openOk=true,closeOk=true,readOk=true;
static size_t accepted=256;static unsigned opens=0,reads=0;
static void mixedControllerSleep(bool rxFirst){
 Hardware h{};h.owner=[](){return true;};
 h.i2sOpen=[](uint8_t,uint8_t,uint8_t,uint8_t,uint32_t){return true;};
 h.i2sOpenRx=[](uint8_t,uint8_t,uint8_t,uint32_t){return true;};
 h.i2sWrite=[](uint8_t,const int16_t*,size_t,size_t* done,uint32_t){*done=1;return false;};
 h.i2sRead=[](uint8_t,int16_t*,size_t,size_t* done,uint32_t){*done=1;return false;};
 h.i2sClose=[](uint8_t){return true;};
 Port p(h);auto& tx=p.i2ss_[rxFirst?1:0];auto& rx=p.i2ss_[rxFirst?0:1];
 tx.port=rx.port=&p;tx.config={sizeof(tx.config),1,0,7,8,9,0};rx.config={sizeof(rx.config),0,1,10,-1,11,0};
 uint64_t txToken=0,rxToken=0;
 assert(Port::i2sOpen(&tx,1,false,7,8,9,16000,2,&txToken));
 assert(Port::i2sOpen(&rx,0,true,10,-1,11,16000,1,&rxToken));
 assert(p.providerStorageSafe() && !p.appExitSafe());
 int16_t pcm[512]{};size_t done=0;
 if(rxFirst)assert(!Port::i2sWrite(&tx,txToken,pcm,256,&done,40));
 else assert(!Port::i2sRead(&rx,rxToken,pcm,256,&done,40));
 assert(done==1 && !p.providerStorageSafe() && !p.appExitSafe());
 auto& gpio=p.gpios_[0];gpio.port=&p;p.pins_[4]={&gpio,90,false};
 risc_light_sleep_result_v1 result{sizeof(result),0};
 assert(Port::gpioLightSleep(&gpio,90,false,&result)==RISC_LIGHT_SLEEP_RETAINED);
 assert(Port::gpioDeepSleep(&gpio,90,false)==RISC_DEEP_SLEEP_RETAINED);
 assert(Port::gpioLightSleepFor(&gpio,90,false,1,&result)==RISC_LIGHT_SLEEP_RETAINED);
 assert(Port::gpioDeepSleepFor(&gpio,90,false,1)==RISC_DEEP_SLEEP_RETAINED);
 assert(Port::i2sClose(rxFirst?&tx:&rx,rxFirst?txToken:rxToken));
 assert(p.providerStorageSafe() && !p.appExitSafe());
 assert(Port::gpioLightSleep(&gpio,90,false,&result)==RISC_LIGHT_SLEEP_BUSY);
 assert(Port::gpioDeepSleep(&gpio,90,false)==RISC_DEEP_SLEEP_BUSY);
 assert(Port::gpioLightSleepFor(&gpio,90,false,1,&result)==RISC_LIGHT_SLEEP_BUSY);
 assert(Port::gpioDeepSleepFor(&gpio,90,false,1)==RISC_DEEP_SLEEP_BUSY);
 assert(Port::i2sClose(rxFirst?&rx:&tx,rxFirst?rxToken:txToken));
 p.pins_[4]={};assert(p.providerStorageSafe() && p.appExitSafe() && p.quiescent());
}
int main(){
 mixedControllerSleep(false);mixedControllerSleep(true);
 Hardware h{};h.owner=[](){return owned;};
 h.i2sOpenRx=[](uint8_t unit,uint8_t clock,uint8_t data,uint32_t rate){assert(!unit && clock==10 && data==11 && rate==16000);++opens;return openOk;};
 h.i2sRead=[](uint8_t unit,int16_t* pcm,size_t frames,size_t* done,uint32_t ms){assert(!unit && frames==256 && ms==40);++reads;*done=accepted;if(accepted<=frames)for(size_t i=0;i<accepted;++i)pcm[i]=int16_t(i);return readOk;};
 h.i2sClose=[](uint8_t unit){assert(!unit);return closeOk;};
 Port p(h);auto& c=p.i2ss_[0];c.port=&p;c.config={sizeof(c.config),0,1,10,-1,11,0};
 auto open=[&](uint64_t* t){return Port::i2sOpen(&c,0,true,10,-1,11,16000,1,t);};
 uint64_t token=55;int16_t pcm[256]{};size_t done=99;
 assert(!Port::i2sOpen(&c,1,true,10,-1,11,16000,1,&token) && !token);
 assert(!Port::i2sOpen(&c,0,false,10,-1,11,16000,1,&token));
 assert(!Port::i2sOpen(&c,0,true,10,8,11,16000,1,&token));
 assert(!Port::i2sOpen(&c,0,true,10,-1,11,16000,2,&token));
 assert(!Port::i2sOpen(&c,0,true,10,-1,11,22050,1,&token));
 owned=false;assert(!open(&token));owned=true;assert(!opens);
 p.pins_[11].owner=&p;assert(!open(&token) && !p.pins_[10].owner);p.pins_[11]={};
 openOk=false;assert(!open(&token) && !token && p.quiescent());openOk=true;
 p.serial_=UINT64_MAX;assert(!open(&token) && !token && p.quiescent());p.serial_=0;
 assert(open(&token) && token);const auto first=token;
 assert(p.providerStorageSafe() && !p.appExitSafe() && !p.quiescent());
 assert(!Port::i2sWrite(&c,token,pcm,256,&done,40) && !done && p.providerStorageSafe());
 assert(!Port::i2sRead(&c,token+1,pcm,256,&done,40) && !done);
 assert(!Port::i2sRead(&c,token,pcm,257,&done,40));assert(!Port::i2sRead(&c,token,pcm,256,&done,0));
 assert(!Port::i2sRead(&c,token,pcm,256,&done,41));assert(!Port::i2sRead(&c,token,nullptr,256,&done,40));
 owned=false;assert(!Port::i2sRead(&c,token,pcm,256,&done,40));assert(!Port::i2sClose(&c,token));owned=true;
 auto& g=p.gpios_[0];g.port=&p;p.pins_[4]={&g,90,false};risc_light_sleep_result_v1 out{sizeof(out),0};
 assert(Port::gpioLightSleep(&g,90,false,&out)==RISC_LIGHT_SLEEP_BUSY);
 assert(Port::gpioDeepSleep(&g,90,false)==RISC_DEEP_SLEEP_BUSY);
 p.spiBuses_[0].held=&p.spis_[0];assert(Port::i2sRead(&c,token,pcm,256,&done,40) && done==256 && pcm[255]==255);
 accepted=32;assert(!Port::i2sRead(&c,token,pcm,256,&done,40) && done==32 && !p.providerStorageSafe());
 p.spiBuses_[0].held=nullptr;
 assert(Port::gpioLightSleep(&g,90,false,&out)==RISC_LIGHT_SLEEP_RETAINED);
 assert(Port::gpioDeepSleep(&g,90,false)==RISC_DEEP_SLEEP_RETAINED);
 auto before=reads;assert(!Port::i2sRead(&c,token,pcm,256,&done,40) && !done && reads==before);
 closeOk=false;assert(!Port::i2sClose(&c,token) && c.token==token && p.pins_[11].owner==&c);
 closeOk=true;assert(Port::i2sClose(&c,token));assert(p.providerStorageSafe() && p.appExitSafe());p.pins_[4]={};
 assert(open(&token) && token!=first);readOk=false;accepted=256;assert(!Port::i2sRead(&c,token,pcm,256,&done,40) && done==256 && !p.providerStorageSafe());assert(Port::i2sClose(&c,token));readOk=true;
 assert(open(&token));accepted=257;assert(!Port::i2sRead(&c,token,pcm,256,&done,40) && !done && p.poisoned_);assert(Port::i2sClose(&c,token));assert(!p.appExitSafe());
 Port retained(h);auto& r=retained.i2ss_[0];r.port=&retained;r.config=c.config;
 openOk=closeOk=false;token=0;assert(!Port::i2sOpen(&r,0,true,10,-1,11,16000,1,&token) && token && retained.poisoned_);
 assert(r.token==token && retained.pins_[10].owner==&r && retained.pins_[11].owner==&r && !retained.providerStorageSafe());
 closeOk=true;assert(Port::i2sClose(&r,token) && !r.token && !retained.pins_[11].owner);
 puts("Actual CPU PDM RX: selected direction/pads/owner/tokens, partial/error terminal state, bound-storage/sleep barriers, mixed-controller retention precedence and retained cleanup PASS");
}
