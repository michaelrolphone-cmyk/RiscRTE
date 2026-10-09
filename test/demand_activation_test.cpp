#include "bootstrap/Runtime.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
using RiscBoot::Runtime;
static std::vector<std::string> events;
static std::string mode;
static bool startOk=true,stopOk=true,nativeSafe=true,owned=true;
static bool promotionMode=false, insidePromotion=false;
static risc_provider_promotion_api_v1 savedPromotion{};
static risc_runtime_capability_v1 promotionGrant{};
static unsigned calls=0,finis=0;
extern "C" bool demand_event(const char* id,const char* event){
  events.emplace_back(std::string(id)+":"+event);
  if(promotionMode && insidePromotion && !strcmp(event,"start")) {
    assert(savedPromotion.promote(savedPromotion.context)==RISC_PROVIDER_PROMOTION_BUSY);
    assert(!risc_runtime_get_api(1)->release(&promotionGrant));
    assert(!risc_runtime_get_api(1)->request_launch("child.elf"));
  }
  if(promotionMode && !strcmp(id,"unused") && !strcmp(event,"start")) {
    if(mode=="promote-native")nativeSafe=false;
    if(mode=="promote-retry" || mode=="promote-retained")return startOk;
  }
  if(promotionMode && mode=="promote-retained" && !strcmp(id,"unused") && !strcmp(event,"quiesce"))return false;
  return strcmp(id,"leaf") || (strcmp(event,"start")?stopOk:startOk);
}
static unsigned count(const char* event){return std::count(events.begin(),events.end(),event);}
static bool acquire(risc_runtime_capability_v1& grant){
  grant.struct_size=sizeof(grant);return risc_runtime_get_api(1)->acquire("test.leaf",1,0,&grant);
}
extern "C" void demand_init(){
  risc_runtime_capability_v1 denied{};denied.struct_size=sizeof(denied);
  assert(!risc_runtime_get_api(1)->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&denied));
  if(savedPromotion.promote)assert(savedPromotion.promote(savedPromotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);
}
extern "C" void demand_fini(){
  if(savedPromotion.promote)assert(savedPromotion.promote(savedPromotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);
  ++finis;events.emplace_back("app:fini");}
extern "C" void demand_app(){
  ++calls;events.emplace_back("app:entry");
  const auto* api=risc_runtime_get_api(1);assert(api);
  if(!promotionMode) {
    risc_runtime_capability_v1 denied{};denied.struct_size=sizeof(denied);
    assert(!api->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&denied));
  }
  if(mode=="demand")return;
  if(promotionMode) {
    if(calls==2) {
      assert(savedPromotion.promote(savedPromotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);
      risc_runtime_capability_v1 denied{};denied.struct_size=sizeof(denied);
      assert(!api->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&denied));return;
    }
    if(savedPromotion.promote)assert(savedPromotion.promote(savedPromotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);
    risc_runtime_capability_v1 display{};assert(acquire(display));
    assert(count("unused:start")==unsigned(mode=="promote-eager" || calls==3));
    promotionGrant={};promotionGrant.struct_size=sizeof(promotionGrant);
    assert(api->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&promotionGrant));
    savedPromotion=*static_cast<const risc_provider_promotion_api_v1*>(promotionGrant.api);
    owned=false;assert(savedPromotion.promote(savedPromotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);owned=true;
    nativeSafe=false;assert(savedPromotion.promote(savedPromotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);nativeSafe=true;
    assert(savedPromotion.promote(reinterpret_cast<void*>(uintptr_t(savedPromotion.context)+1))==RISC_PROVIDER_PROMOTION_CONTEXT);
    if(mode=="promote-pending") {
      stopOk=false;assert(!api->release(&display));
      assert(savedPromotion.promote(savedPromotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);
      stopOk=true;assert(api->release(&display));assert(acquire(display));
    }
    if(mode=="promote-retry" || mode=="promote-retained")startOk=false;
    insidePromotion=true;int status=savedPromotion.promote(savedPromotion.context);insidePromotion=false;
    if(mode=="promote-native" || mode=="promote-retained") {
      assert(status==RISC_PROVIDER_PROMOTION_RETAINED);
      // Promotion already fenced this invocation. The explicit terminal
      // signal is idempotent and must not trigger cleanup or regain authority.
      assert(api->retain_invocation() && api->retain_invocation());
      assert(!risc_runtime_get_api(1));
      assert(savedPromotion.promote(savedPromotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);return;
    }
    if(mode=="promote-retry") {
      assert(status==RISC_PROVIDER_PROMOTION_FAILED);assert(count("leaf:stop")==0);
      startOk=true;insidePromotion=true;status=savedPromotion.promote(savedPromotion.context);insidePromotion=false;
    }
    assert(status==((mode=="promote-eager" || calls==3)?RISC_PROVIDER_PROMOTION_ALREADY_READY:RISC_PROVIDER_PROMOTION_OK));
    assert(savedPromotion.promote(savedPromotion.context)==RISC_PROVIDER_PROMOTION_ALREADY_READY);
    assert(api->release(&display));
    assert(count("leaf:stop")==unsigned(mode=="promote-pending"));
    auto stale=savedPromotion;assert(api->release(&promotionGrant));
    assert(stale.promote(stale.context)==RISC_PROVIDER_PROMOTION_CONTEXT);
    promotionGrant.struct_size=sizeof(promotionGrant);assert(api->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&promotionGrant));
    savedPromotion=*static_cast<const risc_provider_promotion_api_v1*>(promotionGrant.api);
    assert(stale.promote(stale.context)==RISC_PROVIDER_PROMOTION_CONTEXT);
    if(mode=="promote-stop-retained")stopOk=false;
    if(mode=="promote-handoff" && calls==1)assert(api->request_launch("child.elf"));
    return;
  }
  if(mode=="handoff" && calls==2){
    risc_runtime_capability_v1 denied{};assert(!acquire(denied));
    assert(api->request_launch("missing.elf"));return;
  }
  risc_runtime_capability_v1 first{},second{};
  if(mode=="retry" || mode=="failed-start-retained"){
    startOk=false;stopOk=mode=="retry";assert(!acquire(first));
    if(mode=="failed-start-retained") {
      // Failed activation fences this invocation immediately. API lookup is
      // gone, and a table saved before retention must reject new authority.
      assert(!risc_runtime_get_api(1));
      assert(!api->acquire("test.leaf",1,0,&first));
      return;
    }
    assert(count("root:stop")==1 && count("leaf:stop")==1);startOk=true;
  }
  assert(acquire(first));
  assert(acquire(second));assert(first.api==second.api);
  assert(api->release(&second));
  if(mode=="retained" || mode=="stop-retry"){
    stopOk=false;assert(!api->release(&first));
    assert(count("leaf:stop")==0 && count("root:stop")==0);
    if(mode=="retained")return;
    stopOk=true;assert(api->release(&first));assert(!api->release(&first));
    assert(acquire(first));
  }
  if(mode=="native-retained")nativeSafe=false;
  if(mode=="handoff" && calls==1)assert(api->request_launch("child.elf"));
  // Automatic app revocation owns this remaining grant.
}
static void write(const std::string& root,const char* path,const std::string& value){std::ofstream(root+"/"+path)<<value;}
static void save(const std::string& root,const char* path,const JsonDocument& doc){std::string s;serializeJson(doc,s);write(root,path,s);}
static const char* board=R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
int main(int argc,char** argv){
  assert(argc==3);const std::string root=argv[1];mode=argv[2];promotionMode=mode.find("promote-")==0;
  write(root,"board.json",board);
  JsonDocument boot;boot["board"]="board.json";boot["default_app"]="default.elf";
  if(mode!="omitted")boot["provider_activation"]=(mode=="eager" || mode=="promote-eager")?"eager":"demand";
  auto drivers=boot["drivers"].to<JsonArray>();
  for(const char* id:{"leaf","unused","root"}){
    JsonDocument m;m["type"]="driver";m["id"]=id;m["version"]="1.0.0";m["driver_abi"]=2;
    m["architecture"]="xtensa-esp32s3";m["file_name"]=std::string(id)+".elf";
    auto req=m["requires"].to<JsonArray>();if(!strcmp(id,"leaf")){auto r=req.add<JsonObject>();r["capability"]="test.root";r["api"]=1;}
    auto cap=m["provides"].to<JsonArray>().add<JsonObject>();cap["capability"]=std::string("test.")+id;cap["api"]=1;
    auto filename=std::string(id)+".json";save(root,filename.c_str(),m);drivers.add<JsonObject>()["manifest"]=filename;
  }
  write(root,"app.json",R"({"type":"application","id":"demand-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.leaf","api":1}]})");
  auto policy=boot["app_capabilities"].to<JsonArray>().add<JsonObject>();policy["manifest"]="app.json";
  auto grant=policy["grants"].to<JsonArray>().add<JsonObject>();grant["capability"]="test.leaf";grant["api"]=1;grant["instance_id"]=0;
  if(promotionMode) {
    JsonDocument app;std::ifstream in(root+"/app.json");std::string json((std::istreambuf_iterator<char>(in)),{});assert(!deserializeJson(app,json));
    auto req=app["requires"].as<JsonArray>().add<JsonObject>();req["capability"]=RISC_PROVIDER_PROMOTION_CAPABILITY;req["api"]=1;save(root,"app.json",app);
    auto g=policy["grants"].as<JsonArray>().add<JsonObject>();g["capability"]=RISC_PROVIDER_PROMOTION_CAPABILITY;g["api"]=1;g["instance_id"]=0;
  }
  RiscBoot::Port port{[](){return owned;},[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}};
  port.appExitSafe=[](){return nativeSafe;};
  // Malformed policy and unused selected metadata still fail before execution.
  for(const char* invalid:{"null","true","1","\"Demand\"","{}","[]"}){
    JsonDocument bad;bad.set(boot);JsonDocument value;assert(!deserializeJson(value,invalid));bad["provider_activation"].set(value);save(root,"boot.json",bad);
    Runtime r(port);assert(!r.prepare(root.c_str()));assert(!r.run());assert(events.empty());
  }
  if(promotionMode) {
    JsonDocument bad;bad.set(boot);bad["default_app"]="child.elf";save(root,"boot.json",bad);
    Runtime r(port);assert(!r.prepare(root.c_str()));assert(events.empty());
    for(unsigned invalid=0;invalid<4;++invalid) {
      JsonDocument badGrant;badGrant.set(boot);
      auto grants=badGrant["app_capabilities"][0]["grants"].as<JsonArray>();
      if(invalid==0)grants[1]["api"]=2;
      if(invalid==1)grants[1]["instance_id"]=1;
      if(invalid==2)grants.add(grants[1]);
      if(invalid==3)grants[1]["capability"]="runtime.undeclared";
      save(root,"boot.json",badGrant);Runtime denied(port);assert(!denied.prepare(root.c_str()));assert(events.empty());
    }
  }
  save(root,"boot.json",boot);
  write(root,"unused.json","{}");{Runtime r(port);assert(!r.prepare(root.c_str()));assert(events.empty());}
  // Restore unused declaration, then reject a missing dependency in it.
  const char* unused=R"({"type":"driver","id":"unused","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"unused.elf","requires":[],"provides":[{"capability":"test.unused","api":1}]})";
  JsonDocument bad;assert(RiscBoot::parse(unused,strlen(unused),bad));
  auto missing=bad["requires"].as<JsonArray>().add<JsonObject>();missing["capability"]="test.missing";missing["api"]=1;save(root,"unused.json",bad);
  {Runtime r(port);assert(!r.prepare(root.c_str()));assert(events.empty());}
  write(root,"unused.json",unused);write(root,"board.json","{}");
  {Runtime r(port);assert(!r.prepare(root.c_str()));assert(events.empty());}
  write(root,"board.json",board);
  Runtime runtime(port);assert(runtime.prepare(root.c_str()));
  assert(!runtime.registerPlatform("test.extra",1,Runtime::Scope::Global,0,&runtime));
  std::vector<std::string> images;
  assert(runtime.inspectImages([](void* c,const char* path,bool){static_cast<std::vector<std::string>*>(c)->emplace_back(path);return true;},&images));
  assert(images.size()==4 && images[2]==root+"/unused.elf");
  assert(!runtime.inspectImages([](void*,const char* path,bool){return !strstr(path,"unused.elf");},nullptr));
  assert(events.empty());
  bool retained=mode=="promote-stop-retained" || mode=="promote-retained" || mode=="promote-native" || mode=="retained" || mode=="native-retained" || mode=="failed-start-retained";
  assert(runtime.run()!=retained);assert(runtime.retained()==retained);assert(!risc_runtime_get_api(1));
  if(promotionMode) {
    if(mode=="promote-retained" || mode=="promote-native")
      assert(strstr(runtime.error(), "provider promotion retained"));
    assert(count("unused:start")==unsigned(mode=="promote-retry"?2:1));
    if(mode=="promote-handoff")assert(calls==3 && finis==3 && count("leaf:start")==1);
    if(retained)assert(finis==unsigned(mode=="promote-stop-retained"));
    assert(savedPromotion.promote(savedPromotion.context)==RISC_PROVIDER_PROMOTION_CONTEXT);
  } else if(mode=="eager" || mode=="omitted"){
    assert(events[0]=="root:start" && events[1]=="leaf:start" && events[2]=="unused:start");
    assert(count("unused:start")==1 && count("leaf:start")==1);
  }else assert(!count("unused:start"));
  if(mode=="demand")assert(!count("root:start") && !count("leaf:start"));
  if(mode=="handoff")assert(calls==3 && finis==3 && count("leaf:start")==2);
  if(mode=="closure" || mode=="handoff") {
    assert(events[0]=="app:entry" && events[1]=="root:start" && events[2]=="leaf:start");
    assert(events[3]=="app:fini" && events[4]=="leaf:quiesce");
  }
  if(!retained){
    assert(count("root:start")==count("root:stop"));assert(count("leaf:start")==count("leaf:stop"));
    for(size_t i=0;i<events.size();++i)if(events[i]=="leaf:stop" && mode!="eager" && mode!="omitted" && !promotionMode)assert(i+2<events.size() && events[i+1]=="root:quiesce" && events[i+2]=="root:stop");
  }else{
    assert(!count("root:stop") && !count("leaf:stop"));
    if(mode=="native-retained")assert(finis==0);
    assert(!runtime.run());
  }
  printf("Demand activation %s: PASS\n",mode.c_str());
  // Static production owners retain uncertain modules until restart. Do not
  // invoke the graph destruction guard in intentionally retained child cases.
  if(retained){fflush(stdout);std::_Exit(0);}
}
