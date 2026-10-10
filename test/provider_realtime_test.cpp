#include "bootstrap/Runtime.h"
#include "bootstrap/KeyValueGeneration.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <new>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
using RiscBoot::Runtime;
static std::string root;
static bool owned=true,safe=true;
static unsigned reads,seeds,starts,stops,unloads,polls,lifecycle;
static int scenario,fault;
static risc_platform_realtime_api_v1 saved[2]{},previous{};
static risc_realtime_snapshot_v1 value{sizeof(value),RISC_REALTIME_UNSET,0,0,0,100,102};
static bool owner(){return owned;}
static bool safety(){return safe;}
static bool health(risc_runtime_health_v1*){return true;}
static bool logLine(const char*){return true;}
static void delay(uint32_t){}
static void deny(const risc_platform_realtime_api_v1& table){
 if(!table.read)return;
 risc_realtime_snapshot_v1 out;memset(&out,0xa5,sizeof(out));out.struct_size=sizeof(out);const auto before=out;const auto count=reads;
 assert(table.read(table.context,&out)==RISC_REALTIME_CONTEXT);
 assert(!memcmp(&before,&out,sizeof(out)) && reads==count);
}
static void live(const risc_platform_realtime_api_v1& table){
 risc_realtime_snapshot_v1 out{sizeof(out)};const auto count=reads;
 assert(table.read(table.context,&out)==0 && reads==count+1 && !memcmp(&out,&value,sizeof(out)));
}
static int32_t readTime(void*,risc_realtime_snapshot_v1* out){
 ++reads;*out=value;
 switch(fault){
 case 1:out->struct_size=0;break;case 2:out->validity=2;break;case 3:out->reserved=1;break;
 case 4:out->epoch_seconds=-1;break;case 5:out->epoch_seconds=INT64_MAX;break;
 case 6:out->nanoseconds=1000000000;break;case 7:out->monotonic_after_us=0;break;
 case 8:out->validity=RISC_REALTIME_UNSET;out->epoch_seconds=1;break;
 case 9:out->validity=RISC_REALTIME_UNSET;out->epoch_seconds=0;out->nanoseconds=1;break;
 case 10:return RISC_REALTIME_IO;
 }
 return RISC_REALTIME_OK;
}
static int32_t seedTime(void*,int64_t seconds,uint32_t nanos){++seeds;value.validity=1;value.epoch_seconds=seconds;value.nanoseconds=nanos;return 0;}
static const risc_realtime_control_api_v1 backend{1,sizeof(backend),nullptr,readTime,seedTime};
static int32_t get(void*,uint32_t ns,const char* key,void* data,uint32_t,uint32_t* size){assert(ns==1 && !strcmp(key,"k8"));*static_cast<char*>(data)=42;*size=1;return 0;}
static int32_t put(void*,uint32_t,const char*,const void*,uint32_t){assert(false);return -1;}
static const RiscBoot::KeyValueBackend kv{nullptr,get,put};
static bool bind(Runtime& r){return r.registerRealtime(&backend);}
extern "C" int test_provider_failure(unsigned index){return index==1?(scenario==4?1:scenario==5?2:0):(scenario==6?2:0);}
extern "C" void test_provider_event(unsigned index,const char* event,const risc_platform_realtime_api_v1* table){
 assert(index<2);
 if(!strcmp(event,"start")){
  ++starts;deny(previous);deny(*table);saved[index]=*table;
  if(index)assert(saved[0].context!=saved[1].context);
 }else if(!strcmp(event,"poll")){++polls;live(*table);}
 else {deny(*table);if(!strcmp(event,"stop"))++stops;if(!strcmp(event,"unload"))++unloads;}
}
extern "C" void test_provider_lifecycle(){++lifecycle;deny(saved[0]);deny(saved[1]);}
static risc_runtime_capability_v1 acquire(const risc_runtime_api_v1* api,unsigned index){
 risc_runtime_capability_v1 g{sizeof(g)};assert(api->acquire(index?"test.time.second":"test.time.first",1,0,&g));return g;
}
extern "C" void test_provider_app(){
 const auto* api=risc_runtime_get_api(1);assert(api);deny(previous);
 auto grant=acquire(api,0);live(saved[0]);
 if(previous.context)assert(previous.context!=saved[0].context);
 // No provider route, control or raw-time authority can be acquired implicitly.
 risc_runtime_capability_v1 control{sizeof(control)},bad{sizeof(bad)};
 assert(!api->acquire(RISC_PLATFORM_REALTIME_CAPABILITY,1,0,&bad));
 assert(!api->acquire(RISC_REALTIME_CAPABILITY,1,0,&bad));
 assert(api->acquire(RISC_REALTIME_CONTROL_CAPABILITY,1,0,&control));
 const auto* setter=static_cast<const risc_realtime_control_api_v1*>(control.api);
 const auto seedCount=seeds;
 assert(setter->seed(saved[0].context,123,0)==RISC_REALTIME_CONTEXT && seeds==seedCount);
 assert(setter->seed(setter->context,1800000000,123456000)==0);live(saved[0]);
 assert(api->release(&control));
 owned=false;deny(saved[0]);owned=true;live(saved[0]);
 auto invalid=saved[0];invalid.context=nullptr;deny(invalid);invalid.context=reinterpret_cast<void*>(UINTPTR_MAX);deny(invalid);
 risc_realtime_snapshot_v1 out{sizeof(out)},before=out;
 assert(saved[0].read(saved[0].context,nullptr)==RISC_REALTIME_INVALID);
 --out.struct_size;before=out;assert(saved[0].read(saved[0].context,&out)==RISC_REALTIME_INVALID && !memcmp(&out,&before,sizeof(out)));++out.struct_size;before=out;
 for(fault=1;fault<=10;++fault)assert(saved[0].read(saved[0].context,&out)==RISC_REALTIME_IO && !memcmp(&out,&before,sizeof(out)));
 fault=0;const auto polled=polls;api->yield_ms(1);assert(polls>polled);
 if(scenario==1){
  auto stale=saved[0];assert(api->release(&grant));deny(stale);
  grant=acquire(api,0);assert(saved[0].context!=stale.context);deny(stale);live(saved[0]);
 }
 if(scenario==2){safe=false;deny(saved[0]);safe=true;deny(saved[0]);safe=false;return;}
 if(scenario==3){assert(api->retain_invocation() && api->retain_invocation());deny(saved[0]);return;}
 if(scenario==4){
  assert(!api->acquire("test.time.second",1,0,&bad));deny(saved[0]);deny(saved[1]);return;
 }
 if(scenario==5){auto second=acquire(api,1);assert(!api->release(&second));deny(saved[0]);deny(saved[1]);return;}
 assert(api->release(&grant));
}
static void file(const char* name,const std::string& text){std::ofstream(root+"/"+name)<<text;}
static std::string driver(unsigned index,const char* capability=RISC_PLATFORM_REALTIME_CAPABILITY,unsigned version=1){
 return std::string(R"({"type":"driver","id":"time-)")+(index?"second":"first")+R"(","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"provider)"+std::to_string(index)+R"(.elf","requires":[{"capability":")"+capability+R"(","api":)"+std::to_string(version)+R"(}],"provides":[{"capability":"test.time.)"+(index?"second":"first")+R"(","api":1}]})";
}
static void stage(const char* appCap=RISC_REALTIME_CONTROL_CAPABILITY){
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 file("first.json",driver(0));file("second.json",driver(1));
 file("app.json",std::string(R"({"type":"application","id":"time-app","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.time.first","api":1},{"capability":"test.time.second","api":1},{"capability":")")+appCap+R"(","api":1}]})");
 file("boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","provider_activation":")")+(scenario==0 || scenario==6?"eager":"demand")+R"(","drivers":[{"manifest":"first.json"},{"manifest":"second.json"}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.time.first","api":1,"instance_id":0},{"capability":"test.time.second","api":1,"instance_id":0},{"capability":")"+appCap+R"(","api":1,"instance_id":0}]}]})");
}
static void boundKeys(unsigned count){
 JsonDocument boot,manifest;assert(RiscBoot::readJson((root+"/boot.json").c_str(),boot));assert(RiscBoot::readJson((root+"/first.json").c_str(),manifest));
 auto req=manifest["requires"].as<JsonArray>().add<JsonObject>();req["capability"]=RISC_BOUND_KEY_VALUE_CAPABILITY;req["api"]=1;
 auto map=boot["drivers"][0]["key_value"].to<JsonArray>();
 for(unsigned i=0;i<count;++i){auto key=map.add<JsonObject>();key["key"]="k"+std::to_string(i);key["namespace"]=1;key["access"]="read";}
 std::string text;serializeJson(boot,text);file("boot.json",text);text.clear();serializeJson(manifest,text);file("first.json",text);
}
static RiscBoot::Port port(){return {owner,health,delay,logLine,bind,&kv,safety,safety};}
static void admission(){
 stage();const auto before=reads+seeds;
 {Runtime r({owner,health,delay,logLine});assert(!r.prepare(root.c_str()));}
 for(unsigned version:{0u,2u}){stage();file("first.json",driver(0,RISC_PLATFORM_REALTIME_CAPABILITY,version));Runtime r(port());assert(!r.prepare(root.c_str()));}
 for(const char* cap:{RISC_REALTIME_CAPABILITY,RISC_REALTIME_CONTROL_CAPABILITY}){stage();file("first.json",driver(0,cap));Runtime r(port());assert(!r.prepare(root.c_str()));}
 stage(RISC_PLATFORM_REALTIME_CAPABILITY);{Runtime r(port());assert(!r.prepare(root.c_str()));}
 stage();boundKeys(11);{Runtime r(port());assert(!r.prepare(root.c_str()));}
 stage();{Runtime r(port());assert(r.registerRealtime(&backend));assert(!r.registerRealtime(&backend));assert(!r.registerPlatform(RISC_PLATFORM_REALTIME_CAPABILITY,1,Runtime::Scope::Global,0,&backend));}
 uintptr_t end=UINTPTR_MAX;assert(!RiscBoot::nextKeyValueContext(end) && end==UINTPTR_MAX);
 assert(reads+seeds==before);stage();
}
int main(int argc,char** argv){
 assert(argc==2 || argc==3);root=argv[1];
 if(argc==2){
  admission();for(scenario=0;scenario<=7;++scenario){stage();if(scenario==7)boundKeys(10);const auto child=fork();assert(child>=0);if(!child){const auto arg=std::to_string(scenario);execl(argv[0],argv[0],root.c_str(),arg.c_str(),static_cast<char*>(nullptr));_exit(99);}int status;assert(waitpid(child,&status,0)==child);if(status)fprintf(stderr,"provider realtime scenario %d status %d\n",scenario,status);assert(WIFEXITED(status) && WEXITSTATUS(status)==0);}
  puts("Provider realtime: admission/owner/entry/poll, canonical validation, no seed, generations/reused Runtime, failed graph, retained native/invocation/shutdown PASS");return 0;
 }
 scenario=atoi(argv[2]);
 alignas(Runtime) unsigned char storage[sizeof(Runtime)];
 for(unsigned iteration=0;iteration<(scenario<=1?2u:1u);++iteration){
  const auto before=reads+seeds;auto* r=new(storage) Runtime(port());assert(r->prepare(root.c_str()) && reads+seeds==before);deny(previous);
  const bool ok=r->run();assert(ok==(scenario<=1 || scenario==7));deny(saved[0]);deny(saved[1]);
  if(scenario>=2 && scenario<=6){assert(r->retained());assert(scenario==6?lifecycle==2:lifecycle==1);_exit(0);}
  assert(lifecycle==(iteration+1)*2);previous=saved[0];saved[0]={};saved[1]={};r->~Runtime();value={sizeof(value),RISC_REALTIME_UNSET,0,0,0,100,102};
 }
 assert(starts==stops && stops==unloads);return 0;
}
