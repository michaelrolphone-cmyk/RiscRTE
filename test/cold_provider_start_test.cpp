#include "bootstrap/Runtime.h"
#include "diagnostics/StageLog.h"
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
using RiscBoot::Runtime;
static std::string mode,root;
static std::vector<std::string> events,lines;
static bool owned=true,nativeSafe=true,cold=true;
static unsigned classified=0,calls=0,finis=0;
static unsigned count(const char* event){return std::count(events.begin(),events.end(),event);}
static bool partial(){return mode=="partial-failed" || mode=="partial-retained";}
static bool promote(){return mode=="promotion" || mode=="deep-promotion";}
static bool appGrant(){return mode=="reuse" || mode=="deep-acquire";}
static bool owner(){return owned;}
static bool coldBoot(){++classified;if(mode=="classifier-owner")owned=false;return cold;}
namespace RiscDiagnostics {
uint64_t monotonicUs(){return 0;}
void timestamped(const char* format,...){char line[512];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);lines.emplace_back(line);}
}
extern "C" bool demand_event(const char* id,const char* event){
 events.emplace_back(std::string(id)+":"+event);
 if(!strcmp(event,"start")){
  // No app API or app acquire becomes live during native boot activation.
  assert(bool(risc_runtime_get_api(1))==(mode=="deep-acquire"));
  if(!strcmp(id,"leaf")){
   if(mode=="native-retained")nativeSafe=false;
   if(mode=="failed-start" || mode=="failed-start-retained")return false;
  }
  if(!strcmp(id,"unused") && partial())return false;
 }
 if(!strcmp(event,"quiesce")){
  if(!strcmp(id,"leaf") && (mode=="failed-start-retained" || mode=="failed-release-retained"))return false;
  if(!strcmp(id,"unused") && mode=="partial-retained")return false;
 }
 return true;
}
extern "C" void demand_init(){}
extern "C" void demand_fini(){++finis;events.emplace_back("app:fini");}
extern "C" void demand_app(){
 ++calls;events.emplace_back("app:entry");const auto* api=risc_runtime_get_api(1);assert(api);
 const bool started=mode=="eager" || (cold && mode!="no-option");
 assert(count("leaf:start")==unsigned(started));
 if(promote()){
  risc_runtime_capability_v1 control{sizeof(control)};
  assert(api->acquire(RISC_PROVIDER_PROMOTION_CAPABILITY,1,0,&control));
  const auto& table=*static_cast<const risc_provider_promotion_api_v1*>(control.api);
  assert(table.promote(table.context)==RISC_PROVIDER_PROMOTION_OK);
  assert(table.promote(table.context)==RISC_PROVIDER_PROMOTION_ALREADY_READY);
  assert(api->release(&control));assert(count("leaf:start")==unsigned(started));
 }
 risc_runtime_capability_v1 grant{sizeof(grant)};
 if(appGrant()){
  assert(api->acquire("test.leaf",1,0,&grant));assert(count("leaf:start")==1);
  assert(api->release(&grant));
  if(started){assert(!count("leaf:stop") && !count("root:stop"));}
  else assert(count("leaf:stop")==1 && count("root:stop")==1);
 }else assert(!api->acquire("test.leaf",1,0,&grant));
 for(unsigned i=0;i<10;++i)api->yield_ms(1);
 if(mode=="handoff" && calls==1)assert(api->request_launch("child.elf"));
}
static void file(const char* path,const std::string& value){std::ofstream(root+"/"+path)<<value;}
static void save(const char* path,const JsonDocument& doc){std::string text;serializeJson(doc,text);file(path,text);}
static JsonDocument read(const char* path){JsonDocument doc;assert(RiscBoot::readJson((root+"/"+path).c_str(),doc));return doc;}
static void stage(){
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 JsonDocument boot;boot["board"]="board.json";boot["default_app"]="default.elf";
 boot["provider_activation"]=mode=="eager"?"eager":mode=="cold-demand"?"demand":"demand-retained";
 auto selections=boot["drivers"].to<JsonArray>();
 for(const char* id:{"leaf","unused","root"}){
  JsonDocument provider;provider["type"]="driver";provider["id"]=id;provider["version"]="1.0.0";
  provider["driver_abi"]=2;provider["architecture"]="xtensa-esp32s3";provider["file_name"]=std::string(id)+".elf";
  auto req=provider["requires"].to<JsonArray>();
  if(!strcmp(id,"leaf")){auto dep=req.add<JsonObject>();dep["capability"]="test.root";dep["api"]=1;}
  auto cap=provider["provides"].to<JsonArray>().add<JsonObject>();cap["capability"]=std::string("test.")+id;cap["api"]=1;
  const std::string name=std::string(id)+".json";save(name.c_str(),provider);
  auto selected=selections.add<JsonObject>();selected["manifest"]=name;
  if(mode!="no-option" && mode!="eager" && (!strcmp(id,"leaf") || (!strcmp(id,"unused") && partial())))selected["boot_start"]="cold";
 }
 if(promote() || appGrant()){
  JsonDocument app;app["type"]="application";app["id"]="cold-test";app["version"]="1.0.0";app["architecture"]="xtensa-esp32s3";
  app["file_name"]="default.elf";app["entry"]="app_main";
  const char* capability=promote()?RISC_PROVIDER_PROMOTION_CAPABILITY:"test.leaf";
  auto req=app["requires"].to<JsonArray>().add<JsonObject>();req["capability"]=capability;req["api"]=1;
  auto policy=boot["app_capabilities"].to<JsonArray>().add<JsonObject>();policy["manifest"]="app.json";
  auto grant=policy["grants"].to<JsonArray>().add<JsonObject>();grant["capability"]=capability;grant["api"]=1;grant["instance_id"]=0;
  save("app.json",app);
 }
 save("boot.json",boot);
}
static void admission(RiscBoot::Port port){
 const auto original=read("boot.json");
 for(const char* invalid:{"null","true","false","1","{}","[]","\"Cold\"","\"\"","\"eager\""}){
  auto boot=original;JsonDocument value;assert(!deserializeJson(value,invalid));boot["drivers"][0]["boot_start"].set(value);save("boot.json",boot);
  Runtime r(port);assert(!r.prepare(root.c_str()) && !r.run());
 }
 for(const char* activation:{"eager","omitted"}){
  auto boot=original;boot["drivers"][0]["boot_start"]="cold";
  if(!strcmp(activation,"omitted"))boot.remove("provider_activation");else boot["provider_activation"]=activation;
  save("boot.json",boot);Runtime r(port);assert(!r.prepare(root.c_str()));
 }
 save("boot.json",original);
 auto unused=read("unused.json"),bad=unused;
 auto dep=bad["requires"].as<JsonArray>().add<JsonObject>();dep["capability"]="test.absent";dep["api"]=1;
 save("unused.json",bad);{Runtime r(port);assert(!r.prepare(root.c_str()));}save("unused.json",unused);
 auto board=read("board.json");file("board.json","{}");{Runtime r(port);assert(!r.prepare(root.c_str()));}save("board.json",board);
 assert(events.empty() && !classified);
}
int main(int argc,char** argv){
 assert(argc==3);root=argv[1];mode=argv[2];cold=mode.find("deep")!=0;
 RiscBoot::Port port{owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;}};
 port.appExitSafe=[](){return nativeSafe;};port.coldBoot=coldBoot;
 if(mode=="missing-classifier")port.coldBoot=nullptr;
 stage();admission(port);
 Runtime runtime(port);assert(runtime.prepare(root.c_str()));
 std::vector<std::string> images;
 assert(runtime.inspectImages([](void* context,const char* path,bool){static_cast<std::vector<std::string>*>(context)->emplace_back(path);return true;},&images));
 assert(images.size()==4 && images[2]==root+"/unused.elf");
 assert(!runtime.inspectImages([](void*,const char* path,bool){return !strstr(path,"unused.elf");},nullptr));
 assert(events.empty() && !classified);
 if(mode=="wrong-owner")owned=false;
 const bool retained=mode=="failed-start-retained" || mode=="failed-release-retained" || mode=="native-retained" || mode=="partial-retained";
 const bool rejected=retained || mode=="failed-start" || mode=="partial-failed" || mode=="missing-classifier" || mode=="wrong-owner" || mode=="classifier-owner";
 assert(runtime.run()==!rejected);assert(runtime.retained()==retained);assert(!risc_runtime_get_api(1));
 const bool unclassified=mode=="no-option" || mode=="eager" || mode=="missing-classifier" || mode=="wrong-owner";
 assert(classified==unsigned(!unclassified));owned=true;
 if(mode=="missing-classifier" || mode=="wrong-owner" || mode=="classifier-owner")assert(events.empty());
 else {
  const bool started=mode=="eager" || (cold && mode!="no-option") || mode=="deep-acquire";
  assert(count("leaf:start")==unsigned(started) && count("root:start")==unsigned(started));
  assert(count("unused:start")==unsigned(partial() || mode=="eager"));
  assert(calls==(rejected && mode!="failed-release-retained"?0u:mode=="handoff"?3u:1u));
  assert(finis==calls);
  if(started && cold)assert(events[0]=="root:start" && events[1]=="leaf:start");
  if(retained){assert(!count("leaf:stop") && !count("root:stop"));assert(!runtime.run());}
  else if(started)assert(count("leaf:stop")==1 && count("root:stop")==1);
  else assert(!count("leaf:stop") && !count("root:stop"));
 }
 printf("Cold provider start %s: PASS\n",mode.c_str());
 if(retained){fflush(stdout);std::_Exit(0);}
}
