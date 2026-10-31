#include "bootstrap/Runtime.h"
#include "RuntimeFileFixture.h"
#include <RiscResidentAttentionV1.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <string>
using namespace RiscBoot;
static std::string root,mode;
static Runtime* runtime;
static Runtime* volatile retainedRuntime;
static const risc_runtime_api_v1* api;
static risc_resident_attention_client_v1 host{},child{};
static bool owned=true,exitSafe=true;
static unsigned barriers,delays,reads,writes,dispatches,observations,rejections,logs,safetyChecks,finis[2];
static bool is(const char*s){return mode==s;}
static bool terminal(){return mode.rfind("bad-",0)==0||mode.rfind("forbidden-",0)==0||is("owner-loss")||mode.rfind("unsafe-",0)==0;}
static bool owner(){return owned;}
static bool health(risc_runtime_health_v1*out){out->uptime_ms=321;return true;}
static bool clockNow(uint32_t*out){*out=12345;return true;}
static void delay(uint32_t){++delays;}
static bool log(const char*s){++logs;fprintf(stderr,"%s\n",s);return true;}
static bool safe(){++barriers;return exitSafe;}
static bool providerSafe(){
 if(!exitSafe){++safetyChecks;
  if(is("unsafe-pre-owner")){owned=false;return true;}
  if(is("unsafe-post-owner")&&safetyChecks==2){owned=false;return true;}
  if(is("unsafe-pre")||(is("unsafe-post")&&safetyChecks==2))return false;
 }
 return true;
}
static int32_t get(void*,uint32_t ns,const char*,void*out,uint32_t cap,uint32_t*size){++reads;assert(cap>=4);memcpy(out,&ns,4);*size=4;return RISC_KEY_VALUE_OK;}
static int32_t put(void*,uint32_t,const char*,const void*,uint32_t){++writes;return RISC_KEY_VALUE_OK;}
static KeyValueBackend kv{nullptr,get,put};
static risc_runtime_capability_v1 grants[2]{};
static risc_key_value_v1 kvApis[2]{};
static unsigned read(unsigned role){uint32_t v=0,n=0;assert(kvApis[role].get(kvApis[role].context,"key",&v,4,&n)==0&&n==4);return v;}
static int32_t dispatch(void*,const risc_resident_request_v1*q,risc_resident_reply_v1*r){++dispatches;if(is("reject-policy")&&q->reason==RISC_RESIDENT_CHECKPOINT_POLL)r->flags=RISC_RESIDENT_REPLY_POLICY_REQUEST;return RISC_RESIDENT_OK;}
static int32_t rejectManual(void* context,uint32_t bits){
 assert(context==&host&&bits==RISC_RESIDENT_ATTENTION_EXIT);++rejections;
 assert(risc_runtime_get_api(1)==api);
 if(is("forbidden-reject-yield"))api->yield_ms(1);
 if(is("forbidden-reject-recursive"))assert(child.reject_manual(child.base.invocation,bits)==RISC_RESIDENT_RETAINED);
 if(is("bad-reject-status"))return 99;
 return is("reject-busy")?RISC_RESIDENT_BUSY:RISC_RESIDENT_OK;
}
static int32_t observe(void*context,uint32_t now,risc_resident_attention_v1*out){
 assert(context==&host&&now==12345&&out->monotonic_ms==now);assert(risc_runtime_get_api(1)==api);++observations;
 if(is("diagnostic")){assert(!api->diagnostic("provider status changed"));assert(risc_runtime_get_api(1)==api);}
 if(is("forbidden-yield"))api->yield_ms(1);
 if(is("forbidden-acquire")){risc_runtime_capability_v1 g{sizeof(g)};assert(!api->acquire("storage.key-value",1,1,&g));}
 if(is("forbidden-release")){auto g=grants[0];assert(!api->release(&g));}
 if(is("forbidden-launch"))assert(!api->request_launch("child.elf"));
 if(is("forbidden-clock")){uint32_t v;assert(!api->monotonic_ms(&v));}
 if(is("forbidden-checkpoint")){risc_resident_request_v1 q{sizeof(q),1,0,0};risc_resident_reply_v1 r{sizeof(r),0};assert(child.base.checkpoint(child.base.invocation,&q,&r)!=0);}
 if(is("forbidden-register")){risc_resident_callbacks_v1 cb{1,sizeof(cb),nullptr,dispatch,nullptr,nullptr};assert(host.base.register_shell(host.base.invocation,&cb)!=0);}
 if(is("forbidden-recursive")){risc_resident_attention_v1 r{sizeof(r),0,0,0};assert(child.attention(child.base.invocation,&r)==RISC_RESIDENT_RETAINED);}
 if(is("forbidden-storage")){uint32_t v=0,n=0;assert(kvApis[0].get(kvApis[0].context,"key",&v,4,&n)==RISC_KEY_VALUE_CONTEXT);}
 if(mode.rfind("forbidden-",0)==0&&mode.find("reject")==std::string::npos)assert(!risc_runtime_get_api(1));
 if(is("owner-loss")){owned=false;assert(!risc_runtime_get_api(1));}
 out->pending_flags=is("none")||is("busy")?0:RISC_RESIDENT_ATTENTION_ALARM;
 if(is("bad-flags"))out->pending_flags=8;
 if(is("bad-supported"))out->supported_flags=8;
 if(is("bad-size"))out->struct_size=0;
 if(is("bad-time"))out->monotonic_ms++;
 if(mode.find("reject")!=std::string::npos)out->pending_flags=RISC_RESIDENT_ATTENTION_ALARM|RISC_RESIDENT_ATTENTION_EXIT;
 if(is("bad-status"))return 99;
 if(is("bad-retained"))return RISC_RESIDENT_RETAINED;
 return is("busy")||is("bad-busy")?RISC_RESIDENT_BUSY:RISC_RESIDENT_OK;
}
extern "C" void attention_fini(unsigned role){assert(role<2);++finis[role];}
extern "C" void attention_entry(unsigned role){
 api=risc_runtime_get_api(1);assert(api);
 struct Prefix {risc_resident_client_v1 client;uint64_t canary[2];} prefix{};
 prefix.client.struct_size=sizeof(prefix.client);prefix.canary[0]=0x1234;prefix.canary[1]=0x5678;
 assert(api->resident_shell(&prefix.client)&&prefix.client.struct_size==sizeof(prefix.client));assert(prefix.canary[0]==0x1234&&prefix.canary[1]==0x5678);
 auto&client=role?child:host;client.base.struct_size=sizeof(client);assert(api->resident_shell(&client.base));assert(risc_resident_attention_available_v1(&client));assert(prefix.client.invocation==client.base.invocation);
 grants[role].struct_size=sizeof(grants[role]);assert(api->acquire("storage.key-value",1,0,&grants[role]));kvApis[role]=*static_cast<const risc_key_value_v1*>(grants[role].api);assert(read(role)==11+role);
 if(!role){
  risc_resident_callbacks_v1 cb{1,sizeof(cb),nullptr,dispatch,nullptr,nullptr};assert(host.base.register_shell(host.base.invocation,&cb)==0);
  risc_resident_attention_callbacks_v1 attention{1,sizeof(attention),RISC_RESIDENT_ATTENTION_TAG,RISC_RESIDENT_ATTENTION_ALL,&host,observe,rejectManual};
  if(!is("old-host")){auto malformed=attention;malformed.reject_manual=nullptr;assert(host.register_attention(host.base.invocation,&malformed)==RISC_RESIDENT_INVALID);assert(host.register_attention(host.base.invocation,&attention)==0);assert(host.register_attention(host.base.invocation,&attention)==RISC_RESIDENT_INVALID);}
  risc_resident_result_v1 result{};result.struct_size=sizeof(result);int rc=host.base.run_foreground(host.base.invocation,"child.elf",&result);
  if(terminal()){assert(rc==RISC_RESIDENT_RETAINED);assert(!risc_runtime_get_api(1));return;}
  assert(rc==0&&read(0)==11);assert(child.reject_manual(child.base.invocation,4)==RISC_RESIDENT_DENIED);assert(api->release(&grants[0]));return;
 }
 risc_resident_attention_v1 out{sizeof(out),0,0,0};unsigned b=barriers,d=delays,r=reads,w=writes,l=logs;
 assert(child.attention(child.base.invocation+1,&out)==RISC_RESIDENT_DENIED);assert(barriers==b);
 if(is("busy")){out.supported_flags=123;out.pending_flags=5;out.monotonic_ms=987;}
 exitSafe=false;int rc=child.attention(child.base.invocation,&out);
 assert(barriers==b&&delays==d&&reads==r&&writes==w&&logs==l&&!dispatches);
 if(mode.find("reject")!=std::string::npos){
  assert(rc==0&&out.pending_flags==5);
  assert(child.reject_manual(child.base.invocation,1)==RISC_RESIDENT_INVALID);
  assert(child.reject_manual(child.base.invocation,0)==RISC_RESIDENT_INVALID);
  assert(child.reject_manual(child.base.invocation,2)==RISC_RESIDENT_INVALID);
  assert(child.reject_manual(child.base.invocation+1,4)==RISC_RESIDENT_DENIED);
  assert(!rejections);
  rc=is("reject-policy")?RISC_RESIDENT_OK:child.reject_manual(child.base.invocation,4);
  assert(rejections==(is("reject-policy")?0u:1u)&&barriers==b&&delays==d&&reads==r&&writes==w);
  if(!terminal()){
   assert(rc==(is("reject-busy")?RISC_RESIDENT_BUSY:RISC_RESIDENT_OK));
   if(is("reject-busy"))assert(child.reject_manual(child.base.invocation,4)==RISC_RESIDENT_BUSY&&rejections==2);
   else if(!is("reject-policy"))assert(child.reject_manual(child.base.invocation,4)==RISC_RESIDENT_INVALID);
   assert(out.pending_flags==5); // reject cannot mutate copied alarm result
  }
 }
 if(mode.rfind("unsafe-",0)==0)assert(observations==(mode.rfind("unsafe-pre",0)==0?0u:1u));
 if(mode.rfind("bad-",0)==0&&mode.find("reject")==std::string::npos)assert(safetyChecks==1);
 if(terminal()){assert(rc==RISC_RESIDENT_RETAINED&&runtime->retained());assert(!risc_runtime_get_api(1));return;}
 assert(risc_runtime_get_api(1)==api);assert(read(1)==12); // exact child grant survived
 if(is("old-host")||is("no-clock")||is("no-safety-hook"))assert(rc==RISC_RESIDENT_INCOMPATIBLE&&!observations);
 else if(is("busy"))assert(rc==RISC_RESIDENT_BUSY&&out.supported_flags==123&&out.pending_flags==5&&out.monotonic_ms==987);
 else if(mode.find("reject")==std::string::npos)assert(rc==0&&out.supported_flags==7&&out.pending_flags==(is("none")?0u:1u));
 exitSafe=true;risc_resident_request_v1 q{sizeof(q),RISC_RESIDENT_CHECKPOINT_POLL,0,0};risc_resident_reply_v1 reply{sizeof(reply),0};assert(child.base.checkpoint(child.base.invocation,&q,&reply)==0);assert(dispatches==1&&barriers>b);if(is("reject-policy")){q.reason=RISC_RESIDENT_CHECKPOINT_POLICY;assert(child.base.checkpoint(child.base.invocation,&q,&reply)==0);assert(child.reject_manual(child.base.invocation,4)==RISC_RESIDENT_INVALID&&!rejections);}assert(read(1)==12);assert(api->release(&grants[1]));
}
int main(int argc,char**argv){
 assert(argc==3);root=argv[1];mode=argv[2];
 std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
 std::ofstream(root+"/boot.json")<<R"({"board":"board.json","default_app":"host.elf","drivers":[],"provider_activation":"demand","resident_shell":{"api":1,"host":"host.elf","foreground":["child.elf"]},"app_capabilities":[{"manifest":"host.json","grants":[{"capability":"storage.key-value","api":1,"instance_id":11}]},{"manifest":"child.json","grants":[{"capability":"storage.key-value","api":1,"instance_id":12}]}]})";
 for(const char*name:{"host","child"})std::ofstream(root+"/"+name+".json")<<"{\"type\":\"application\",\"id\":\""<<name<<"\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"file_name\":\""<<name<<".elf\",\"entry\":\"app_main\",\"requires\":[{\"capability\":\"storage.key-value\",\"api\":1}]}";
 static RiscTest::FileSources files({root});Port port{owner,health,delay,log,nullptr,&kv,safe,is("no-safety-hook")?nullptr:providerSafe};if(!is("no-clock"))port.monotonicMs=clockNow;
 runtime=new Runtime(port);assert(files.bind(*runtime));if(!runtime->prepare(root.c_str())){fprintf(stderr,"prepare: %s\n",runtime->error());abort();}assert(runtime->run()!=terminal());assert(runtime->retained()==terminal());assert(finis[0]==(terminal()?0u:1u)&&finis[1]==(terminal()?0u:1u));
 if(terminal())retainedRuntime=runtime;else delete runtime;
 printf("resident attention %s PASS\n",mode.c_str());
}
