#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
static bool owned=true,openOk=true,closeOk=true,idle=true,safe=true,sendOk=true,readOk=true,malformed=false;
static unsigned opens=0,closes=0,sends=0,reads=0;
int main(){
 Hardware h{};h.owner=[](){return owned;};h.hciIdle=[](){return idle;};h.hciSafe=[](){return safe;};
 h.hciOpen=[](){++opens;idle=false;return openOk;};h.hciClose=[](){++closes;if(closeOk)idle=true;return closeOk;};
 h.hciSend=[](uint8_t,const uint8_t*,size_t,uint32_t){++sends;return sendOk;};
 h.hciReceive=[](uint8_t* type,uint8_t*,size_t cap,size_t* n,uint32_t){assert(cap==1028);++reads;*type=4;*n=malformed?1029:0;return readOk;};
 Port p(h);auto& c=p.hci_;c.port=&p;
 uint8_t state=99;assert(Port::hciStatus(&c,0,&state) && state==RISC_HCI_CONTROLLER_OFF);
 uint64_t token=9;owned=false;assert(!Port::hciOpen(&c,0,&token) && !token);owned=true;
 assert(!Port::hciOpen(&c,1,&token) && !opens);
 assert(Port::hciOpen(&c,0,&token) && token && opens==1);const auto first=token;
 assert(Port::hciStatus(&c,token,&state) && state==RISC_HCI_CONTROLLER_ON);
 assert(!Port::hciStatus(&c,0,&state) && state==RISC_HCI_CONTROLLER_RETAINED);
 assert(!Port::hciStatus(&c,token+1,&state));assert(!reads && !sends);
 assert(p.appExitSafe() && p.providerStorageSafe() && !p.restartResourcesSafe() && !p.quiescent());
 uint64_t other=9;assert(!Port::hciOpen(&c,0,&other) && !other && opens==1);
 auto& g=p.gpios_[0];g.port=&p;p.pins_[4]={&g,90,false};risc_light_sleep_result_v1 wake{sizeof(wake),0};
 assert(Port::gpioLightSleep(&g,90,false,&wake)==RISC_LIGHT_SLEEP_BUSY);
 assert(Port::gpioDeepSleep(&g,90,false)==RISC_DEEP_SLEEP_BUSY);
 uint8_t command[]={3,12,0},type=9,out[1028]{};size_t size=99;
 assert(!Port::hciSend(&c,token+1,1,command,3,0));assert(!Port::hciSend(&c,token,1,command,2,0));assert(!Port::hciSend(&c,token,1,command,3,21));assert(!sends);
 assert(Port::hciSend(&c,token,1,command,3,20) && sends==1);
 assert(!Port::hciReceive(&c,token,&type,out,1027,&size,0) && !size && !reads);
 assert(Port::hciReceive(&c,token,&type,out,1028,&size,0) && !size);
 safe=false;assert(!p.appExitSafe() && !p.providerStorageSafe());safe=true;
 closeOk=false;assert(!Port::hciClose(&c,token) && c.token==token && c.closing && !p.appExitSafe());
 assert(Port::gpioLightSleep(&g,90,false,&wake)==RISC_LIGHT_SLEEP_RETAINED);
 assert(Port::gpioDeepSleep(&g,90,false)==RISC_DEEP_SLEEP_RETAINED);
 assert(!Port::hciSend(&c,token,1,command,3,0));assert(!Port::hciReceive(&c,token,&type,out,1028,&size,0));
 assert(Port::hciStatus(&c,token,&state) && state==RISC_HCI_CONTROLLER_RETAINED);
 closeOk=true;owned=false;assert(!Port::hciClose(&c,token));owned=true;assert(Port::hciClose(&c,token));
 assert(Port::hciStatus(&c,0,&state) && state==RISC_HCI_CONTROLLER_OFF);
 assert(p.appExitSafe() && p.restartResourcesSafe());assert(!Port::hciClose(&c,token));
 assert(Port::gpioLightSleep(&g,90,false,&wake)==RISC_LIGHT_SLEEP_UNSUPPORTED);
 assert(Port::gpioDeepSleep(&g,90,false)==RISC_DEEP_SLEEP_UNSUPPORTED);p.pins_[4]={};assert(p.quiescent());
 assert(Port::hciOpen(&c,0,&token) && token>first);malformed=true;assert(!Port::hciReceive(&c,token,&type,out,1028,&size,0) && !size && c.closing);assert(Port::hciClose(&c,token));malformed=false;
 assert(Port::hciOpen(&c,0,&token));sendOk=false;assert(!Port::hciSend(&c,token,1,command,3,0) && c.closing);assert(Port::hciClose(&c,token));
 openOk=false;assert(!Port::hciOpen(&c,0,&token) && !token && p.quiescent());
 closeOk=false;assert(!Port::hciOpen(&c,0,&token) && token && c.closing && !p.appExitSafe());closeOk=true;assert(Port::hciClose(&c,token));
 p.serial_=UINT64_MAX;assert(!Port::hciOpen(&c,0,&token) && !token);
 puts("CPU HCI: owner/scoped tokens, bounds, healthy handoff, sleep barriers, failed-open token and retained cleanup PASS");
}
