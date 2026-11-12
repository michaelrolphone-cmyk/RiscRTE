// Actual merged Runtime; explicit doubled native capture and serial Port hooks.
// NativeSerialFiles protocol/storage qualification is a separate retained suite.
#include "bootstrap/Runtime.h"
#include "RuntimeFileFixture.h"
#include <RiscResidentAttentionV1.h>
#include "native/NativeAppMemory.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <string>
using namespace RiscBoot;
static Runtime* runtime;
static Runtime* volatile retainedRuntime;
static const risc_runtime_api_v1* api;
static risc_resident_attention_client_v1 host{}, child{};
static std::string mode;
static bool capture, pending, observing;
static unsigned barriers, polls, requests, enters, checkpoints, shutdowns, stops;
static unsigned memorySwitches;
static uint64_t memoryInvocation;
extern "C" bool native_app_memory_begin_for(uint64_t id){assert(!observing);++memorySwitches;memoryInvocation=id;return true;}
extern "C" bool native_app_memory_select(uint64_t id){assert(!observing);++memorySwitches;memoryInvocation=id;return true;}
extern "C" bool native_app_memory_end(){assert(!observing);++memorySwitches;memoryInvocation=0;return true;}
extern "C" void native_app_memory_retain_all(){}
extern "C" void native_app_memory_relocation(bool){assert(!observing);}
static unsigned dispatches, delays, starts, ends, observations, rejections, finis[2];
static bool is(const char* s) { return mode==s; }
static bool forbidden() { return mode.rfind("forbidden-",0)==0; }
struct Counts {
 unsigned b,p,r,e,c,s,t,d,y,a,z,m;
 bool operator==(const Counts& o)const { return b==o.b&&p==o.p&&r==o.r&&e==o.e&&c==o.c&&s==o.s&&t==o.t&&d==o.d&&y==o.y&&a==o.a&&z==o.z&&m==o.m; }
};
static Counts counts(){return {barriers,polls,requests,enters,checkpoints,shutdowns,stops,dispatches,delays,starts,ends,memorySwitches};}
static bool owner(){return true;}
static bool health(risc_runtime_health_v1* h){h->uptime_ms=321;return true;}
static bool clockNow(uint32_t* out){*out=12345;return true;}
static bool safe(){assert(!observing);++barriers;return !capture;}
static bool providerSafe(){return true;} // healthy capture is not exit-safe
static void delay(uint32_t){assert(!observing);++delays;}
static bool log(const char*){assert(!observing);return true;}
static void poll(){assert(!observing);++polls;}
static bool requested(){assert(!observing);++requests;return pending;}
static bool checkpoint(){assert(!observing&&!capture);++checkpoints;return true;}
static bool shutdown(){assert(!observing&&!capture);++shutdowns;return true;}
static bool stop(){assert(!observing&&!capture);++stops;return true;}
static void enter(){assert(!observing&&!capture&&pending&&!runtime->active()&&finis[0]==1&&finis[1]==1);++enters;}
static void controlBegin(bool){assert(!observing);++starts;}
static bool controlEnd(){assert(!observing);++ends;return true;}
static int32_t get(void*,uint32_t ns,const char*,void* out,uint32_t cap,uint32_t* n){assert(!observing&&cap>=4);memcpy(out,&ns,4);*n=4;return 0;}
static int32_t put(void*,uint32_t,const char*,const void*,uint32_t){assert(false);return -1;}
static KeyValueBackend kv{nullptr,get,put};
static risc_runtime_capability_v1 grants[2]{};
static unsigned read(unsigned role){auto* k=static_cast<const risc_key_value_v1*>(grants[role].api);uint32_t v=0,n=0;assert(k->get(k->context,"key",&v,4,&n)==0&&n==4);return v;}
static int32_t dispatch(void*,const risc_resident_request_v1*,risc_resident_reply_v1*){assert(!observing&&!capture);++dispatches;return RISC_RESIDENT_OK;}
static void attemptForbidden(bool rejecting){
 if(is(rejecting?"forbidden-reject-yield":"forbidden-yield"))api->yield_ms(1);
 if(is(rejecting?"forbidden-reject-service":"forbidden-service"))assert(api->service_checkpoint()!=RISC_RUNTIME_CHECKPOINT_OK);
 if(is(rejecting?"forbidden-reject-exit":"forbidden-exit"))assert(!api->exit_requested());
 if(is(rejecting?"forbidden-reject-launch":"forbidden-launch"))assert(!api->request_launch("child.elf"));
}
static int32_t observe(void* context,uint32_t now,risc_resident_attention_v1* out){
 assert(context==&host&&capture&&now==12345&&risc_runtime_get_api(1)==api);assert(memoryInvocation==child.base.invocation);observing=true;++observations;
 const auto before=counts();attemptForbidden(false);assert(counts()==before);
 out->pending_flags=is("quiet")?0:RISC_RESIDENT_ATTENTION_ALARM|RISC_RESIDENT_ATTENTION_EXIT;
 observing=false;return RISC_RESIDENT_OK;
}
static int32_t reject(void* context,uint32_t bits){
 assert(context==&host&&capture&&pending&&bits==RISC_RESIDENT_ATTENTION_EXIT&&risc_runtime_get_api(1)==api);assert(memoryInvocation==child.base.invocation);observing=true;++rejections;
 const auto before=counts();attemptForbidden(true);assert(counts()==before);
 observing=false;return is("reject-busy")?RISC_RESIDENT_BUSY:RISC_RESIDENT_OK;
}
extern "C" void attention_fini(unsigned role){assert(role<2&&!capture&&!observing);++finis[role];}
extern "C" void attention_entry(unsigned role){
 api=risc_runtime_get_api(1);assert(api);auto& client=role?child:host;client.base.struct_size=sizeof(client);
 assert(api->resident_shell(&client.base)&&risc_resident_attention_available_v1(&client));
 grants[role].struct_size=sizeof(grants[role]);assert(api->acquire("storage.key-value",1,0,&grants[role])&&read(role)==11+role);
 if(!role){
  risc_resident_callbacks_v1 cb{1,sizeof(cb),nullptr,dispatch,nullptr,nullptr};assert(host.base.register_shell(host.base.invocation,&cb)==0);
  risc_resident_attention_callbacks_v1 a{1,sizeof(a),RISC_RESIDENT_ATTENTION_TAG,RISC_RESIDENT_ATTENTION_ALL,&host,observe,reject};assert(host.register_attention(host.base.invocation,&a)==0);
  risc_resident_result_v1 result{};result.struct_size=sizeof(result);int rc=host.base.run_foreground(host.base.invocation,"child.elf",&result);
  if(forbidden()){assert(rc==RISC_RESIDENT_RETAINED&&runtime->retained()&&!risc_runtime_get_api(1));return;}
  assert(rc==0&&pending&&api->exit_requested()&&read(0)==11);assert(api->release(&grants[0]));return;
 }
 capture=true;pending=!is("quiet");const bool pendingBefore=pending;const auto before=counts();
 risc_resident_attention_v1 out{sizeof(out),0,0,0};int rc=child.attention(child.base.invocation,&out);
 assert(counts()==before&&pending==pendingBefore&&capture&&observations==1);
 const bool rejectMode=mode.find("reject")!=std::string::npos;
 if(rejectMode){assert(rc==0&&out.pending_flags==5);rc=child.reject_manual(child.base.invocation,4);assert(rejections==1&&counts()==before&&pending&&capture);}
 if(forbidden()){
  assert(rc==RISC_RESIDENT_RETAINED&&!risc_runtime_get_api(1));
  const auto terminalCounts=counts();assert(child.attention(child.base.invocation,&out)!=0);assert(counts()==terminalCounts);return;
 }
 assert(rc==(is("reject-busy")?RISC_RESIDENT_BUSY:RISC_RESIDENT_OK));
 assert(risc_runtime_get_api(1)==api&&read(1)==12&&capture); // child authority preserved
 if(is("quiet")){assert(out.pending_flags==0);pending=true;}
 capture=false;assert(api->exit_requested()&&!api->request_launch("child.elf"));
 assert(api->release(&grants[1]));
}
int main(int argc,char** argv){
 assert(argc==3);std::string root=argv[1];mode=argv[2];
 std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
 std::ofstream(root+"/boot.json")<<R"({"board":"board.json","default_app":"host.elf","drivers":[],"provider_activation":"demand","resident_shell":{"api":1,"host":"host.elf","foreground":["child.elf"]},"app_capabilities":[{"manifest":"host.json","grants":[{"capability":"storage.key-value","api":1,"instance_id":11}]},{"manifest":"child.json","grants":[{"capability":"storage.key-value","api":1,"instance_id":12}]}]})";
 for(const char* name:{"host","child"})std::ofstream(root+"/"+name+".json")<<"{\"type\":\"application\",\"id\":\""<<name<<"\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"file_name\":\""<<name<<".elf\",\"entry\":\"app_main\",\"requires\":[{\"capability\":\"storage.key-value\",\"api\":1}]}";
 static RiscTest::FileSources files({root});Port port{owner,health,delay,log,nullptr,&kv,safe,providerSafe};port.monotonicMs=clockNow;
 port.controlBegin=controlBegin;port.controlEnd=controlEnd;port.serialFilesPoll=poll;port.serialTransferRequested=requested;port.serialTransferEnter=enter;
 port.serialFilesCheckpoint=checkpoint;port.serialFilesControlCheckpoint=checkpoint;port.serialFilesShutdown=shutdown;port.serialFilesStop=stop;
 runtime=new Runtime(port);assert(files.bind(*runtime)&&runtime->prepare(root.c_str()));assert(runtime->run()!=forbidden());
 if(forbidden()){assert(runtime->retained()&&finis[0]==0&&finis[1]==0&&!enters);retainedRuntime=runtime;}
 else {assert(!runtime->retained()&&pending&&finis[0]==1&&finis[1]==1);assert(runtime->nativeFilesServiceSafe()&&runtime->nativeFilesCheckpoint());assert(enters==1&&pending);assert(runtime->stopNativeFiles()&&stops==1);delete runtime;}
 printf("attention-transfer %s PASS\n",mode.c_str());
}
