#include "bootstrap/Runtime.h"
#include <RiscResidentShellV1.h>
#include <RiscStreamClientV1.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <string>
#include <vector>
#ifdef RISC_NATIVE_APP_MEMORY_TEST
#include "native/NativeAppMemory.h"
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <unordered_set>
bool resident_test_memory_lock=true;
static std::unordered_set<void*> memoryLive;
extern "C" void* heap_caps_malloc(size_t n,uint32_t){void* p=std::malloc(n);assert(p);memoryLive.insert(p);return p;}
extern "C" void* heap_caps_calloc(size_t n,size_t z,uint32_t caps){if(z && n>SIZE_MAX/z)return nullptr;void* p=heap_caps_malloc(n*z,caps);std::memset(p,0,n*z);return p;}
extern "C" void* heap_caps_realloc(void* p,size_t n,uint32_t caps){if(!p)return heap_caps_malloc(n,caps);assert(memoryLive.erase(p));p=std::realloc(p,n);assert(p);memoryLive.insert(p);return p;}
extern "C" void heap_caps_free(void* p){if(p){assert(memoryLive.erase(p));std::free(p);}}
extern "C" void* pressure_malloc(size_t n){return heap_caps_malloc(n,0);}
extern "C" void* pressure_calloc(size_t n,size_t z){return heap_caps_calloc(n,z,0);}
extern "C" void* pressure_realloc(void* p,size_t n){return heap_caps_realloc(p,n,0);}
extern "C" void pressure_free(void* p){heap_caps_free(p);}
extern "C" size_t heap_caps_get_free_size(uint32_t){return 0;}
extern "C" size_t heap_caps_get_largest_free_block(uint32_t){return 0;}
extern "C" void vTaskDelay(TickType_t){}
#endif
using namespace RiscBoot;
static_assert(RISC_RUNTIME_DEFAULT_REQUEST_V1_SIZE==offsetof(risc_runtime_api_v1,resident_shell),"Runtime prefix remains unchanged");
static_assert(RISC_RESIDENT_HANDOFF==3 && RISC_RESIDENT_NO_PENDING==4,"Append-only resident status values");
namespace {
enum {Host,Child,Legacy,Receiver,Unlisted,LegacyNext,Roles};
const char* names[]={"host","child","legacy","receiver","unlisted","legacy-next"};
std::string root,mode;
bool owned=true,exitSafe=true,finished=false;
unsigned maps[Roles]{},unmaps[Roles]{},starts[Roles]{},entries[Roles]{},finis[Roles]{};
unsigned unloadAttempts[Roles]{};
unsigned activeRole=Host,dispatches=0,failures=0,grantQuiesces=0,streamCloses=0;
unsigned handoffs=0,resumes=0,fileResults=0;
#ifdef RESIDENT_LOADING_TEST
unsigned loadingCalls=0;
bool loadingBusyProbe=false;
std::string loadingPath;
#endif
Runtime* volatile retainedRuntime=nullptr;
const risc_runtime_api_v1* api=nullptr;
std::vector<std::string> events;
struct Snapshot {
  unsigned role=0;
  risc_runtime_capability_v1 grant{};
  risc_key_value_v1 kv{};
  risc_stream_client_v1 streams{};
  risc_stream_opened_v1 opened{};
  risc_resident_client_v1 resident{};
  risc_resident_callbacks_v1 callbacks{};
  void* allocation=nullptr;
  bool unloaded=false;
  bool memoryEnded=false;
};
Snapshot snapshots[20]{};
size_t snapshotCount=0,currentSnapshot[Roles]{};
Snapshot& current(unsigned role){return snapshots[currentSnapshot[role]];}
bool is(const char* value){return mode==value;}
bool has(const char* value){return mode.find(value)!=std::string::npos;}
bool begins(const char* value){return mode.rfind(value,0)==0;}
bool target(unsigned role,const char* suffix){return mode==std::string(names[role])+"-"+suffix;}
bool terminal(){return has("retained");}
bool residentFile(){return begins("file-resident-legacy");}
bool legacyFile(){return begins("file-legacy-resident");}
bool startChild(){return begins("child-") || residentFile() || is("repeated-chain");}
bool legacyLaunch(){return is("legacy-launch-resident") || is("repeated-chain") || is("unconsumed-continuation") || is("loading-resume-busy");}
void event(unsigned role,const char* phase){events.push_back(std::string(names[role])+":"+phase);}
size_t position(const std::string& value){for(size_t n=0;n<events.size();++n)if(events[n]==value)return n;return events.size();}
bool owner(){return owned;}
bool health(risc_runtime_health_v1*){return true;}
void delay(uint32_t){}
bool log(const char*){return true;}
bool safe(){return exitSafe;}
int32_t get(void*,uint32_t ns,const char*,void* out,uint32_t cap,uint32_t* size){assert(cap>=4);memcpy(out,&ns,4);*size=4;return RISC_KEY_VALUE_OK;}
int32_t put(void*,uint32_t,const char*,const void*,uint32_t){return RISC_KEY_VALUE_OK;}
KeyValueBackend kv{nullptr,get,put};
void save(const char* path,const JsonDocument& doc){std::string bytes;serializeJson(doc,bytes);std::ofstream(root+"/"+path)<<bytes;}
void setup(){
  std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
  JsonDocument boot;boot["board"]="board.json";boot["default_app"]="host.elf";boot["provider_activation"]="demand";
  auto drivers=boot["drivers"].to<JsonArray>();
  for(const char* name:{"stream","root","grant"})drivers.add<JsonObject>()["manifest"]=std::string(name)+".json";
  std::ofstream(root+"/stream.json")<<R"({"type":"driver","id":"stream","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"stream_session_provider.elf","requires":[{"capability":"test.root","api":1}],"provides":[{"capability":"test.stream","api":1}]})";
  std::ofstream(root+"/root.json")<<R"({"type":"driver","id":"stream-root","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"stream_session_root.elf","requires":[],"provides":[{"capability":"test.root","api":1}]})";
  std::ofstream(root+"/grant.json")<<R"({"type":"driver","id":"home-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"default_request_provider.elf","requires":[],"provides":[{"capability":"test.home","api":1}]})";
  auto resident=boot["resident_shell"].to<JsonObject>();resident["api"]=1;resident["host"]="host.elf";
  auto foreground=resident["foreground"].to<JsonArray>();foreground.add("child.elf");foreground.add("receiver.elf");
  auto legacy=resident["legacy"].to<JsonArray>();legacy.add("legacy.elf");legacy.add("legacy-next.elf");
  if(is("legacy-empty"))legacy.clear();
  if(is("legacy-absent"))resident.remove("legacy");
  if(is("policy-legacy-null"))resident["legacy"]=nullptr;
  if(is("policy-legacy-string"))resident["legacy"]="legacy.elf";
  if(is("policy-legacy-object"))resident["legacy"].to<JsonObject>();
  if(is("policy-legacy-number"))resident["legacy"]=1;
  if(is("policy-legacy-bool"))resident["legacy"]=true;
  if(is("policy-legacy-item-null"))legacy.add(nullptr);
  if(is("policy-legacy-item-number"))legacy.add(1);
  if(is("policy-legacy-duplicate"))legacy.add("legacy.elf");
  if(is("policy-legacy-host"))legacy.add("host.elf");
  if(is("policy-legacy-foreground"))legacy.add("child.elf");
  if(is("policy-legacy-unadmitted"))legacy.add("missing.elf");
  if(is("policy-legacy-traversal"))legacy.add("../legacy.elf");
  if(is("policy-legacy-absolute"))legacy.add(root+"/legacy.elf");
  if(is("policy-unknown"))resident["unrecognized"]=true;
  auto policies=boot["app_capabilities"].to<JsonArray>();
  for(unsigned role=0;role<Roles;++role){
    JsonDocument app;app["type"]="application";app["id"]=names[role];app["version"]="1.0.0";app["architecture"]="xtensa-esp32s3";
    app["file_name"]=std::string(names[role])+".elf";app["entry"]="app_main";
    auto requirements=app["requires"].to<JsonArray>();auto policy=policies.add<JsonObject>();policy["manifest"]=std::string(names[role])+".json";
    auto grants=policy["grants"].to<JsonArray>();
    auto add=[&](const char* cap,uint32_t ns){auto req=requirements.add<JsonObject>();req["capability"]=cap;req["api"]=1;auto grant=grants.add<JsonObject>();grant["capability"]=cap;grant["api"]=1;grant["instance_id"]=ns;};
    add("storage.key-value",11+role);add("test.stream",0);add("file.open",0);
    if(target(role,"grant-retained"))add("test.home",0);
    if(role!=Host)app["supported_file_types"].to<JsonArray>().add(".txt");
    save((std::string(names[role])+".json").c_str(),app);
  }
  save("boot.json",boot);
}
risc_runtime_capability_v1 acquire(const char* cap){risc_runtime_capability_v1 grant{};grant.struct_size=sizeof(grant);assert(api->acquire(cap,1,0,&grant));return grant;}
void denied(const risc_key_value_v1& table){uint32_t value=0,n=0;assert(table.get(table.context,"test",&value,4,&n)==RISC_KEY_VALUE_CONTEXT);}
void checkStale(size_t except){
  for(size_t i=0;i<snapshotCount;++i){
    if(i==except)continue;
    const auto& old=snapshots[i];denied(old.kv);auto grant=old.grant;assert(!api->release(&grant));
    uint32_t n=99;char byte=0;assert(old.streams.read(old.streams.context,old.opened.rx,&byte,1,&n)==RISC_STREAM_CLOSED);
    if(old.resident.role==RISC_RESIDENT_ROLE_HOST){
      assert(old.resident.register_shell(old.resident.invocation,&old.callbacks)==RISC_RESIDENT_DENIED);
      risc_resident_result_v1 result{};result.struct_size=sizeof(result);
      assert(old.resident.run_foreground(old.resident.invocation,"legacy.elf",&result)==RISC_RESIDENT_DENIED);
      assert(old.resident.request_foreground_exit(old.resident.invocation)==RISC_RESIDENT_DENIED);
    }else if(old.resident.role){
      risc_resident_request_v1 request{sizeof(request),1,0,0};risc_resident_reply_v1 reply{sizeof(reply),0};
      assert(old.resident.checkpoint(old.resident.invocation,&request,&reply)==RISC_RESIDENT_DENIED);
    }
  }
}
void queueFile(unsigned role,char* source,char* receiver,const char* name){
  auto grant=acquire("file.open");auto* files=static_cast<const t5_file_open_api_v1*>(grant.api);
  std::strcpy(receiver,name);assert(!std::strcmp(source,"/sd/Books/Original.TXT"));
  assert(files->open_request(source,receiver,0x123456789abcdef0ULL));
  // The queue and continuation must own copies after the calling ELF is gone.
  std::memset(source,'x',std::strlen(source));std::memset(receiver,'y',std::strlen(receiver));
  assert(!api->request_default() && !api->request_launch("unlisted.elf"));
  event(role,"file-request");
}
void receiveFile(bool home){
  auto grant=acquire("file.open");auto* files=static_cast<const t5_file_open_api_v1*>(grant.api);
  char source[512];assert(files->source_path_get(source,sizeof(source)) && !std::strcmp(source,"/sd/Books/Original.TXT"));
  assert(!api->request_launch("unlisted.elf"));
  if(home){assert(api->request_default());if(has("home-retained"))exitSafe=false;}
}
void takeFile(){
  auto grant=acquire("file.open");auto* files=static_cast<const t5_file_open_api_v1*>(grant.api);
  int32_t error=77;uint64_t cookie=0;assert(files->open_take_result(&error,&cookie));
  assert(cookie==0x123456789abcdef0ULL && error==(has("failure")?-1:0));
  assert(!files->open_take_result(&error,&cookie));++fileResults;
}
void checkpoint(Snapshot& context){
  risc_resident_request_v1 request{sizeof(request),RISC_RESIDENT_CHECKPOINT_CONTROLS,0,0};risc_resident_reply_v1 reply{sizeof(reply),0};
  assert(context.resident.checkpoint(context.resident.invocation,&request,&reply)==RISC_RESIDENT_OK);
}
int32_t runHost(Snapshot& context,const char* path){
  risc_resident_result_v1 result;std::memset(&result,0xa5,sizeof(result));result.struct_size=sizeof(result);
  const auto status=context.resident.run_foreground(context.resident.invocation,path,&result);
  assert(result.status==status);
  if(status==RISC_RESIDENT_HANDOFF){++handoffs;assert(!starts[Legacy] || legacyFile() || is("repeated-chain"));}
  if(status==RISC_RESIDENT_NO_PENDING)assert(result.invocation==0 && result.failure.kind==0);
  if(status==RISC_RESIDENT_OK && !path){++resumes;assert(result.invocation);}
  return status;
}
}
extern "C" int dlclose(void* module){
  const auto* role=static_cast<const unsigned*>(dlsym(module,"resident_legacy_test_role"));
  if(role && *role<Roles){
    ++unloadAttempts[*role];
    if(target(*role,"unload-retained")){
      // runOne already revoked grants and ended the native allocation ledger.
      // Keep the actual mapping live to exercise production's unload fence.
      current(*role).memoryEnded=true;event(*role,"unload-refused");return -1;
    }
  }
  static const auto close=reinterpret_cast<int(*)(void*)>(dlsym(RTLD_NEXT,"dlclose"));
  assert(close);return close(module);
}
#ifdef RISC_NATIVE_APP_MEMORY_TEST
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(){return reinterpret_cast<void*>(owned?1:2);}
#endif
extern "C" const char* stream_test_mode(){return target(activeRole,"stream-retained")?"close-fail":"normal";}
extern "C" void stream_test_event(const char* name){events.emplace_back(name);if(!std::strcmp(name,"provider:close"))++streamCloses;}
extern "C" void stream_test_slow(){assert(false);}
extern "C" void stream_test_provider(const void*){}
extern "C" void stream_test_root(const void*){}
extern "C" void stream_test_grant_failure(unsigned){assert(false);}
extern "C" void stream_test_lock(){assert(false);}
extern "C" void stream_test_unlock(){assert(false);}
extern "C" void stream_test_reenter(bool){assert(false);}
extern "C" bool test_home_quiesce(){++grantQuiesces;return false;}
extern "C" void test_legacy_map(unsigned role){
#ifdef RESIDENT_LOADING_TEST
  if(role!=Host && role!=LegacyNext)assert(loadingCalls && loadingPath==std::string(names[role])+".elf");
#endif
  if(role==Legacy || role==LegacyNext)assert(maps[Host]==unmaps[Host] && maps[Child]==unmaps[Child] && maps[Receiver]==unmaps[Receiver]);
  if(role==Host)assert(maps[Legacy]==unmaps[Legacy] && maps[LegacyNext]==unmaps[LegacyNext]);
  if(role==LegacyNext)assert(maps[Legacy]==unmaps[Legacy]);
  ++maps[role];event(role,"map");
}
extern "C" void test_legacy_unmap(unsigned role){
  // Retained ELF destructors may run during OS process shutdown, outside Runtime.
  if(finished)return;
  ++unmaps[role];event(role,"unmap");
  assert(maps[role]==unmaps[role]);
  current(role).unloaded=true;
#ifdef RISC_NATIVE_APP_MEMORY_TEST
  assert(!memoryLive.count(current(role).allocation));
#endif
}
extern "C" int test_legacy_init(unsigned role){
  activeRole=role;++starts[role];event(role,"init");
  assert(starts[role]<5 && snapshotCount<20);
  if(role==Legacy)assert(maps[Host]==unmaps[Host] && maps[Child]==unmaps[Child] && maps[Receiver]==unmaps[Receiver]);
  if(role==Host && starts[Host]>1)assert(maps[Legacy]==unmaps[Legacy]);
  api=risc_runtime_get_api(1);assert(api && api->struct_size>=RISC_RUNTIME_RESIDENT_SHELL_V1_SIZE);
  risc_resident_client_v1 absent{};absent.struct_size=sizeof(absent);assert(!api->resident_shell(&absent));
  assert(!api->request_default());
  auto& context=snapshots[snapshotCount];context.role=role;currentSnapshot[role]=snapshotCount++;
  context.grant=acquire("storage.key-value");context.kv=*static_cast<const risc_key_value_v1*>(context.grant.api);
  context.streams.struct_size=sizeof(context.streams);assert(api->stream_client(&context.streams));
  auto grant=acquire("test.stream");context.opened.struct_size=sizeof(context.opened);uint32_t request=42;
  assert(context.streams.open(context.streams.context,&grant,&request,sizeof(request),100,&context.opened)==RISC_STREAM_OK);
#ifdef RISC_NATIVE_APP_MEMORY_TEST
  context.allocation=native_app_psram_alloc(37+role);assert(context.allocation);std::memset(context.allocation,0x71+role,37+role);
#endif
  checkStale(currentSnapshot[role]);
  if(target(role,"grant-retained"))(void)acquire("test.home");
  if(target(role,"init-retained")){exitSafe=false;return -1;}
  const bool fileFailure=(residentFile() && role==Legacy) || (legacyFile() && role==Child);
  if(target(role,"init-failure") || (fileFailure && has("init-failure")) || (role==Host && starts[Host]==2 && is("reload-host-init-failure")))return -1;
  return 0;
}
extern "C" void test_legacy_fini(unsigned role){
  activeRole=role;++finis[role];event(role,"fini");
  risc_resident_client_v1 absent{};absent.struct_size=sizeof(absent);assert(!api->resident_shell(&absent));
  assert(!api->request_default());
  if(target(role,"fini-retained"))exitSafe=false;
#ifdef RISC_NATIVE_APP_MEMORY_TEST
  if(target(role,"memory-retained"))resident_test_memory_lock=false;
#endif
}
extern "C" int32_t test_legacy_dispatch(unsigned* visits,const risc_resident_request_v1* request,risc_resident_reply_v1*){
  assert(*visits==1 && request->reason==RISC_RESIDENT_CHECKPOINT_CONTROLS);
  assert(maps[Host]==unmaps[Host]+1 && maps[Legacy]==unmaps[Legacy]);
#ifdef RISC_NATIVE_APP_MEMORY_TEST
  assert(memoryLive.count(current(Host).allocation));
  assert(*static_cast<unsigned char*>(current(Host).allocation)==0x71);
#endif
  ++dispatches;return RISC_RESIDENT_OK;
}
#ifdef RESIDENT_LOADING_TEST
extern "C" int32_t test_legacy_loading(void* context,const char* path){
  assert(context && *static_cast<unsigned*>(context)==1 && path && path[0]!='/');
  for(unsigned role=1;role<Roles;++role)assert(maps[role]==unmaps[role]);
  const auto& host=current(Host);uint32_t value=0,n=0;
  assert(host.kv.get(host.kv.context,"test",&value,4,&n)==RISC_KEY_VALUE_OK && value==11);
  risc_resident_client_v1 currentHost{};currentHost.struct_size=sizeof(currentHost);
  assert(api->resident_shell(&currentHost) && currentHost.invocation==host.resident.invocation);
  assert(currentHost.request_foreground_exit(currentHost.invocation)==RISC_RESIDENT_DENIED);
  loadingPath=path;++loadingCalls;events.emplace_back("loading:"+loadingPath);
  const bool fileResumeBusy=(is("file-legacy-resident-loading-busy") || is("file-resident-legacy-loading-busy")) && entries[Host]>1 && loadingPath=="child.elf";
  if(loadingBusyProbe || fileResumeBusy || (is("loading-resume-busy") && loadingPath=="receiver.elf"))return RISC_RESIDENT_BUSY;
  return RISC_RESIDENT_OK;
}
#endif
extern "C" void test_legacy_failed(const risc_resident_failure_v1* failure){assert(failure->status<0);++failures;}
extern "C" void test_legacy_main(unsigned role,unsigned* visits,const risc_resident_callbacks_v1* callbacks,char* source,char* receiver){
  activeRole=role;++entries[role];event(role,"entry");assert(++*visits==1);
  auto& context=current(role);checkStale(currentSnapshot[role]);
  context.resident.struct_size=sizeof(context.resident);
  assert(api->resident_shell(&context.resident)==(role!=Legacy && role!=LegacyNext));
  assert(role!=Unlisted);
  if(role==Host){
    assert(context.resident.role==RISC_RESIDENT_ROLE_HOST);context.callbacks=*callbacks;
    assert(context.resident.register_shell(context.resident.invocation,callbacks)==RISC_RESIDENT_OK);
    const bool continuation=entries[Host]>1 && (legacyLaunch() || (residentFile() && !has("home")) || (legacyFile() && entries[Host]==2));
    if(continuation)assert(runHost(context,"legacy.elf")==RISC_RESIDENT_BUSY);
    if(is("unconsumed-continuation") && entries[Host]==2)return;
    if(entries[Host]>1 && (is("legacy-init-failure") || is("legacy-load-failure") || (residentFile() && has("failure")))){
      risc_resident_failure_v1 failure{};failure.struct_size=sizeof(failure);
      assert(context.resident.last_failure(context.resident.invocation,&failure));
      assert(failure.kind==(has("init-failure")?RISC_RESIDENT_FAILURE_INIT:RISC_RESIDENT_FAILURE_LOAD));
      assert(failure.status==RISC_RESIDENT_FAILED && !std::strcmp(failure.application,"legacy.elf"));
    }
    const auto pending=runHost(context,nullptr);
    if(entries[Host]>1){
      if(is("loading-resume-busy") || has("loading-busy")) {
        assert(pending==RISC_RESIDENT_BUSY && runHost(context,nullptr)==RISC_RESIDENT_NO_PENDING && !failures);return;
      }
      if(legacyLaunch() || (residentFile() && !has("home")))assert(pending==RISC_RESIDENT_OK);
      else if(legacyFile() && entries[Host]==2){
        if(has("home-retained"))assert(pending==RISC_RESIDENT_RETAINED);
        else assert(pending==(has("home")?RISC_RESIDENT_OK:RISC_RESIDENT_HANDOFF));
      }else assert(pending==RISC_RESIDENT_NO_PENDING);
      if(pending==RISC_RESIDENT_OK)assert(runHost(context,nullptr)==RISC_RESIDENT_NO_PENDING);
      if(is("repeated-chain") && entries[Host]<4)assert(runHost(context,"child.elf")==RISC_RESIDENT_HANDOFF);
      return;
    }
    assert(pending==RISC_RESIDENT_NO_PENDING);
    for(const char* path:{"unlisted.elf","missing.elf","../legacy.elf","/legacy.elf","host.elf"}){
      assert(runHost(context,path)==RISC_RESIDENT_DENIED);
    }
    assert(!api->request_launch("legacy.elf") && !api->request_default());
    if(is("denied") || is("legacy-empty") || is("legacy-absent")){
      if(!is("denied"))assert(runHost(context,"legacy.elf")==RISC_RESIDENT_DENIED);
      assert(!starts[Legacy]);return;
    }
#ifdef RESIDENT_LOADING_TEST
    loadingBusyProbe=true;
    assert(runHost(context,"legacy.elf")==RISC_RESIDENT_BUSY);
    assert(runHost(context,nullptr)==RISC_RESIDENT_NO_PENDING && !starts[Legacy] && !failures);
    loadingBusyProbe=false;
#endif
    const auto status=runHost(context,startChild()?"child.elf":"legacy.elf");
    if(begins("child-") && terminal())assert(status==RISC_RESIDENT_RETAINED);
    else if(begins("child-") && has("failure"))assert(status==RISC_RESIDENT_FAILED);
    else assert(status==RISC_RESIDENT_HANDOFF);
    if(target(role,"entry-retained"))exitSafe=false;
    return; // HANDOFF requires unwinding the resident host stack immediately.
  }
  if(role==Legacy || role==LegacyNext){
    assert(maps[Host]==unmaps[Host]);
    if(is("legacy-home"))assert(api->request_default());
    if(is("legacy-launch-default"))assert(api->request_launch("host.elf"));
    if(is("legacy-launch-legacy") && role==Legacy)assert(api->request_launch("legacy-next.elf"));
    if(legacyLaunch()){char path[]="receiver.elf";assert(api->request_launch(path));std::memset(path,'x',sizeof(path)-1);}
    if(residentFile())receiveFile(has("home"));
    if(legacyFile()){if(entries[Legacy]==1)queueFile(role,source,receiver,"child");else takeFile();}
    if(is("reload-host-load-failure"))assert(!std::remove((root+"/host.elf").c_str()));
    if(is("legacy-explicit-retained")){assert(api->retain_invocation());return;}
    assert(!api->request_launch("unlisted.elf") && !api->request_launch("../child.elf"));
  }else{
    assert(context.resident.role==RISC_RESIDENT_ROLE_FOREGROUND);
    checkpoint(context);
    if(residentFile()){if(entries[Child]==1)queueFile(role,source,receiver,"legacy");else takeFile();}
    else if(legacyFile())receiveFile(has("home"));
    else if(role==Child){char path[]="legacy.elf";assert(api->request_launch(path));std::memset(path,'z',sizeof(path)-1);}
    assert(!api->request_launch("unlisted.elf"));
  }
  if(target(role,"entry-retained"))exitSafe=false;
}
int main(int argc,char** argv){
  assert(argc==3);root=argv[1];mode=argv[2];setup();
  auto* runtime=new Runtime({owner,health,delay,log,nullptr,&kv,safe});
  const bool malformed=begins("policy-");
  assert(runtime->prepare(root.c_str())!=malformed);
  if(malformed){assert(events.empty());delete runtime;std::printf("Resident legacy: %s PASS\n",mode.c_str());finished=true;return 0;}
  if(is("legacy-load-failure") || is("file-resident-legacy-load-failure"))assert(!std::remove((root+"/legacy.elf").c_str()));
  if(is("child-load-failure") || is("file-legacy-resident-load-failure"))assert(!std::remove((root+"/child.elf").c_str()));
  const bool hostFailure=is("host-init-failure") || begins("reload-host-") || is("unconsumed-continuation");
  assert(runtime->run()==!(terminal() || hostFailure));assert(runtime->retained()==terminal());
  if(is("unconsumed-continuation"))assert(std::strstr(runtime->error(),"did not consume pending continuation"));
  assert(!risc_runtime_get_api(1));
  if(snapshotCount)checkStale(size_t(-1));
  if(terminal()){
    assert(starts[Host]==(legacyFile()?2u:1u));
    assert(!starts[Unlisted] && !starts[Receiver]);
    if(begins("host-"))assert(!starts[Legacy] && !starts[Child] && unmaps[Host]==0);
    if(begins("child-"))assert(!starts[Legacy] && unmaps[Host]==0 && unmaps[Child]==0);
    if(begins("legacy-") || residentFile())assert(unmaps[Host]==1 && unmaps[Legacy]==0);
    if(has("grant-retained"))assert(grantQuiesces==1);
    if(has("unload-retained")){
      const unsigned role=begins("host-")?Host:begins("child-")?Child:Legacy;
      assert(unloadAttempts[role]==1 && finis[role]==1 && unmaps[role]==0);
    }
    if(has("init-retained") || has("entry-retained") || is("legacy-explicit-retained")){
      const unsigned role=begins("host-")?Host:begins("child-")?Child:Legacy;assert(finis[role]==0);
    }
    const size_t before=events.size();assert(!runtime->run());assert(events.size()==before);
    retainedRuntime=runtime;
  }else{
    for(unsigned role=0;role<Roles;++role)assert(maps[role]==unmaps[role]);
    if(is("denied") || is("legacy-empty") || is("legacy-absent") || is("host-init-failure") || (begins("child-") && has("failure")))assert(starts[Host]==1 && starts[Legacy]==0);
    else if(is("repeated-chain"))assert(starts[Host]==4 && entries[Child]==3 && entries[Legacy]==3 && entries[Receiver]==3 && handoffs==3 && resumes==3 && dispatches==6);
    else if(has("loading-busy"))assert(starts[Host]==2 && entries[Legacy]==1 && entries[Child]==(residentFile()?1u:0u) && !fileResults && handoffs==1);
    else if(legacyFile() && !has("home"))assert(starts[Host]==3 && entries[Legacy]==2 && fileResults==1 && handoffs==2);
    else if(legacyFile())assert(starts[Host]==2 && entries[Legacy]==1 && fileResults==0 && handoffs==1);
    else {
      assert(starts[Host]==(is("reload-host-load-failure")?1u:2u));
      assert(handoffs==1);
      if(residentFile())assert(entries[Child]==(has("home")?1u:2u) && fileResults==(has("home")?0u:1u));
      if(is("legacy-launch-resident"))assert(entries[Receiver]==1 && resumes==1);
      if(is("loading-resume-busy"))assert(entries[Receiver]==0 && resumes==0);
      if(is("legacy-launch-legacy"))assert(entries[Legacy]==1 && entries[LegacyNext]==1 && position("legacy:unmap")<position("legacy-next:map"));
      if(is("unconsumed-continuation"))assert(entries[Receiver]==0 && resumes==0);
    }
    if(starts[Legacy])assert(position("host:unmap")<position("legacy:init"));
    if(startChild() && starts[Legacy])assert(position("child:unmap")<position("host:unmap"));
    assert(streamCloses==snapshotCount);delete runtime;
  }
#ifdef RISC_NATIVE_APP_MEMORY_TEST
  if(!terminal())assert(memoryLive.empty());
  else for(size_t n=0;n<snapshotCount;++n)if(!snapshots[n].unloaded){
    assert(bool(memoryLive.count(snapshots[n].allocation))!=snapshots[n].memoryEnded);
  }
#endif
  finished=true;std::printf("Resident legacy: %s PASS\n",mode.c_str());
}
