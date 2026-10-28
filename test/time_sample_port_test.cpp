#define main original_http_port_main
#include "http_port_test.cpp"
#undef main
static int32_t sampleOpen(void*,const risc_time_sample_request_v1*,uint64_t* out){++calls;httpActive=true;*out=1;return 0;}
static int32_t sampleStep(void*,uint64_t,risc_time_sample_v1*){++calls;return RISC_TIME_SAMPLE_PENDING;}
static int32_t sampleClose(void*,uint64_t){++calls;if(!httpHealthy)return RISC_TIME_SAMPLE_RETAINED;httpActive=false;return 0;}
int main(){
 original_http_port_main();owns=true;httpHealthy=true;httpActive=false;
 const risc_time_sample_api_v1 api{1,sizeof(api),nullptr,sampleOpen,sampleStep,sampleClose};
 Hardware hw=bindingHardware(nullptr);hw.httpIdle=nullptr;hw.httpSafe=nullptr;hw.timeSample=&api;
 hw.timeSampleIdle=[](){return !httpActive;};hw.timeSampleSafe=[](){return httpHealthy;};
 hw.radioLeave=[](){++radioCalls;return true;};hw.radioIdle=[](){return true;};
 Port p(hw);RiscBoot::Runtime runtime({[](){return owns;}});assert(p.bind(runtime));uint64_t t=0;
 assert(Port::timeSampleOpen(&p,nullptr,&t)==0&&t==1);
 assert(!p.appExitSafe()&&!p.quiescent()&&!p.restartResourcesSafe()&&p.providerStorageSafe());
 Port::Gpio gpio{};gpio.port=&p;risc_light_sleep_result_v1 result{};result.struct_size=sizeof(result);
 assert(Port::lightSleepImpl(&gpio,1,false,0,&result)==RISC_LIGHT_SLEEP_BUSY);
 assert(Port::deepSleepImpl(&gpio,1,false,0)==RISC_DEEP_SLEEP_BUSY);
 assert(Port::sleepSetImpl(&gpio,1,false,0,false,&result)==RISC_LIGHT_SLEEP_BUSY);
 assert(Port::sleepSetImpl(&gpio,1,false,0,true,&result)==RISC_LIGHT_SLEEP_BUSY);
 Port::Radio radio{};radio.port=&p;radio.token=7;radio.active=true;
 risc_native_radio_async_v1 async{};async.begin=[](const risc_radio_request_v1*,uint32_t*){++radioCalls;return RISC_RADIO_INVALID;};async.cancel=[](uint32_t){++radioCalls;return RISC_RADIO_INVALID;};p.hw_.radioAsync=&async;
 radio.operation=9;risc_radio_request_v1 request{};request.struct_size=sizeof(request);request.flags=RISC_RADIO_TEMPORARY;request.kind=RISC_RADIO_REQUEST_SCAN;uint32_t operation=99;
 auto beforeAsync=radioCalls;assert(Port::radioAsyncBegin(&radio,7,&request,&operation)==RISC_RADIO_BUSY&&!operation);
 assert(Port::radioAsyncCancel(&radio,7,9)==RISC_RADIO_BUSY&&radioCalls==beforeAsync);radio.operation=0;
 auto saved=calls,rs=radioCalls;assert(!Port::radioLeave(&radio,7)&&radioCalls==rs&&!radio.closing);
 for(unsigned which=0;which<3;++which){owns=which!=0;p.sleepRetained_=which==1;p.transferring_=which==2;
 assert(Port::timeSampleStep(&p,t,nullptr)==RISC_TIME_SAMPLE_CONTEXT&&Port::timeSampleClose(&p,t)==RISC_TIME_SAMPLE_CONTEXT&&calls==saved);}
 owns=true;p.sleepRetained_=false;p.transferring_=false;
 httpHealthy=false;assert(!p.providerStorageSafe()&&Port::timeSampleStep(&p,t,nullptr)==RISC_TIME_SAMPLE_RETAINED&&calls==saved);
 assert(Port::timeSampleClose(&p,t)==RISC_TIME_SAMPLE_RETAINED&&httpActive);httpHealthy=true;
 assert(Port::timeSampleClose(&p,t)==0&&!httpActive&&p.appExitSafe()&&p.quiescent());
 assert(Port::radioLeave(&radio,7)&&radioCalls==rs+1);
 for(unsigned which=0;which<7;++which){auto bad=api;auto h=bindingHardware(nullptr);h.timeSample=&bad;h.timeSampleIdle=[](){return true;};h.timeSampleSafe=[](){return true;};
  if(which==0)bad.api_version=2;
  if(which==1)bad.struct_size--;
  if(which==2)bad.open=nullptr;
  if(which==3)bad.step=nullptr;
  if(which==4)bad.close=nullptr;
  if(which==5)h.timeSampleIdle=nullptr;
  if(which==6)h.timeSampleSafe=nullptr;
  Port invalid(h);RiscBoot::Runtime rt({[](){return owns;}});assert(!invalid.bind(rt));}
 puts("CPU sampler owner/retention/exit/restart/quiescence/radio fencing PASS");
}
