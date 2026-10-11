#include "bootstrap/Runtime.h"
#include <RiscPlatformClockV1.h>
#include <cassert>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <cstring>
using namespace RiscBoot;
static bool owned=true,safe=true,retaining=false;static unsigned phase=0,reads=0,writes=0;
static unsigned capacity=0,indexMode=0;
static void* allocations[8]{};
static size_t allocationCount=0,allocationBytes=0;
static bool allocationFailure=false;
namespace RiscBoot {
void* metadataTestAllocate(size_t n){
 if(allocationFailure)return nullptr;
 assert(allocationCount<8);void* p=std::malloc(n);assert(p);
 allocations[allocationCount++]=p;allocationBytes=n;return p;
}
}
extern "C" void __real_free(void*);
extern "C" void __wrap_free(void* p){
 for(size_t i=0;i<allocationCount;++i)if(allocations[i]==p){allocations[i]=allocations[--allocationCount];break;}
 __real_free(p);
}
extern "C" unsigned multi_index_mode(){return indexMode;}
extern "C" unsigned multi_capacity(){return capacity;}
static risc_key_value_v1 saved{};
static std::map<uint32_t,std::map<std::string,std::vector<unsigned char>>> values;
extern "C" void multi_owner(int v){owned=v;}
extern "C" int multi_phase(){return phase;}
extern "C" void multi_next(){++phase;}
extern "C" int multi_retaining(){return retaining;}
extern "C" void multi_retain(){safe=false;}
extern "C" void multi_keep(risc_key_value_v1 api){saved=api;}
static bool owner(){return owned;}
static int32_t get(void*,uint32_t ns,const char* key,void* p,uint32_t cap,uint32_t* size){
 ++reads;assert((ns>=1&&ns<=capacity)||ns==1||ns==5);auto n=values.find(ns);if(n==values.end()||!n->second.count(key))return RISC_KEY_VALUE_NOT_FOUND;
 auto& bytes=n->second[key];*size=bytes.size();if(cap<bytes.size())return RISC_KEY_VALUE_BUFFER_SMALL;memcpy(p,bytes.data(),bytes.size());return 0;
}
static int32_t put(void*,uint32_t ns,const char* key,const void* p,uint32_t n){++writes;assert((ns>=1&&ns<=capacity)||ns==1||ns==5);const auto* b=static_cast<const unsigned char*>(p);values[ns][key]=std::vector<unsigned char>(b,b+n);return 0;}
static KeyValueBackend backend{nullptr,get,put};
static bool bindClock(Runtime& runtime){
 static const risc_platform_clock_api_v1 clock{1,sizeof(clock),nullptr,[](void*)->uint64_t{return 0;},[](void*,uint32_t){}};
 return runtime.registerPlatform("platform.clock",1,Runtime::Scope::Global,0,&clock);
}
static bool bindLastClock(Runtime& runtime){
 static const risc_platform_clock_api_v1 clock{1,sizeof(clock),nullptr,[](void*)->uint64_t{return 12345;},[](void*,uint32_t){}};
 for(unsigned i=0;i<31;i++)if(!runtime.registerPlatform("platform.index-fixture",1,Runtime::Scope::Device,i+1,&clock))return false;
 if(!runtime.registerPlatform("platform.clock",1,Runtime::Scope::Global,0,&clock))return false;
 assert(!runtime.registerPlatform("platform.extra",1,Runtime::Scope::Device,99,&clock));return true;
}
static Port port(){return {owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},indexMode==1?bindLastClock:bindClock,&backend,[](){return safe;}};}
static std::string grant(unsigned ns,const char* cap=RISC_KEY_VALUE_CAPABILITY,unsigned api=1){return "{\"capability\":\""+std::string(cap)+"\",\"api\":"+std::to_string(api)+",\"instance_id\":"+std::to_string(ns)+"}";}
static std::string requirement(const char* cap=RISC_KEY_VALUE_CAPABILITY,unsigned api=1){return "{\"capability\":\""+std::string(cap)+"\",\"api\":"+std::to_string(api)+"}";}
int main(int argc,char** argv){
 assert(argc==2);std::string root=argv[1];
 auto write=[&](const char* path,const std::string& s){std::ofstream(root+"/"+path)<<s;};
 auto manifest=[&](const char* id,const char* file,const std::string& req){return "{\"type\":\"application\",\"id\":\""+std::string(id)+"\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"file_name\":\""+file+"\",\"entry\":\"app_main\",\"requires\":["+req+"]}";};
 write("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"kv-test","revision":"test","buses":[],"devices":[]})");
 write("child.json",manifest("child","child.elf",requirement()));
 auto stage=[&](const std::string& grants,const std::string& req){
  write("default.json",manifest("default","default.elf",req));
  write("boot.json","{\"board\":\"board.json\",\"default_app\":\"default.elf\",\"drivers\":[],\"app_capabilities\":[{\"manifest\":\"default.json\",\"grants\":["+grants+"]},{\"manifest\":\"child.json\",\"grants\":["+grant(1)+"]}]}");
 };
 unsigned cases=0;
 auto check=[&](const std::string& grants,const std::string& req,bool expected){stage(grants,req);Runtime r(port());bool actual=r.prepare(root.c_str());if(actual!=expected)fprintf(stderr,"unexpected admission %s\n",r.error());assert(actual==expected);if(!expected){assert(!r.run());stage(grant(1),requirement());assert(!r.prepare(root.c_str()));assert(!r.run());}++cases;};
 check(grant(1)+","+grant(5),requirement(),true);
 check(grant(5)+","+grant(1),requirement(),true);
 check(grant(1)+","+grant(1),requirement(),false);
 check(grant(1)+","+grant(0),requirement(),false);
 check(grant(1)+","+grant(5),requirement()+","+requirement(),false);
 check(grant(1),"",false);
 check("",requirement(),false);
 check(grant(1)+","+grant(5,"undeclared"),requirement(),false);
 check(grant(1)+","+grant(5,RISC_KEY_VALUE_CAPABILITY,2),requirement(),false);
 check(grant(0,"platform.clock"),requirement("platform.clock"),true);
 check(grant(0,"platform.clock")+","+grant(0,"platform.clock"),requirement("platform.clock"),false);
 check(grant(1)+","+grant(0x7fffffffu),requirement(),true);
 check(grant(1)+","+grant(0x80000000u),requirement(),false);
 for(const char* invalid:{"-1","1.5","\"5\""}){
  std::string row=grant(5);row.replace(row.rfind(":5"),2,":"+std::string(invalid));check(grant(1)+","+row,requirement(),false);
 }
 check(grant(1)+","+grant(5),requirement()+","+requirement(RISC_KEY_VALUE_CAPABILITY,2),false);
 check(grant(1,RISC_BOUND_KEY_VALUE_CAPABILITY),requirement(RISC_BOUND_KEY_VALUE_CAPABILITY),false);
 std::string many;
 for(unsigned i=1;i<=Runtime::MaxAppPolicyGrants;++i){
  if(i>1)many+=",";
  many+=grant(i);
  if(i>=8)check(many,requirement(),true);
 }
 check(many+","+grant(Runtime::MaxAppPolicyGrants+1),requirement(),false);
 check(many+","+grant(1),requirement(),false);
 // The newly usable final slot keeps the same duplicate and type rejection.
 std::string prefix;for(unsigned i=1;i<Runtime::MaxAppPolicyGrants;++i){if(i>1)prefix+=",";prefix+=grant(i);}
 check(prefix+","+grant(1),requirement(),false);
 check(prefix+","+grant(0),requirement(),false);
 check(prefix+","+grant(12,"undeclared"),requirement(),false);
 check(prefix+","+grant(12,RISC_BOUND_KEY_VALUE_CAPABILITY),requirement(),false);
 check(prefix+","+grant(12,RISC_KEY_VALUE_CAPABILITY,2),requirement(),false);
 assert(!allocationCount);
 stage(many,requirement());allocationFailure=true;
 {Runtime r(port());assert(!r.prepare(root.c_str()));assert(!strcmp(r.error(),"app policy allocation failed"));assert(!r.run());}
 allocationFailure=false;assert(!allocationCount);
 {Runtime r(port());assert(r.prepare(root.c_str()));assert(allocationCount==1);
  printf("Policy rows=%zu Runtime bytes=%zu metadata per app=%zu\n",Runtime::MaxAppPolicyGrants,sizeof(Runtime),allocationBytes/2);}
 assert(!allocationCount);++cases;
 for(unsigned run=0;run<3;++run){
  stage(run&1?grant(5)+","+grant(1):grant(1)+","+grant(5),requirement());
  owned=safe=true;retaining=run==2;phase=0;values.clear();reads=writes=0;
  Runtime r(port());assert(r.prepare(root.c_str()));assert(r.run()!=retaining);
  assert(values[1]["same"]==std::vector<unsigned char>({'o','n','e'}));assert(values[5]["same"]==std::vector<unsigned char>({'f','i','v','e'}));
  assert(writes==2&&reads>=2);uint32_t size=999;char bytes[8];
  assert(saved.get(saved.context,"same",bytes,sizeof(bytes),&size)==RISC_KEY_VALUE_CONTEXT&&!size);
  assert(saved.put(saved.context,"same","bad",3)==RISC_KEY_VALUE_CONTEXT);++cases;
 }
 for(unsigned limit:{9u,12u,13u,14u,15u,16u,17u}) {
  if(limit>Runtime::MaxAppPolicyGrants)continue;
  capacity=limit;owned=safe=true;retaining=false;values.clear();reads=writes=0;
  std::string declared;for(unsigned i=1;i<=limit;++i){if(i>1)declared+=",";declared+=grant(i);}
  stage(declared,requirement());Runtime r(port());assert(r.prepare(root.c_str()));
  std::vector<std::string> parsedStorageChurn(256,std::string(4096,'X'));
  assert(!r.prepare(root.c_str()));assert(r.run());
  assert(reads==limit&&writes==limit);
  for(unsigned ns=1;ns<=limit;++ns){unsigned got=0;assert(values[ns]["slot"].size()==sizeof(got));memcpy(&got,values[ns]["slot"].data(),sizeof(got));assert(got==ns);}
  if(limit==17){unsigned value=0;uint32_t n=99;assert(saved.get(saved.context,"slot",&value,sizeof(value),&n)==RISC_KEY_VALUE_CONTEXT && !n);}
  ++cases;
 }
 // Admit sixteen declarations; reject a seventeenth with only sixteen grants.
 static_assert(Runtime::MaxAppRequirements==16,"Manifest requirement bound remains sixteen");
 static_assert(Runtime::MaxAppPolicyGrants==16 || Runtime::MaxAppPolicyGrants==17,"Declared policy bound");
 for(unsigned count:{12u,13u,14u,15u,16u,17u}) {
  std::string requirements,grants,drivers;
  for(unsigned i=0;i<count;++i){const std::string cap=i==16?"platform.clock":"test.cap"+std::to_string(i),id="cap"+std::to_string(i);
   if(i){requirements+=",";if(i<16)drivers+=",";}
   requirements+=requirement(cap.c_str());
   if(i<16){if(!grants.empty())grants+=",";grants+=grant(0,cap.c_str());}
   if(i==16)continue;
   drivers+="{\"manifest\":\""+id+".json\"}";
   write((id+".json").c_str(),"{\"type\":\"driver\",\"id\":\""+id+"\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"file_name\":\"default.elf\",\"driver_abi\":2,\"requires\":[],\"provides\":["+requirement(cap.c_str())+"]}");
  }
  write("default.json",manifest("default","default.elf",requirements));
  write("boot.json","{\"board\":\"board.json\",\"default_app\":\"default.elf\",\"drivers\":["+drivers+"],\"app_capabilities\":[{\"manifest\":\"default.json\",\"grants\":["+grants+"]}]}");
  Runtime r(port());const bool accepted=r.prepare(root.c_str());if(accepted!=(count<=Runtime::MaxAppRequirements))fprintf(stderr,"requirement capacity: %s\n",r.error());assert(accepted==(count<=Runtime::MaxAppRequirements));if(count>Runtime::MaxAppRequirements)assert(!strcmp(r.error(),"invalid app identity/declarations"));++cases;
 }
 // Sixteen unique requirements need seventeen rows when one requirement has
 // two distinct namespaces. Default rejects; opt-in admits the exact boundary.
 {
  std::string requirements=requirement(),grants=grant(1)+","+grant(5),drivers;
  for(unsigned i=0;i<15;++i){const std::string cap="test.cap"+std::to_string(i),id="cap"+std::to_string(i);
   requirements+=","+requirement(cap.c_str());grants+=","+grant(0,cap.c_str());
   if(i)drivers+=",";
   drivers+="{\"manifest\":\""+id+".json\"}";
  }
  write("default.json",manifest("default","default.elf",requirements));
  write("boot.json","{\"board\":\"board.json\",\"default_app\":\"default.elf\",\"drivers\":["+drivers+"],\"app_capabilities\":[{\"manifest\":\"default.json\",\"grants\":["+grants+"]}]}");
  {Runtime r(port());assert(r.prepare(root.c_str())==(Runtime::MaxAppPolicyGrants==17));}
  ++cases;
 }
 capacity=0;indexMode=1;owned=safe=true;
 stage(grant(0,"platform.clock"),requirement("platform.clock"));
 {Runtime r(port());assert(r.prepare(root.c_str()) && r.run());++cases;}
 indexMode=2;std::string driverList;
 for(unsigned i=0;i<16;i++){
  const std::string name="slot"+std::to_string(i),cap="test."+name;
  write((name+".json").c_str(),"{\"type\":\"driver\",\"id\":\""+name+"\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"file_name\":\""+name+".elf\",\"driver_abi\":2,\"requires\":[],\"provides\":["+requirement(cap.c_str())+"]}");
  if(i)driverList+=",";
  driverList+="{\"manifest\":\""+name+".json\"}";
 }
 write("default.json",manifest("default","default.elf",requirement("test.slot15")));
 write("boot.json","{\"board\":\"board.json\",\"default_app\":\"default.elf\",\"drivers\":["+driverList+"],\"app_capabilities\":[{\"manifest\":\"default.json\",\"grants\":["+grant(0,"test.slot15")+"]}]}");
 {Runtime r(port());assert(r.prepare(root.c_str()) && r.run());++cases;}
 indexMode=0;
 capacity=0;
 assert(!allocationCount);
 printf("Multiple explicit KV namespaces: %u admission/lifecycle/owner/isolation/retention cases PASS\n",cases);
}
