#include "bootstrap/Runtime.h"
#include "runtime/streams/ProviderQueueHost.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
static std::string mode;
static std::vector<std::string> events;
static bool owner=true,locked=false;
static unsigned invocations=0;
static const void* appImage=nullptr;
static const void* providerImage=nullptr;
static const void* rootImage=nullptr;
static risc_stream_client_v1 oldClient{};
static risc_stream_opened_v1 oldOpened{};
static risc_runtime_capability_v1 oldGrant{};
static const risc_runtime_api_v1* savedRuntime=nullptr;
extern "C" const char* stream_test_mode(){return mode.c_str();}
extern "C" void stream_test_event(const char* e){events.emplace_back(e);}
extern "C" void stream_test_slow(){std::this_thread::sleep_for(std::chrono::milliseconds(4));}
extern "C" void stream_test_provider(const void* p){providerImage=p;}
extern "C" void stream_test_root(const void* p){rootImage=p;}
extern "C" void stream_test_app(const void* p){appImage=p;}
extern "C" void stream_test_grant_failure(unsigned n){RuntimeStreams::Testing::failGrantNumber(n);}
extern "C" void stream_test_lock(){assert(!locked);assert(RuntimeStreams::Testing::lockRegistry());locked=true;}
extern "C" void stream_test_owner(bool b){owner=b;}
extern "C" unsigned stream_test_invocation(){return ++invocations;}
extern "C" void stream_test_save(const risc_stream_client_v1* c,const risc_stream_opened_v1* o,const risc_runtime_capability_v1* g){oldClient=*c;oldOpened=*o;oldGrant=*g;}
extern "C" void stream_test_stale(const risc_stream_client_v1* current){
 savedRuntime=risc_runtime_get_api(1);
 if(!oldClient.context)return;
 assert(oldClient.context!=current->context);
 uint32_t n=99;char data=0;
 assert(oldClient.read(oldClient.context,oldOpened.rx,&data,1,&n)==RISC_STREAM_CLOSED && !n);
 assert(oldClient.close(oldClient.context,oldOpened.session,1)==RISC_STREAM_CLOSED);
 risc_stream_opened_v1 out{sizeof(out)};uint32_t request=42;
 assert(oldClient.open(oldClient.context,&oldGrant,&request,sizeof(request),1,&out)==RISC_STREAM_DENIED && !out.session);
}
extern "C" void stream_test_reenter(bool adapter){
 if(!savedRuntime){assert(!risc_runtime_get_api(1));return;}
 const bool active=risc_runtime_get_api(1)!=nullptr;
 if(adapter)assert(!active);
 auto grant=oldGrant;
 assert(!savedRuntime->release(&grant));
 risc_runtime_capability_v1 extra{sizeof(extra)};
 assert(!savedRuntime->acquire("test.stream",1,0,&extra));
 assert(!savedRuntime->request_launch("child.elf"));
 assert(!savedRuntime->retain_invocation());
 savedRuntime->yield_ms(1);
 if(oldClient.context){
  char c=0;uint32_t n=77;
  assert(oldClient.read(oldClient.context,oldOpened.rx,&c,1,&n)==(active || adapter?RISC_STREAM_BUSY:RISC_STREAM_CLOSED) && !n);
  assert(oldClient.close(oldClient.context,oldOpened.session,1)==(active || adapter?RISC_STREAM_BUSY:RISC_STREAM_CLOSED));
 }
}
static unsigned count(const char* s){return unsigned(std::count(events.begin(),events.end(),s));}
static bool mapped(const void* p){Dl_info info{};return p && dladdr(p,&info);}
static void write(const std::string& root,const char* p,const std::string& text){std::ofstream(root+"/"+p)<<text;}
int main(int argc,char** argv){
 assert(argc==4);const std::string root=argv[1];mode=argv[2];const std::string activation=argv[3];
 write(root,"board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 write(root,"stream.json",R"({"type":"driver","id":"stream","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"stream.elf","requires":[{"capability":"test.root","api":1}],"provides":[{"capability":"test.stream","api":1}]})");
 write(root,"root.json",R"({"type":"driver","id":"stream-root","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"root.elf","requires":[],"provides":[{"capability":"test.root","api":1}]})");
 const bool promote=mode=="demand-retained";
 const std::string promotion=promote?R"(,{"capability":"runtime.provider-promotion","api":1})":"";
 for(const char* app:{"default","child"})write(root,(std::string(app)+".json").c_str(),std::string(R"({"type":"application","id":")")+app+R"(","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":")"+app+R"(.elf","entry":"app_main","requires":[{"capability":"test.stream","api":1})"+(std::string(app)=="default"?promotion:"")+"]}");
 const std::string promoteGrant=promote?R"(,{"capability":"runtime.provider-promotion","api":1,"instance_id":0})":"";
 write(root,"boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","provider_activation":")")+activation+R"(","drivers":[{"manifest":"stream.json"},{"manifest":"root.json"}],"app_capabilities":[{"manifest":"default.json","grants":[{"capability":"test.stream","api":1,"instance_id":0})"+promoteGrant+R"(]},{"manifest":"child.json","grants":[{"capability":"test.stream","api":1,"instance_id":0}]}]})");
 uint32_t clock=0;
 static uint32_t* ticks=&clock;
 auto* runtime=new RiscBoot::Runtime({[](){return owner;},[](risc_runtime_health_v1* h){h->uptime_ms=++*ticks;return true;},[](uint32_t){},[](const char*){return true;}});
 assert(runtime->prepare(root.c_str()));
 const bool result=runtime->run();
 if(locked){RuntimeStreams::Testing::unlockRegistry();locked=false;}
 const bool retained=mode=="terminal-retained" || mode=="start-retained" || mode=="quiesce-fail" || mode=="close-fail" || mode=="close-slow" || mode=="close-busy" || mode=="revoke-busy" || mode=="grant-rollback-retained" ||
  mode=="open-retained-zero" || mode=="open-retained-token" || mode=="open-malformed" || mode=="open-duplicate" || mode=="open-foreign" || mode=="open-direction" || mode=="open-partial-error" || mode=="open-slow" || mode=="open-busy" || mode.rfind("call-",0)==0;
 assert(runtime->retained()==retained);
 const bool malformedExtension=mode=="unknown-version" || mode.rfind("truncated-",0)==0;
 assert(result==(!retained && (!malformedExtension || activation=="demand")));
 if(retained){
  assert(mapped(providerImage) && mapped(rootImage) && !count("root:stop"));
  if(mode!="start-retained")assert(mapped(appImage) && !count("app:fini"));
  assert(!count("provider:stop"));
  if(mode=="start-retained")assert(count("provider:quiesce")==2);
  const unsigned closes=count("provider:close"),polls=count("provider:poll");
  if(mode=="open-duplicate" || mode=="open-foreign" || mode=="open-direction" || mode=="open-malformed" || mode=="open-busy")assert(!closes);
  uint32_t n=123;char c=0;if(oldClient.read)assert(oldClient.read(oldClient.context,oldOpened.rx,&c,1,&n)==RISC_STREAM_CLOSED && !n);
  if(oldClient.close)assert(oldClient.close(oldClient.context,oldOpened.session,1)==RISC_STREAM_CLOSED);
  assert(count("provider:close")==closes && count("provider:poll")==polls);
  assert(!runtime->run());
  printf("%s/%s: retained; closes=%u polls=%u bytes=%zu PASS\n",mode.c_str(),activation.c_str(),closes,polls,RuntimeStreams::Testing::allocatedBytes());
  fflush(stdout);std::_Exit(0);
 }
 if(mode=="child" || mode=="child-init-fail" || promote){assert(invocations==3);assert(count("app:fini")==unsigned(mode=="child-init-fail"?2:3));}
 if(mode=="release-open" || mode=="forgot-close" || mode=="fini-close" || mode=="grant1-fail" || mode=="grant2-fail")assert(count("provider:close")==1);
 if(malformedExtension)assert(!count("provider:start"));
 delete runtime;
 assert(RuntimeStreams::Testing::allocatedBytes()==0);
 printf("%s/%s: checked cleanup and stale invocation isolation PASS\n",mode.c_str(),activation.c_str());
}
