#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace RiscCpu;
static bool owns=true,httpActive=false,httpHealthy=true,maintenance=false;
static unsigned calls=0,radioCalls=0;
static int32_t open(void*,const risc_http_request_v1*,uint64_t* out){++calls;httpActive=true;*out=1;return 0;}
static int32_t read(void*,uint64_t,void*,uint32_t,uint32_t* out){++calls;*out=0;return RISC_HTTP_AGAIN;}
static int32_t info(void*,uint64_t,risc_http_response_v1*){++calls;return 0;}
static int32_t close(void*,uint64_t){++calls;if(!httpHealthy)return RISC_HTTP_RETAINED;httpActive=false;return 0;}
static int32_t openTime(void*,const risc_http_request_v1*,const char* server,uint64_t* out){assert(!std::strcmp(server,"time.cloudflare.com"));++calls;httpActive=true;*out=2;return 0;}
static Hardware bindingHardware(const risc_http_client_v1* api){
 Hardware h{};h.owner=[](){return owns;};h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){};
 h.gpioOpen=[](uint8_t,bool,bool,bool){return false;};h.gpioWrite=[](uint8_t,bool){return false;};h.gpioRead=[](uint8_t,bool*){return false;};
 h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){return false;};h.gpioClose=[](uint8_t){return false;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){return false;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){return false;};h.i2cClose=[](uint8_t){return false;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){return false;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){return false;};
 h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){return false;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){return false;};h.spiClose=[](uint8_t){return false;};
 h.httpClient=api;h.httpIdle=[](){return !httpActive;};h.httpSafe=[](){return httpHealthy;};return h;
}
static void suffixTests(){
 const risc_http_client_v1 old{1,sizeof(old),nullptr,open,read,info,close};
 const risc_http_client_time_v1 valid{{1,sizeof(valid),nullptr,open,read,info,close},RISC_HTTP_TIME_TAG_V1,RISC_HTTP_TIME_VERSION_V1,openTime};
 for(unsigned which=0;which<6;++which){
  auto api=valid;if(which==1)api.base.struct_size=sizeof(api)-1;if(which==2)api.time_tag^=1;if(which==3)api.time_version=2;if(which==4)api.open_with_time=nullptr;
  const auto* backend=which==0?&old:&api.base;Port port(bindingHardware(backend));RiscBoot::Runtime runtime({[](){return owns;}});
  assert(port.bind(runtime));const auto* suffix=risc_http_client_time_from_v1(&port.http_.base);
  uint64_t token=99;const unsigned before=calls;
  if(which<5){assert(!suffix && port.http_.base.struct_size==sizeof(old));assert(Port::httpOpenWithTime(&port,nullptr,"time.cloudflare.com",&token)==RISC_HTTP_CLOSED && !token && calls==before);}
  else{
   assert(suffix && suffix->base.context==&port);
   assert(suffix->open_with_time(suffix->base.context,nullptr,"time.cloudflare.com",&token)==0 && token==2 && calls==before+1);
   assert(!port.appExitSafe() && !port.quiescent() && port.providerStorageSafe());
   unsigned saved=calls;port.sleepRetained_=true;assert(suffix->open_with_time(&port,nullptr,"time.cloudflare.com",&token)==RISC_HTTP_CLOSED && !token && calls==saved);port.sleepRetained_=false;
   port.transferring_=true;assert(suffix->open_with_time(&port,nullptr,"time.cloudflare.com",&token)==RISC_HTTP_CLOSED && !token && calls==saved);port.transferring_=false;
   owns=false;assert(suffix->open_with_time(&port,nullptr,"time.cloudflare.com",&token)==RISC_HTTP_CLOSED && !token && calls==saved);owns=true;
   httpHealthy=false;assert(suffix->open_with_time(&port,nullptr,"time.cloudflare.com",&token)==RISC_HTTP_RETAINED && !token && calls==saved);
   assert(!port.appExitSafe() && !port.providerStorageSafe());httpHealthy=true;assert(suffix->base.close(&port,2)==0 && !httpActive);
  }
  assert(port.http_.base.open(&port,nullptr,&token)==0 && token==1);assert(port.http_.base.close(&port,token)==0);
 }
}
int main(){
 const risc_http_client_v1 api{1,sizeof(api),nullptr,open,read,info,close};
 Hardware hw{};hw.owner=[](){return owns;};hw.httpClient=&api;
 hw.httpIdle=[](){return !httpActive;};hw.httpSafe=[](){return httpHealthy;};
 hw.maintenanceIdle=[](){return !maintenance;};
 hw.radioLeave=[](){++radioCalls;return true;};hw.radioIdle=[](){return true;};
 Port p(hw);uint64_t token=0;char byte;uint32_t n=9;
 assert(Port::httpOpen(&p,nullptr,&token)==0&&token==1&&calls==1);
 assert(!p.appExitSafe()&&p.providerStorageSafe()&&!p.quiescent()&&!p.restartResourcesSafe());
 Port::Radio radio{};radio.port=&p;radio.token=7;radio.active=true;
 assert(!Port::radioLeave(&radio,7)&&radioCalls==0&&!radio.closing);
 assert(Port::httpRead(&p,token,&byte,1,&n)==RISC_HTTP_AGAIN&&n==0);
 unsigned saved=calls;p.sleepRetained_=true;
 assert(Port::httpRead(&p,token,&byte,1,&n)==RISC_HTTP_CLOSED&&calls==saved);
 assert(Port::httpClose(&p,token)==RISC_HTTP_CLOSED&&calls==saved);
 assert(!p.providerStorageSafe());p.sleepRetained_=false;
 p.transferring_=true;assert(Port::httpOpen(&p,nullptr,&token)==RISC_HTTP_CLOSED&&calls==saved);p.transferring_=false;
 owns=false;assert(Port::httpClose(&p,1)==RISC_HTTP_CLOSED&&calls==saved);owns=true;
 httpHealthy=false;assert(!p.providerStorageSafe()&&!p.appExitSafe());
 saved=calls;assert(Port::httpRead(&p,1,&byte,1,&n)==RISC_HTTP_RETAINED&&calls==saved&&n==0);
 assert(Port::httpClose(&p,1)==RISC_HTTP_RETAINED&&httpActive);
 httpHealthy=true;assert(Port::httpClose(&p,1)==0&&!httpActive);
 assert(Port::radioLeave(&radio,7)&&radioCalls==1);
 assert(p.providerStorageSafe()&&p.appExitSafe()&&p.quiescent());
 maintenance=true;assert(!p.appExitSafe()&&!p.quiescent()&&p.providerStorageSafe()&&p.restartResourcesSafe());
 Port::Gpio gpio{};gpio.port=&p;risc_light_sleep_result_v1 result{};result.struct_size=sizeof(result);
 assert(Port::lightSleepImpl(&gpio,1,false,0,&result)==RISC_LIGHT_SLEEP_BUSY);
 assert(Port::deepSleepImpl(&gpio,1,false,0)==RISC_DEEP_SLEEP_BUSY);
 maintenance=false;
 suffixTests();
 puts("CPU HTTP: old/tagged suffix forwarding, healthy storage, exit/sleep, wrong-owner, retained no-I/O and transport-before-radio cleanup PASS");
}
