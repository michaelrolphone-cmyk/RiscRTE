#include "bootstrap/LegacySettingsProvider.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
using namespace RiscBoot;
struct Disk {
 std::map<uint32_t,std::map<std::string,std::string>> values;
 std::map<uint32_t,std::vector<uint8_t>> journals;
 bool fail=false;
 KeyValueBackend backend(){return {this,
  [](void*c,uint32_t ns,const char*k,void*out,uint32_t cap,uint32_t*n)->int32_t{auto&d=*static_cast<Disk*>(c);auto i=d.values[ns].find(k);if(i==d.values[ns].end())return RISC_KEY_VALUE_NOT_FOUND;*n=i->second.size();if(cap<*n)return RISC_KEY_VALUE_BUFFER_SMALL;memcpy(out,i->second.data(),*n);return 0;},
  [](void*c,uint32_t ns,const char*k,const void*p,uint32_t n)->int32_t{auto&d=*static_cast<Disk*>(c);d.values[ns][k].assign(static_cast<const char*>(p),n);return d.fail?RISC_KEY_VALUE_IO:0;},2048,
  [](void*c,uint32_t ns,void*out,uint32_t cap,uint32_t*n)->int32_t{auto&v=static_cast<Disk*>(c)->journals[ns];if(v.empty())return RISC_KEY_VALUE_NOT_FOUND;*n=v.size();if(cap<*n)return RISC_KEY_VALUE_BUFFER_SMALL;memcpy(out,v.data(),*n);return 0;},
  [](void*c,uint32_t ns,const void*p,uint32_t n)->int32_t{auto&d=*static_cast<Disk*>(c);d.journals[ns].assign(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n);return d.fail?RISC_KEY_VALUE_IO:0;},
  [](void*c,uint32_t ns,bool*exists)->int32_t{*exists=!static_cast<Disk*>(c)->values[ns].empty();return 0;}};}
};
int main(){
 assert(!openSettingsService(nullptr,nullptr));
 Disk d;auto b=d.backend();SettingsProvider absent{};
 assert(!openSettingsService(&absent,&b)); // selected unavailable provider never falls back
 auto active=openSettingsService(nullptr,&b);auto candidate=openSettingsService(nullptr,&b);
 assert(active&&candidate&&active.get()!=candidate.get());
 assert(active->configure(10,2,true,nullptr,0));assert(candidate->configure(10,2,true,nullptr,0));
 assert(active->status(1)==0&&candidate->status(1)==0&&active->status(3)==UINT32_MAX);
 char out[128]{};uint32_t n=0;bool pending=false;
 assert(!active->launchReady(10,&pending));assert(active->get(10,"mode",out,sizeof out,&n,64,false)==RISC_KEY_VALUE_BUSY);
 assert(active->restore(1,"settings-test",nullptr,0,true)==0);
 assert(active->put(10,"mode","saved",5,false)==0);
 assert(active->get(10,"mode",out,sizeof out,&n,64,false)==0&&std::string(out,n)=="saved");
 assert(candidate->status(1)==0);candidate.reset(); // rejected candidate cannot clear active state
 assert(active->get(10,"mode",out,sizeof out,&n,64,false)==0);
 uint64_t generation=0;assert(active->snapshot(1,out,0,&n,&generation)==RISC_KEY_VALUE_BUFFER_SMALL);
 std::vector<uint8_t> snapshot(n);assert(active->snapshot(1,snapshot.data(),n,&n,&generation)==0);
 assert(active->acknowledge(1,generation)==0);active.reset();
 auto restart=openSettingsService(nullptr,&b);assert(restart->configure(10,2,true,nullptr,0));
 assert(restart->restore(1,"settings-test",snapshot.data(),snapshot.size(),false)==0);
 assert(restart->get(10,"mode",out,sizeof out,&n,64,false)==0&&std::string(out,n)=="saved");
 // Existing native writer namespace remains independent; no key/encoding migration.
 assert(restart->put(7,"mode","native",6,true)==0&&d.values[7]["mode"]=="native");
 assert(restart->get(7,"mode",out,sizeof out,&n,64,true)==0&&std::string(out,n)=="native");
 d.fail=true;assert(restart->put(10,"mode","uncertain",9,false)==RISC_KEY_VALUE_IO);
 assert(restart->status(1)==3);assert(restart->get(10,"mode",out,sizeof out,&n,64,false)==RISC_KEY_VALUE_BUSY);
 assert(d.values[10].empty()); // uncertain checkpoint never downgrades to native
 auto disabled=openSettingsService(nullptr,&b);assert(disabled->configure(0,0,false,nullptr,0));
 assert(disabled->checkpointCount()==0&&disabled->sharedCount()==0&&disabled->status(1)==UINT32_MAX);
 puts("Settings service: optional selection, independent sessions, legacy writes, checkpoint snapshot/ack/restart, cold and uncertain no-fallback PASS");
}
