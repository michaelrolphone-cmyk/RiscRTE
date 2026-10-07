#include "bootstrap/Runtime.h"
#include <RiscPlatformClockV1.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <string>
using namespace RiscBoot;
static Runtime* rt=nullptr;static std::string mode,app;static unsigned healthCalls=0,phase=0;
static bool namespaceMode=false,storageSafe=true;static unsigned finalGrant=0;
extern "C" unsigned test_update_phase(){return phase;}
extern "C" void test_update_next(){++phase;}
extern "C" void test_update_retain(){storageSafe=false;}
extern "C" const char* test_update_mode(){return mode.c_str();}
extern "C" void test_update_inspect(){
 char bytes[4096];uint32_t n=0;assert(rt->appCount()==1 && rt->appInventory(0,bytes,sizeof(bytes),&n));
 assert(n==app.size() && !memcmp(bytes,app.data(),n));assert(!rt->appInventory(1,bytes,sizeof(bytes),&n) && n==0);
 Runtime::UpdateApp selected;JsonDocument json;assert(parse(app.data(),app.size(),json));
 auto check=[&](bool expected){std::string s;serializeJson(json,s);assert(rt->appUpdate("test",s.data(),s.size(),selected)==expected);};
 json["version"]="1.0.1";check(true);assert(!strcmp(selected.elf,"default.elf") && !strcmp(selected.manifest,"app.json"));
 for(const char* bad:{"1.0.0","0.9.9","1.0.01","1.0.1-rc","4294967296.0.0"}){json["version"]=bad;check(false);}json["version"]="1.0.1";
 json["id"]="other";check(false);json["id"]="test";
 json["file_name"]="board.json";check(false);json["file_name"]="default.elf";
 json["entry"]="driver_main";check(false);json["entry"]="app_main";
 json["grants"].to<JsonArray>();check(false);json.remove("grants");
 auto requirements=json["requires"].as<JsonArray>();
 if(finalGrant){
   // Clock's unique requirement first matches the ninth/tenth grant after
   // eight/nine independently authorized KV namespaces. Exercise the actual
   // appUpdate matcher, not just boot admission, with ASan enabled by runner.
   check(true);auto last=requirements[1];last["capability"]="platform.bank-store";check(false);
   last["capability"]="platform.clock";check(true);requirements.remove(1);check(false);
   auto restored=requirements.add<JsonObject>();restored["capability"]="platform.clock";restored["api"]=1;check(true);
   for(unsigned ns=1;ns<=finalGrant;++ns){risc_runtime_capability_v1 g{};g.struct_size=sizeof(g);assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,ns,&g));assert(rt->release(&g));}
   risc_runtime_capability_v1 g{};g.struct_size=sizeof(g);assert(rt->acquire("platform.clock",1,0,&g));assert(rt->release(&g));
   assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,0,&g));
   return;
 }
 requirements.clear();
 if(namespaceMode){
   check(false); // Removing the admitted requirement cannot remove boot grants.
   auto req=requirements.add<JsonObject>();req["capability"]="storage.key-value";req["api"]=2;check(false);
   req["api"]=1;check(true);
   auto duplicate=requirements.add<JsonObject>();duplicate["capability"]="storage.key-value";duplicate["api"]=1;check(false);
   duplicate["api"]=2;check(false);requirements.remove(1);
   req["capability"]="platform.bank-store";check(false);req["capability"]="storage.key-value";check(true);
   // Validation leaves both owner-provisioned grants intact and keeps ID0 ambiguous.
   risc_runtime_capability_v1 one{},five{},ambiguous{};
   one.struct_size=sizeof(one);five.struct_size=sizeof(five);ambiguous.struct_size=sizeof(ambiguous);
   assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,1,&one));assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,5,&five));
   assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,0,&ambiguous));
   assert(rt->release(&one) && rt->release(&five));
 }else{
   auto req=requirements.add<JsonObject>();req["capability"]="platform.bank-store";req["api"]=1;check(false);
   requirements.clear();check(true);
 }
 const char* dup=R"({"id":"test","id":"other"})";assert(!rt->appUpdate("test",dup,strlen(dup),selected));
}
static bool owner(){return true;}
static bool health(risc_runtime_health_v1*){return true;}
static bool log(const char*){return true;}
static void delay(uint32_t){}
static bool confirm(){++healthCalls;return mode!="refuse";}
static bool safe(){return storageSafe;}
static int32_t get(void*,uint32_t,const char*,void*,uint32_t,uint32_t*){assert(false);return RISC_KEY_VALUE_IO;}
static int32_t put(void*,uint32_t,const char*,const void*,uint32_t){assert(false);return RISC_KEY_VALUE_IO;}
static const KeyValueBackend backend{nullptr,get,put};
static bool bindClock(Runtime& runtime){
 static const risc_platform_clock_api_v1 clock{1,sizeof(clock),nullptr,[](void*)->uint64_t{return 0;},[](void*,uint32_t){}};
 return runtime.registerPlatform("platform.clock",1,Runtime::Scope::Global,0,&clock);
}
int main(int argc,char** argv){assert(argc==2);std::string root=argv[1];
 std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
 for(unsigned namespaces:{0u,2u,9u,10u}){
   namespaceMode=namespaces!=0;finalGrant=namespaces>=9?namespaces-1:0;
   app=R"({"type":"application","id":"test","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[)";
   if(namespaces)app+=R"({"capability":"storage.key-value","api":1})";
   if(finalGrant)app+=R"(,{"capability":"platform.clock","api":1})";
   app+="]}";std::ofstream(root+"/app.json")<<app;
   std::string boot=R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"app.json","grants":[)";
   if(finalGrant){for(unsigned n=1;n<=finalGrant;++n){if(n>1)boot+=",";boot+="{\"capability\":\"storage.key-value\",\"api\":1,\"instance_id\":"+std::to_string(n)+"}";}boot+=R"(,{"capability":"platform.clock","api":1,"instance_id":0})";}
   else if(namespaces)boot+=R"({"capability":"storage.key-value","api":1,"instance_id":1},{"capability":"storage.key-value","api":1,"instance_id":5})";
   boot+="]}]}";std::ofstream(root+"/boot.json")<<boot;
   for(const char* test:{"exit","healthy","refuse","queued","retained"}){
     mode=test;healthCalls=phase=0;storageSafe=true;
     Runtime runtime({owner,health,delay,log,bindClock,&backend,safe,safe,confirm});rt=&runtime;
     unsigned inspected=0;
     auto inspect=[](void* context,const char* path,bool driver){assert(!driver && strstr(path,"/default.elf"));++*static_cast<unsigned*>(context);return true;};
     assert(!runtime.inspectImages(inspect,&inspected) && inspected==0);
     assert(!runtime.confirmBoot());assert(runtime.prepare(root.c_str()));
     assert(runtime.inspectImages(inspect,&inspected) && inspected==1); // default and its policy share one image
     assert(!runtime.inspectImages(nullptr,nullptr));
     assert(!runtime.inspectImages([](void*,const char*,bool){return false;},nullptr));
     assert(runtime.run()!=(mode=="retained"));assert(!runtime.confirmBoot());
     assert(!runtime.inspectImages(inspect,&inspected) && inspected==1);
     assert(healthCalls==((mode=="exit" || mode=="retained")?0:1));
   }
 }
 puts("Default-entry health, child/queued/retained refusal, and immutable multi-namespace app-update admission PASS");
}
