// Execute the real runtime, seven external drivers, and three shared apps.
#include "support/WatchPeripheralModel.h"
using namespace WatchPeripheralModel;
namespace {
bool touchBus=false; unsigned touchReads=0,batteryReads=0,clockReady=0,phase=0,step=0;
std::vector<std::vector<uint8_t>> frames;
bool live(risc_runtime_health_v1* h){assert(++model.calls<30000);h->uptime_ms=0;return clockReady<2;}
bool diagnostic(const char* line){puts(line);assert(!strstr(line,"error="));if(!strncmp(line,"WATCH_CLOCK ready",17))++clockReady;return true;}
bool busOpen(uint8_t p,uint8_t sda,uint8_t scl,uint32_t hz){
 if(!p)return i2cOpen(p,sda,scl,hz);
 assert(p==1 && sda==39 && scl==40 && hz==100000 && !touchBus);touchBus=true;return true;
}
bool busClose(uint8_t p){if(!p)return i2cClose(p);assert(p==1 && touchBus);touchBus=false;return true;}
bool transfer(uint8_t p,uint8_t a,const uint8_t* tx,size_t tn,uint8_t* rx,size_t rn,uint32_t ms){
 if(!p){if(a==0x34 && tn==1 && tx[0]==0x34 && rn==2)++batteryReads;return i2cTransfer(p,a,tx,tn,rx,rn,ms);}
 assert(p==1 && touchBus && a==0x38 && tn==1 && tx[0]==2 && rn==13 && ms==30);
 ++touchReads;memset(rx,0,rn);
 unsigned current=model.rows/240;
 if(current!=phase){phase=current;step=0;}
 if(!phase || phase>=4)return true;
 // Each newly entered app first sees an unarmed held contact and its release.
 // Only a subsequent neutral -> down -> up sequence may launch/exit.
 unsigned x=phase==2?160:20,y=phase==2?100:18;
 bool down=step<2 || step==4 || step==5;
 if(down){rx[0]=1;rx[1]=(x>>8)&15;rx[2]=x;rx[3]=(y>>8)&15;rx[4]=y;}
 ++step;assert(step<40);return true;
}
bool displayTransfer(uint8_t p,const uint8_t* tx,uint8_t* rx,size_t n,uint32_t ms){
 unsigned before=model.rows;bool ok=spiTransfer(p,tx,rx,n,ms);
 if(model.rows!=before && model.rows%240==0){frames.push_back(model.frame);assert(frames.size()<=4);}
 return ok;
}
}
int main(int argc,char**argv){
 assert(argc==2);reset(true);model.registers[0][0x34]=0x0f;model.registers[0][0x35]=0xa0;
 RiscCpu::Port port({owner,now,delay,gpioOpen,gpioWrite,gpioRead,gpioPwm,gpioClose,busOpen,transfer,busClose,spiOpen,spiBegin,displayTransfer,spiEnd,spiClose});cpu=&port;
 RiscBoot::Runtime runtime({owner,live,delay,diagnostic,bind});
 if(!runtime.prepare(argv[1]) || !runtime.run()){fprintf(stderr,"runtime: %s\n",runtime.error());return 1;}
 fprintf(stderr,"ready=%u frames=%zu battery=%u touch=%u steps=%u\n",clockReady,frames.size(),batteryReads,touchReads,step);
 // Current PMU deliberately has no SOC profile; adapter rejects the sample.
 // Battery exits before rendering. Do not claim a Battery Back/Update pass.
 assert(clockReady==2 && frames.size()==3 && batteryReads==1 && touchReads>=13);
 assert(frames[0]==frames[2] && frames[0]!=frames[1]);
 assert(!model.dateWrites && !touchBus && !model.i2c && !model.spi && !model.held && port.quiescent());
 for(bool pin:model.pins)assert(!pin);
 puts("Real runtime: clock -> shared Springboard -> Battery early return (missing SOC profile) -> fresh clock; seven drivers and complete teardown PASS; Battery UI/Back/Update BLOCKED");
}
