#include "NativeSntp.h"
#ifdef RISC_PAIRED_BANKS
#include "bootstrap/Json.h"
#include "HttpBounds.h"
#include "runtime/provisioning/TimeInput.h"
#include <esp_sntp.h>
#include <atomic>
#include <cstring>
namespace RiscBootstrap {
namespace {
// Permanent, one-shot boot lifetime: delayed callbacks never reference a freed
// session or a subsequent acquisition. No retry resets this object's identity.
struct Clock {
 enum {Dormant,Waiting,Writing,Ready,Cancelled};
 std::atomic<unsigned> state{Dormant};
 char servers[3][64]{};size_t count=0;uint64_t (*now)()=nullptr;
 uint64_t started=0,observed=0,utc=0;TimeSample sample{};
 bool configured=false,owned=false,cached=false,closed=false;
 bool stop(){
   state.exchange(Cancelled,std::memory_order_acq_rel);
   if(owned){
     esp_sntp_stop();
     // IDF4.4 stop queues a tcpip callback. setservername uses synchronous
     // tcpip_api_call, draining the stop and any preceding sync callback before
     // unregistering. Server storage has permanent boot lifetime.
     esp_sntp_setservername(0,servers[0]);
     esp_sntp_set_time_sync_notification_cb(nullptr);
     if(esp_sntp_enabled())return false;
     for(unsigned i=0;i<SNTP_MAX_SERVERS;++i)esp_sntp_setservername(i,nullptr);
     owned=false;
   }
   closed=true;return true;
 }
 TimeStatus poll(TimeSample* out);
};
Clock clock;
void synchronized(struct timeval* value){
 unsigned waiting=Clock::Waiting;
 if(!clock.state.compare_exchange_strong(waiting,Clock::Writing,std::memory_order_acq_rel))return;
 // now is initialized before callback registration and never changed afterward.
 clock.observed=clock.now();clock.utc=value&&value->tv_sec>0?uint64_t(value->tv_sec):0;
 unsigned writing=Clock::Writing;
 (void)clock.state.compare_exchange_strong(writing,Clock::Ready,std::memory_order_release,std::memory_order_relaxed);
}
TimeStatus Clock::poll(TimeSample* out){
 if(!out||!configured)return TimeStatus::Unavailable;
 if(cached){*out=sample;return TimeStatus::Ready;}
 if(closed)return TimeStatus::Unavailable;
 auto status=state.load(std::memory_order_acquire);
 if(status==Dormant){
   // Do not take over another owner's SNTP process or accept its old status.
   if(esp_sntp_enabled()){closed=true;return TimeStatus::Unavailable;}
   started=now();owned=true;
   esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
   esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
   esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
#if LWIP_DHCP_GET_NTP_SRV
   esp_sntp_servermode_dhcp(false);
#endif
   for(unsigned i=0;i<SNTP_MAX_SERVERS;++i)esp_sntp_setservername(i,i<count?servers[i]:nullptr);
   state.store(Waiting,std::memory_order_release);
   esp_sntp_set_time_sync_notification_cb(synchronized);esp_sntp_init();
   return TimeStatus::Pending;
 }
 if(status==Ready){
   const auto current=now();
   const bool valid=observed>=started&&observed<=current&&current-started<30000u&&
     utc>=RiscCpu::HttpBounds::FirstUtc&&utc<=RiscCpu::HttpBounds::LastUtc;
   sample={utc,observed,300000u};
   if(!stop()||!valid)return TimeStatus::Unavailable;
   cached=true;*out=sample;return TimeStatus::Ready;
 }
 if(now()-started>=30000u){(void)stop();return TimeStatus::Unavailable;}
 return status==Waiting||status==Writing?TimeStatus::Pending:TimeStatus::Unavailable;
}

}
FreshTime configuredFreshTime(RiscProvision::Input input,uint64_t (*now)()){
 if(clock.configured||!input.read||!now)return {};
 RiscProvision::Descriptor descriptor;
 if(RiscProvision::loadDescriptor(input,descriptor)!=RiscProvision::InputStatus::Ready||!descriptor.timeKey[0])return {};
 char bytes[384]{};uint32_t size=0;
 if(input.read(input.context,descriptor.timeKey,bytes,sizeof(bytes),&size)!=RiscProvision::InputStatus::Ready||!size||size>sizeof(bytes))return {};
 JsonDocument json;if(!RiscBoot::parse(bytes,size,json))return {};
 auto root=json.as<JsonObjectConst>();int64_t version=0;
 if(!RiscBoot::keys(root,{"schema","schema_version","servers"})||!RiscBoot::eq(root["schema"],"riscrte.sntp")||!RiscBoot::integer(root["schema_version"],1,1,version))return {};
 auto servers=root["servers"].as<JsonArrayConst>();
 if(servers.isNull()||servers.size()<1||servers.size()>3||servers.size()>SNTP_MAX_SERVERS)return {};
 for(auto item:servers){if(!RiscBoot::text(item,clock.servers[clock.count],64)||!RiscProvision::timeServer(clock.servers[clock.count])){clock.count=0;return {};}++clock.count;}
 clock.now=now;clock.configured=true;
 return {&clock,[](void* p,TimeSample* out){return static_cast<Clock*>(p)->poll(out);},[](void* p){return static_cast<Clock*>(p)->stop();}};
}
}
#endif
