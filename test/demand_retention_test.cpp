#include "bootstrap/Runtime.h"
#include "diagnostics/Performance.h"
#include "diagnostics/StageLog.h"
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
using RiscBoot::Runtime;
static std::string mode;
static std::vector<std::string> events,lines;
static bool owned=true,nativeSafe=true,startOk=true,stopOk=true,inAcquire=false;
static unsigned calls=0,finis=0,loads=0,metadataReads=0,metadataParses=0;
static uint64_t ticks=0;
static risc_provider_promotion_api_v1 promotion{};
static risc_runtime_capability_v1 control{};
static const void* savedLeaf=nullptr;
static constexpr unsigned providerLimit=RuntimeProviders::GraphV2::kMaxModules;
static unsigned count(const char* value){return std::count(events.begin(),events.end(),value);}
namespace RiscDiagnostics {
uint64_t monotonicUs(){return ++ticks;}
void timestamped(const char* format,...){
  char text[512];va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);
  lines.emplace_back(text);if(!strncmp(text,"provider load begin ",20))++loads;
}
}
static bool acquire(const char* id,risc_runtime_capability_v1& out){
  out={};out.struct_size=sizeof(out);inAcquire=true;
  const bool result=risc_runtime_get_api(1)->acquire((std::string("test.")+id).c_str(),1,0,&out);
  inAcquire=false;return result;
}
extern "C" bool demand_event(const char* id,const char* event){
  events.emplace_back(std::string(id)+":"+event);
  if(inAcquire && !strcmp(event,"start") && promotion.promote && mode!="before-release") {
    assert(promotion.promote(promotion.context)==(calls==2?RISC_PROVIDER_PROMOTION_CONTEXT:RISC_PROVIDER_PROMOTION_BUSY));
    const auto* api=risc_runtime_get_api(1);assert(api);
    risc_runtime_capability_v1 denied{};denied.struct_size=sizeof(denied);
    assert(!api->acquire("test.unused",1,0,&denied));
    assert(!api->release(&control));assert(!api->request_launch("child.elf"));
    assert(!api->retain_invocation());api->yield_ms(1);
  }
  if(!strcmp(id,"leaf") && !strcmp(event,"start")) {
    if(mode=="native-retained")nativeSafe=false;
    return startOk;
  }
  if(!strcmp(id,"leaf") && !strcmp(event,"quiesce"))return stopOk;
  return true;
}
extern "C" void demand_init(){
  risc_runtime_capability_v1 denied{};denied.struct_size=sizeof(denied);
  assert(!risc_runtime_get_api(1)->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&denied));
  if(promotion.promote)assert(promotion.promote(promotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);
}
extern "C" void demand_fini(){
  ++finis;events.emplace_back("app:fini");
  if(promotion.promote)assert(promotion.promote(promotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);
}
static void arm(){
  const auto* api=risc_runtime_get_api(1);control={};control.struct_size=sizeof(control);
  assert(api->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&control));
  promotion=*static_cast<const risc_provider_promotion_api_v1*>(control.api);
  owned=false;assert(promotion.promote(promotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);owned=true;
  nativeSafe=false;assert(promotion.promote(promotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);nativeSafe=true;
  assert(promotion.promote(reinterpret_cast<void*>(uintptr_t(promotion.context)+1))==RISC_PROVIDER_PROMOTION_CONTEXT);
  const unsigned before=loads;
  const int status=promotion.promote(promotion.context);
  assert(status==((mode=="baseline-eager" || calls==3)?RISC_PROVIDER_PROMOTION_ALREADY_READY:RISC_PROVIDER_PROMOTION_OK));
  if(mode!="baseline-demand")assert(loads==before);
  assert(promotion.promote(promotion.context)==RISC_PROVIDER_PROMOTION_ALREADY_READY);
}
static void getRelease(const char* id){
  risc_runtime_capability_v1 out{};assert(acquire(id,out));
  if(!strcmp(id,"leaf")) {
    if(savedLeaf && mode!="before-release")assert(out.api==savedLeaf);
    savedLeaf=out.api;
  }
  assert(risc_runtime_get_api(1)->release(&out));
}
extern "C" void demand_app(){
  ++calls;events.emplace_back("app:entry");const auto* api=risc_runtime_get_api(1);assert(api);
  if(calls==1){metadataReads=RiscPerf::data.counts[RiscPerf::MetadataReadBegin];metadataParses=RiscPerf::data.counts[RiscPerf::ParseBegin];}
  if(mode=="timer"){for(unsigned i=0;i<20;++i)api->yield_ms(1);assert(!loads);return;}
  if(mode=="limits") {
    if(calls==1) {
      arm();assert(!loads);
      for(unsigned i=0;i<15;++i){char id[8];snprintf(id,sizeof(id),"p%02u",i);getRelease(id);}
      assert(loads==15);assert(api->request_launch("child.elf"));return;
    }
    if(calls==2) {
      if(providerLimit==17) {
        getRelease("p15");
        risc_runtime_capability_v1 live[16]{};
        for(unsigned i=0;i<15;++i)assert(acquire("p15",live[i]));
        // The new boot pin uses the final graph slot; the app grant fails.
        assert(!acquire("p16",live[15]));assert(loads==17 && count("p16:start")==1);
        assert(api->release(&live[0]));assert(acquire("p16",live[15]));
        for(auto& out:live)if(out.slot)assert(api->release(&out));
        assert(count("p16:start")==1 && !count("p16:stop"));
      }else for(unsigned i=15;i<providerLimit;++i){char id[8];snprintf(id,sizeof(id),"p%02u",i);getRelease(id);}
      assert(loads==providerLimit);return;
    }
    arm();assert(api->release(&control));
    risc_runtime_capability_v1 live[17]{};
    const unsigned available=std::min(16u,unsigned(RuntimeProviders::GraphV2::kMaxGrants)-providerLimit);
    for(unsigned i=0;i<available;++i)assert(acquire("p00",live[i]));
    assert(!acquire("p00",live[available]));
    assert(api->release(&live[0]));assert(acquire("p00",live[available]));
    for(auto& out:live)if(out.slot)assert(api->release(&out));
    assert(loads==providerLimit);return;
  }
  if(calls==2) {
    assert(promotion.promote(promotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);
    risc_runtime_capability_v1 denied{};denied.struct_size=sizeof(denied);
    assert(!api->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&denied));
    getRelease("leaf");if(mode=="late")getRelease("unused");return;
  }
  if(calls==3) {
    arm();getRelease("leaf");if(mode=="late")getRelease("unused");return;
  }
  if(mode=="pre-failed-retained") {
    assert(api->request_launch("child.elf"));startOk=stopOk=false;
    risc_runtime_capability_v1 out{};assert(!acquire("leaf",out));return;
  }
  if(mode=="before-release" || mode=="pending") {
    if(mode=="pending") {
      risc_runtime_capability_v1 out{};assert(acquire("leaf",out));stopOk=false;assert(!api->release(&out));
      control={};control.struct_size=sizeof(control);
      assert(!api->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&control));
      stopOk=true;assert(api->release(&out));
    }else getRelease("leaf");
    assert(count("leaf:stop")==1 && loads==2);
    arm();assert(loads==2);getRelease("leaf");assert(loads==4);return;
  }
  if(mode=="handoff" || mode=="baseline-demand" || mode=="baseline-eager" || mode=="late") {
    risc_runtime_capability_v1 out{};assert(acquire("leaf",out));savedLeaf=out.api;
    arm();assert(api->release(&out));
    const unsigned before=loads;
    auto stale=promotion;assert(api->release(&control));
    assert(stale.promote(stale.context)==RISC_PROVIDER_PROMOTION_CONTEXT);
    control.struct_size=sizeof(control);assert(api->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&control));
    promotion=*static_cast<const risc_provider_promotion_api_v1*>(control.api);
    assert(stale.promote(stale.context)==RISC_PROVIDER_PROMOTION_CONTEXT);
    assert(promotion.promote(promotion.context)==RISC_PROVIDER_PROMOTION_ALREADY_READY);
    assert(api->release(&control));
    // Arming is session-owned even when its control grant is retired.
    getRelease("leaf");assert(loads==before);
    assert(api->request_launch("child.elf"));return;
  }
  arm();assert(!loads);
  if(mode=="armed-empty")return;
  if(mode=="queued") {
    assert(api->request_launch("child.elf"));
    risc_runtime_capability_v1 out{};assert(!acquire("leaf",out));assert(!loads);return;
  }
  if(mode=="partial-retry" || mode=="partial-retained")getRelease("unused");
  if(mode=="retry" || mode=="failed-retained" || mode=="partial-retry" || mode=="partial-retained") {
    startOk=false;stopOk=mode=="retry" || mode=="partial-retry";
    risc_runtime_capability_v1 out{};assert(!acquire("leaf",out));
    if(mode=="failed-retained" || mode=="partial-retained") {
      assert(!risc_runtime_get_api(1));assert(!api->request_launch("child.elf"));
      assert(promotion.promote(promotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);return;
    }
    assert(count("leaf:stop")==1 && count("root:stop")==1 && !count("unused:stop"));startOk=true;
  }
  if(mode=="native-retained") {
    risc_runtime_capability_v1 out{};assert(!acquire("leaf",out));
    assert(!risc_runtime_get_api(1));assert(!api->request_launch("child.elf"));return;
  }
  owned=false;risc_runtime_capability_v1 denied{};denied.struct_size=sizeof(denied);
  assert(!api->acquire("test.leaf",1,0,&denied));owned=true;
  getRelease("leaf");getRelease("leaf");
  if(mode=="shutdown-retained")stopOk=false;
}
static void write(const std::string& root,const std::string& name,const std::string& content){std::ofstream(root+"/"+name)<<content;}
static void save(const std::string& root,const std::string& name,const JsonDocument& doc){std::string value;serializeJson(doc,value);write(root,name,value);}
static void app(const std::string& root,JsonDocument& boot,const char* id,const std::vector<std::string>& caps,bool promote){
  JsonDocument manifest;manifest["type"]="application";manifest["id"]=id;manifest["version"]="1.0.0";
  manifest["architecture"]="xtensa-esp32s3";manifest["file_name"]=std::string(id)+".elf";manifest["entry"]="app_main";
  auto reqs=manifest["requires"].to<JsonArray>();
  auto p=boot["app_capabilities"].as<JsonArray>().add<JsonObject>();p["manifest"]=std::string(id)+".json";
  auto grants=p["grants"].to<JsonArray>();
  auto add=[&](const char* capability){auto r=reqs.add<JsonObject>();r["capability"]=capability;r["api"]=1;
    auto g=grants.add<JsonObject>();g["capability"]=capability;g["api"]=1;g["instance_id"]=0;};
  for(const auto& cap:caps)add(cap.c_str());
  if(promote)add(RISC_PROVIDER_PROMOTION_CAPABILITY);
  save(root,std::string(id)+".json",manifest);
}
int main(int argc,char** argv){
  assert(argc==3);const std::string root=argv[1];mode=argv[2];
  RiscPerf::configure([](){return ++ticks;},[](){return owned;},true);
  JsonDocument boot;boot["board"]="board.json";boot["default_app"]="default.elf";
  boot["provider_activation"]=mode=="baseline-eager"?"eager":mode=="baseline-demand"?"demand":"demand-retained";
  auto selected=boot["drivers"].to<JsonArray>();
  std::vector<std::string> ids=mode=="limits"?std::vector<std::string>{}:std::vector<std::string>{"leaf","unused","root"};
  if(mode=="limits")for(unsigned i=0;i<providerLimit;++i){char id[8];snprintf(id,sizeof(id),"p%02u",i);ids.emplace_back(id);}
  for(const auto& id:ids) {
    JsonDocument m;m["type"]="driver";m["id"]=id;m["version"]="1.0.0";m["driver_abi"]=2;
    m["architecture"]="xtensa-esp32s3";m["file_name"]=id+".elf";
    auto req=m["requires"].to<JsonArray>();if(id=="leaf"){auto r=req.add<JsonObject>();r["capability"]="test.root";r["api"]=1;}
    auto provided=m["provides"].to<JsonArray>().add<JsonObject>();provided["capability"]="test."+id;provided["api"]=1;
    save(root,id+".json",m);selected.add<JsonObject>()["manifest"]=id+".json";
  }
  boot["app_capabilities"].to<JsonArray>();
  std::vector<std::string> first,second;
  if(mode=="limits"){for(unsigned i=0;i<providerLimit;++i)(i<15?first:second).push_back("test."+ids[i]);}
  else first=second={"test.leaf","test.unused"};
  app(root,boot,"default",first,true);app(root,boot,"child",second,false);
  write(root,"board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  RiscBoot::Port port{[](){return owned;},[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}};
  port.appExitSafe=[](){return nativeSafe;};
  if(mode=="limits") {
    selected.add(selected[0]);save(root,"boot.json",boot);
    Runtime denied(port);assert(!denied.prepare(root.c_str()));assert(!loads);selected.remove(selected.size()-1);
  }
  for(const char* invalid:{"null","true","1","{}","[]","\"Demand-retained\""}) {
    JsonDocument bad;bad.set(boot);JsonDocument value;assert(!deserializeJson(value,invalid));bad["provider_activation"].set(value);
    save(root,"boot.json",bad);Runtime denied(port);assert(!denied.prepare(root.c_str()));assert(!loads);
  }
  save(root,"boot.json",boot);
  const std::string unused=mode=="limits"?ids.back()+".json":"unused.json";
  std::ifstream in(root+"/"+unused);const std::string original((std::istreambuf_iterator<char>(in)),{});in.close();
  write(root,unused,"{}");{Runtime denied(port);assert(!denied.prepare(root.c_str()));assert(!loads);}
  write(root,unused,original);
  Runtime runtime(port);assert(runtime.prepare(root.c_str()));assert(!loads);
  unsigned inspected=0;assert(runtime.inspectImages([](void* c,const char*,bool){++*static_cast<unsigned*>(c);return true;},&inspected));
  assert(inspected==ids.size()+2);
  assert(!runtime.inspectImages([](void*,const char* path,bool){return !strstr(path,"unused.elf") && !strstr(path,"p16.elf");},nullptr));
  const bool retained=mode=="failed-retained" || mode=="partial-retained" || mode=="native-retained" || mode=="pre-failed-retained" || mode=="shutdown-retained";
  assert(runtime.run()!=retained);assert(runtime.retained()==retained);
  if(mode=="timer" || mode=="armed-empty")assert(loads==0);
  else if(mode=="limits")assert(loads==providerLimit && calls==3 && finis==3);
  else if(mode=="before-release" || mode=="retry" || mode=="pending")assert(loads==4 && count("leaf:start")==2);
  else if(mode=="partial-retry")assert(loads==5 && count("leaf:start")==2 && count("unused:start")==1);
  else if(mode=="partial-retained")assert(loads==3 && count("leaf:start")==1 && count("unused:start")==1 && !count("unused:stop"));
  else if(mode=="baseline-eager" || mode=="baseline-demand" || mode=="late")assert(loads==3 && calls==3 && count("leaf:start")==1 && count("unused:start")==1);
  else assert(loads==2 && count("leaf:start")==1 && !count("unused:start"));
  if(!retained)for(const auto& id:ids){assert(count((id+":start").c_str())==count((id+":stop").c_str()));}
  else {
    assert(calls==1 && finis==unsigned(mode=="shutdown-retained"));
    assert(!count("root:stop") && !count("leaf:stop"));assert(!runtime.run());
  }
  if(mode=="handoff" || mode=="late")assert(calls==3 && finis==3);
  if(count("leaf:start"))assert(std::find(events.begin(),events.end(),"root:start")<std::find(events.begin(),events.end(),"leaf:start"));
  unsigned armed=0;for(const auto& line:lines)if(line.find("providers retention armed ")==0)++armed;
  assert(armed==unsigned(mode!="timer" && mode!="pre-failed-retained" && mode!="baseline-demand" && mode!="baseline-eager"));
  if(mode=="handoff")assert(std::count(lines.begin(),lines.end(),"providers retention armed active=2 deferred=1")==1);
  if(mode=="armed-empty")assert(std::count(lines.begin(),lines.end(),"providers retention armed active=0 deferred=3")==1);
  assert(metadataReads==RiscPerf::data.counts[RiscPerf::MetadataReadBegin] && metadataParses==RiscPerf::data.counts[RiscPerf::ParseBegin]);
  printf("Demand retention %-20s loads=%u starts=%u acquires=%u calls=%u fini=%u retained=%u PASS\n",mode.c_str(),loads,
    RiscPerf::data.counts[RiscPerf::ProviderStartBegin],RiscPerf::data.counts[RiscPerf::ProviderAcquireBegin],calls,finis,unsigned(retained));
  if(retained){fflush(stdout);std::_Exit(0);}
}
