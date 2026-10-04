#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
static bool owns=true,httpActive=false,httpHealthy=true,maintenance=false;
static unsigned calls=0,radioCalls=0;
static int32_t open(void*,const risc_http_request_v1*,uint64_t* out){++calls;httpActive=true;*out=1;return 0;}
static int32_t read(void*,uint64_t,void*,uint32_t,uint32_t* out){++calls;*out=0;return RISC_HTTP_AGAIN;}
static int32_t info(void*,uint64_t,risc_http_response_v1*){++calls;return 0;}
static int32_t close(void*,uint64_t){++calls;if(!httpHealthy)return RISC_HTTP_RETAINED;httpActive=false;return 0;}
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
 puts("CPU HTTP: healthy storage, exit/sleep, wrong-owner, retained no-I/O and transport-before-radio cleanup PASS");
}
