#include "bootstrap/Runtime.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <string>
#include <vector>
static std::string mode;
static bool owned=true,nativeBusy=false,inDelay=false;
static unsigned nativeChecks=0,blockedReentries=0;
static std::vector<std::string> events;
static std::vector<uint32_t> ordinaryWaits,retainedWaits;
static risc_runtime_api_v1 saved{};
static risc_key_value_v1 savedKeyValue{};
static unsigned keyValueCalls=0;
extern "C" void retained_yield_save_key_value(const risc_key_value_v1* api){savedKeyValue=*api;}
static RiscBoot::Runtime* runtime;
static RiscBoot::Runtime* other;
static const void* appImage;
static const void* providerImage;
static const char* appAllocation;
static unsigned count(const char* e){return std::count(events.begin(),events.end(),e);}
extern "C" const char* retained_yield_mode(){return mode.c_str();}
extern "C" void retained_yield_event(const char* e){events.emplace_back(e);}
extern "C" void retained_yield_owner(bool owner){owned=owner;}
extern "C" void retained_yield_native_busy(bool busy){nativeBusy=busy;}
extern "C" void retained_yield_save_provider(const void* p){providerImage=p;}
extern "C" void retained_yield_save(const risc_runtime_api_v1* api,const void* image,const char* allocation){
  saved=*api;appImage=image;appAllocation=allocation;
}
extern "C" void retained_yield_lifecycle(){
  if(!saved.yield_ms)return;
  const auto before=ordinaryWaits.size()+retainedWaits.size();
  saved.yield_ms(50);runtime->yield(50);
  assert(ordinaryWaits.size()+retainedWaits.size()==before);
  ++blockedReentries;
}
static void delay(uint32_t ms,bool retained){
  assert(!inDelay);inDelay=true;
  (retained?retainedWaits:ordinaryWaits).push_back(ms);
  if(retained){
    assert(!risc_runtime_get_api(1));
    risc_runtime_health_v1 health{sizeof(health)};
    assert(!saved.health(&health) && !saved.diagnostic("forbidden"));
    if(savedKeyValue.get){
      const unsigned before=keyValueCalls;uint32_t size=123;
      assert(savedKeyValue.get(savedKeyValue.context,"probe",nullptr,0,&size)==RISC_KEY_VALUE_CONTEXT);
      assert(!size && keyValueCalls==before);
    }
  }
  if(saved.yield_ms){
    const auto before=ordinaryWaits.size()+retainedWaits.size();
    saved.yield_ms(50);runtime->yield(50);
    assert(ordinaryWaits.size()+retainedWaits.size()==before);
    ++blockedReentries;
  }
  inDelay=false;
}
extern "C" void retained_yield_check(bool retained,bool after){
  static size_t normalBegin,retainedBegin;
  static unsigned pollBegin,nativeBegin;
  if(!after){
    normalBegin=ordinaryWaits.size();retainedBegin=retainedWaits.size();
    pollBegin=count("provider:poll");nativeBegin=nativeChecks;
    /* A different prepared Runtime is not the current invocation. */
    other->yield(50);assert(ordinaryWaits.size()==normalBegin && retainedWaits.size()==retainedBegin);
    return;
  }
  const std::vector<uint32_t> expected{1,1,20,50,50,50,50,50,50};
  if(retained){
    printf("retained loop: ordinary waits=%zu raw waits=%zu provider polls=%u\n",ordinaryWaits.size()-normalBegin,retainedWaits.size()-retainedBegin,count("provider:poll")-pollBegin);fflush(stdout);
    assert(ordinaryWaits.size()==normalBegin);
    assert((std::vector<uint32_t>(retainedWaits.begin()+retainedBegin,retainedWaits.end())==
      (mode=="no-raw-hook"?std::vector<uint32_t>{}:expected)));
    assert(count("provider:poll")==pollBegin);
  } else {
    assert(retainedWaits.size()==retainedBegin);
    assert((std::vector<uint32_t>(ordinaryWaits.begin()+normalBegin,ordinaryWaits.end())==expected));
    assert(count("provider:poll")==pollBegin+expected.size());
  }
  /* No native exit checks, metadata reads, or integrity work on yield. */
  assert(nativeChecks==nativeBegin);
}
static void write(const std::string& root,const char* name,const char* bytes){std::ofstream(root+"/"+name)<<bytes;}
static bool mapped(const void* p){Dl_info info{};return p && dladdr(p,&info);}
int main(int argc,char** argv){
  assert(argc==3);const std::string root=argv[1];mode=argv[2];
  write(root,"board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  write(root,"provider.json",R"({"type":"driver","id":"yield-provider","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"provider.elf","requires":[],"provides":[{"capability":"test.yield","api":1}]})");
  write(root,"app.json",R"({"type":"application","id":"retained-yield","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.yield","api":1}]})");
  write(root,"boot.json",R"({"board":"board.json","default_app":"default.elf","provider_activation":"demand","drivers":[{"manifest":"provider.json"}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.yield","api":1,"instance_id":0}]}]})");
  if(mode=="invalid-interface"){
    write(root,"app.json",R"({"type":"application","id":"retained-yield","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.yield","api":1},{"capability":"storage.key-value","api":1}]})");
    write(root,"boot.json",R"({"board":"board.json","default_app":"default.elf","provider_activation":"demand","drivers":[{"manifest":"provider.json"}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.yield","api":1,"instance_id":0},{"capability":"storage.key-value","api":1,"instance_id":1}]}]})");
  }
  RiscBoot::KeyValueBackend keyValue{nullptr,[](void*,uint32_t,const char*,void*,uint32_t,uint32_t*){++keyValueCalls;return int32_t(RISC_KEY_VALUE_NOT_FOUND);},[](void*,uint32_t,const char*,const void*,uint32_t){++keyValueCalls;return int32_t(RISC_KEY_VALUE_OK);}};
  RiscBoot::Port port{[](){return owned;},[](risc_runtime_health_v1* h){h->uptime_ms=0;return true;},
    [](uint32_t ms){delay(ms,false);},[](const char*){return true;}};
  port.keyValue=&keyValue;
  port.appExitSafe=[](){++nativeChecks;return !nativeBusy;};
  if(mode!="no-raw-hook")port.retainedDelay=[](uint32_t ms){delay(ms,true);};
  runtime=new RiscBoot::Runtime(port);other=new RiscBoot::Runtime(port);
  runtime->yield(50);assert(ordinaryWaits.empty() && retainedWaits.empty());
  assert(runtime->prepare(root.c_str()) && other->prepare(root.c_str()));
  runtime->yield(50);assert(ordinaryWaits.empty() && retainedWaits.empty());
  const bool retained=mode!="normal" && mode!="native-busy";
  assert(runtime->run()==!retained && runtime->retained()==retained);
  assert(!risc_runtime_get_api(1));
  const auto before=ordinaryWaits.size()+retainedWaits.size();
  saved.yield_ms(50);runtime->yield(50);other->yield(50);
  assert(ordinaryWaits.size()+retainedWaits.size()==before);
  assert(!count("child:entry") && !count("child:init"));
  assert(blockedReentries>0);
  if(retained){
    assert(!count("app:fini") && !count("app:unloaded") && !count("provider:unloaded") && !count("provider:stop"));
    assert(count("provider:quiesce")==unsigned(mode=="graph-retained" || mode=="invalid-interface"));
    assert(mapped(appImage) && mapped(providerImage) && !strcmp(appAllocation,"still retained"));
    assert(!runtime->run());
  }else{
    assert(count("app:fini")==1 && count("app:unloaded")==1 && count("provider:unloaded")==1);
    assert(count("provider:quiesce")==1 && count("provider:stop")==1);
    assert(!mapped(appImage) && !mapped(providerImage));
  }
  printf("Retained yield %-16s normal=%zu retained=%zu polls=%u reentries=%u PASS\n",mode.c_str(),ordinaryWaits.size(),retainedWaits.size(),count("provider:poll"),blockedReentries);
  if(retained){fflush(stdout);std::_Exit(0);}
  delete runtime;delete other;
}
