#include "ports/esp32s3/NativeSntp.h"
#include <esp_sntp.h>
#include <cassert>
#include <cstring>
#include <string>
#include <iostream>
static bool enabled=false,stopFails=false,stopQueued=false;static Callback callback=nullptr,late=nullptr;
static int starts=0,stops=0;static uint64_t ticks=100;
static std::string servers[3];
static std::string inputDescriptor=R"({"schema":"riscrte.bootstrap","schema_version":1,"profile_key":"profile"})";
bool esp_sntp_enabled(){return enabled;}
void esp_sntp_stop(){++stops;stopQueued=true;}
void esp_sntp_init(){enabled=true;++starts;late=callback;}
void esp_sntp_set_time_sync_notification_cb(Callback value){callback=value;}
void esp_sntp_setservername(unsigned n,const char* value){assert(n<3);if(stopQueued){if(!stopFails)enabled=false;stopQueued=false;}servers[n]=value?value:"";}
void esp_sntp_setoperatingmode(int){}void esp_sntp_set_sync_mode(int){}
void esp_sntp_set_sync_status(int){}void esp_sntp_servermode_dhcp(bool value){assert(!value);}
int main(int argc,char** argv){
 assert(argc==2);std::string mode=argv[1];
 if(mode=="v2")inputDescriptor=R"({"schema":"riscrte.bootstrap","schema_version":2,"profile_key":"install_p0","time_key":"install_t0"})";
 if(mode=="disabled")inputDescriptor=R"({"schema":"riscrte.bootstrap","schema_version":2,"profile_key":"install_p0","time_key":""})";
 std::string config=R"({"schema":"riscrte.sntp","schema_version":1,"servers":["time.example.invalid"]})";
 if(mode=="invalid")config=R"({"schema":"riscrte.sntp","schema_version":1,"servers":["https://bad"]})";
 if(mode=="unknown")config=R"({"schema":"riscrte.sntp","schema_version":1,"servers":["time.example.invalid"],"utc":2000000000})";
 RiscProvision::Input input{&config,[](void* p,const char* key,void* out,uint32_t cap,uint32_t* size){if(!strcmp(key,"descriptor")){const char* d=inputDescriptor.c_str();*size=strlen(d);memcpy(out,d,*size);return RiscProvision::InputStatus::Ready;}assert(!strcmp(key,"time")||!strcmp(key,"install_t0"));auto& s=*static_cast<std::string*>(p);assert(s.size()<=cap);memcpy(out,s.data(),s.size());*size=s.size();return RiscProvision::InputStatus::Ready;}};
 auto time=RiscBootstrap::configuredFreshTime(input,[](){return ticks;});
 if(mode=="invalid"||mode=="unknown"||mode=="disabled"){assert(!time.poll&&!starts);return 0;}
 assert(time.poll&&time.stop&&!starts);
 RiscBootstrap::TimeSample sample{};
 if(mode=="busy"){enabled=true;assert(time.poll(time.context,&sample)==RiscBootstrap::TimeStatus::Unavailable);assert(!starts&&!stops);return 0;}
 assert(time.poll(time.context,&sample)==RiscBootstrap::TimeStatus::Pending);
 assert(starts==1&&servers[0]=="time.example.invalid"&&servers[1].empty());
 assert(time.poll(time.context,&sample)==RiscBootstrap::TimeStatus::Pending); // existing wall clock cannot qualify
 if(mode=="timeout"){ticks+=30000;assert(time.poll(time.context,&sample)==RiscBootstrap::TimeStatus::Unavailable);}
 else if(mode=="cancel"){assert(time.stop(time.context));}
 else {
   timeval value{};value.tv_sec=mode=="range"?1:1800000000;ticks+=12;callback(&value);
   if(mode=="stop-fail")stopFails=true;
   auto status=time.poll(time.context,&sample);
   if(mode=="range"||mode=="stop-fail")assert(status==RiscBootstrap::TimeStatus::Unavailable);
   else {assert(status==RiscBootstrap::TimeStatus::Ready);assert(sample.utc_seconds==1800000000&&sample.sampled_monotonic_ms==ticks);}
 }
 assert(!callback);
 timeval value{};value.tv_sec=1900000000;late(&value); // delayed callback cannot resurrect/corrupt cached sample
 if(mode=="success"||mode=="v2"){assert(time.poll(time.context,&sample)==RiscBootstrap::TimeStatus::Ready);assert(sample.utc_seconds==1800000000);}
 else assert(time.poll(time.context,&sample)==RiscBootstrap::TimeStatus::Unavailable);
 stopFails=false,stopQueued=false;assert(time.stop(time.context));assert(!enabled);
 std::cout<<"SNTP lifecycle "<<mode<<" PASS\n";
}
