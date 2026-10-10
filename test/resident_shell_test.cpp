#include "bootstrap/Runtime.h"
#include <RiscResidentShellV1.h>
#include <RiscStreamClientV1.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
static unsigned failNothrow=0;
static bool failMetadata=false;
void* operator new(size_t n,const std::nothrow_t&) noexcept {
 if(failNothrow && !--failNothrow)return nullptr;
 try{return ::operator new(n);}catch(...){return nullptr;}
}
namespace RiscBoot {void* metadataTestAllocate(size_t n){return failMetadata?nullptr:std::malloc(n);}}
#ifdef RISC_NATIVE_APP_MEMORY_TEST
#include "native/NativeAppMemory.h"
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <cstdlib>
#include <unordered_set>
bool resident_test_memory_lock=true;
static bool memoryOom=false;
static std::unordered_set<void*> memoryLive;
static unsigned char* appMemory[4]{};
static void* overlayMemory=nullptr;
static unsigned activeAppRole=0;
#ifdef RESIDENT_LOADING_TEST
static void* loadingMemory[16]{};
static unsigned loadingMemoryCount=0;
#endif
extern "C" void* heap_caps_malloc(size_t n,uint32_t){if(memoryOom)return nullptr;void*p=std::malloc(n);assert(p);memoryLive.insert(p);return p;}
extern "C" void* heap_caps_calloc(size_t n,size_t z,uint32_t caps){if(z && n>SIZE_MAX/z)return nullptr;void*p=heap_caps_malloc(n*z,caps);if(p)std::memset(p,0,n*z);return p;}
extern "C" void* heap_caps_realloc(void*p,size_t n,uint32_t caps){if(!p)return heap_caps_malloc(n,caps);if(memoryOom)return nullptr;assert(memoryLive.erase(p));p=std::realloc(p,n);assert(p);memoryLive.insert(p);return p;}
extern "C" void heap_caps_free(void*p){if(p){assert(memoryLive.erase(p));std::free(p);}}
extern "C" void* pressure_malloc(size_t n){return heap_caps_malloc(n,0);}
extern "C" void* pressure_calloc(size_t n,size_t z){return heap_caps_calloc(n,z,0);}
extern "C" void* pressure_realloc(void*p,size_t n){return heap_caps_realloc(p,n,0);}
extern "C" void pressure_free(void*p){heap_caps_free(p);}
extern "C" size_t heap_caps_get_free_size(uint32_t){return 0;}
extern "C" size_t heap_caps_get_largest_free_block(uint32_t){return 0;}
extern "C" void vTaskDelay(TickType_t){}
#endif
using namespace RiscBoot;
static_assert(RISC_RUNTIME_DEFAULT_REQUEST_V1_SIZE==offsetof(risc_runtime_api_v1,resident_shell),"Preserve the complete .77 Runtime prefix");
namespace {
std::string root,mode;
bool owned=true,exitSafe=true;
unsigned starts[4]{},entries[4]{},finis[4]{},dispatches=0,failures=0,quiesces=0;
Runtime* volatile retainedRuntime=nullptr;
Runtime* runningRuntime=nullptr;
RiscRetainedWake::Image wakeImage{};
RiscRetainedWake::Store wakeStore(wakeImage);
risc_retained_wake_api_v1 hostWake{};
const risc_runtime_api_v1* api=nullptr;
risc_resident_client_v1 host{},child{},oldChild{};
risc_runtime_capability_v1 hostGrant{},childGrant{};
risc_key_value_v1 hostKv{},childKv{},oldKv{};
risc_stream_client_v1 hostStreams{},childStreams{};
risc_storage_volume_api_v1 hostFiles{},childFiles{};
uint32_t hostDir=0,childDir=0;
risc_stream_opened_v1 hostOpened{},childOpened{};
risc_runtime_capability_v1 hostStreamGrant{};
bool childClosing=false;
bool streamMode(){return mode=="streams" || mode=="stream-retained";}
unsigned* hostStatic=nullptr;
#ifdef RESIDENT_LOADING_TEST
unsigned loadingCalls=0,maps[4]{},unmaps[4]{};
bool finished=false;
std::string loadingPath;
#endif
bool owner(){return owned;}
bool health(risc_runtime_health_v1*){return true;}
void delay(uint32_t){}
bool log(const char*){return true;}
bool safe(){return exitSafe;}
int32_t get(void*,uint32_t ns,const char*,void* out,uint32_t cap,uint32_t* size){assert(cap>=4);memcpy(out,&ns,4);*size=4;return RISC_KEY_VALUE_OK;}
int32_t put(void*,uint32_t,const char*,const void*,uint32_t){return RISC_KEY_VALUE_OK;}
KeyValueBackend kv{nullptr,get,put};
bool prior(risc_resident_failure_v1* out){out->kind=RISC_RESIDENT_FAILURE_PRIOR_RESET;out->native_reason=77;return true;}
bool terminal(){return mode=="child-retained" || mode=="callback-retained" || mode=="fini-retained" || mode=="native-retained" || mode=="invalid-callback" || mode=="provider-retained" || mode=="stream-retained" || mode=="memory-lock-retained" || mode=="switch-lock-retained" || mode=="failure-callback-retained" || mode=="prelaunch-retained" || mode=="register-retained" || mode=="release-retained" || mode=="loading-retained" || mode=="loading-invalid" || mode=="loading-native-retained" || mode=="loading-provider-retained";}
void save(const char* path,JsonDocument& doc){std::string bytes;serializeJson(doc,bytes);std::ofstream(root+"/"+path)<<bytes;}
void setup(){
 std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
 JsonDocument boot;boot["board"]="board.json";boot["default_app"]="host.elf";boot["provider_activation"]="demand";
 auto drivers=boot["drivers"].to<JsonArray>();
 if(mode=="provider-retained" || mode=="release-retained" || mode=="loading-provider-retained"){
  drivers.add<JsonObject>()["manifest"]="provider.json";
  std::ofstream(root+"/provider.json")<<R"({"type":"driver","id":"home-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"provider.elf","requires":[],"provides":[{"capability":"test.home","api":1}]})";
 }
 if(streamMode()) {
  drivers.add<JsonObject>()["manifest"]="stream.json";drivers.add<JsonObject>()["manifest"]="root.json";
  std::ofstream(root+"/stream.json")<<R"({"type":"driver","id":"stream","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"stream.elf","requires":[{"capability":"test.root","api":1}],"provides":[{"capability":"test.stream","api":1}]})";
  std::ofstream(root+"/root.json")<<R"({"type":"driver","id":"stream-root","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"root.elf","requires":[],"provides":[{"capability":"test.root","api":1}]})";
 }
 auto resident=boot["resident_shell"].to<JsonObject>();resident["api"]=1;resident["host"]="host.elf";
 auto foreground=resident["foreground"].to<JsonArray>();foreground.add("child.elf");foreground.add("next.elf");foreground.add("bad.elf");
 if(mode=="policy-host")resident["host"]="child.elf";
 if(mode=="policy-duplicate")foreground.add("child.elf");
 if(mode=="policy-missing")foreground.add("unknown.elf");
 if(mode=="policy-null")boot["resident_shell"]=nullptr;
 auto policies=boot["app_capabilities"].to<JsonArray>();
 const char* names[]={"host","child","next","bad"};
 for(unsigned i=0;i<4;++i){
  JsonDocument app;app["type"]="application";app["id"]=names[i];app["version"]="1.0.0";app["architecture"]="xtensa-esp32s3";
  app["file_name"]=std::string(names[i])+".elf";app["entry"]="app_main";
  auto requirements=app["requires"].to<JsonArray>();auto policy=policies.add<JsonObject>();policy["manifest"]=std::string(names[i])+".json";
  auto grants=policy["grants"].to<JsonArray>();
  auto add=[&](const char* cap,uint32_t ns){auto req=requirements.add<JsonObject>();req["capability"]=cap;req["api"]=1;auto grant=grants.add<JsonObject>();grant["capability"]=cap;grant["api"]=1;grant["instance_id"]=ns;};
  if(mode=="wake-host" && i==0)add("runtime.retained-wake",0);
  if(mode=="policy-wake-child" && i==1)add("runtime.retained-wake",0);
  if(streamMode())add("test.stream",0);
  add("storage.key-value",11+i);add("storage.installed-files",0);add("file.open",0);
  if(i==2)app["supported_file_types"].to<JsonArray>().add(".txt");
  if((i==1 && (mode=="provider-retained" || mode=="release-retained")) || (i==0 && mode=="loading-provider-retained"))add("test.home",0);
  save((std::string(names[i])+".json").c_str(),app);
 }
 save("boot.json",boot);
 std::ofstream(root+"/cohort.json")<<R"({"schema":"riscrte.cohort","schema_version":1,"product":"test","version":"1.0.0","runtime_version":"0.1.82","source_repo":"example/test","source_revision":"1111111111111111111111111111111111111111","layout":"riscrte-paired-16m-v1","store_abi":1,"firmware_size":32,"firmware_sha256":"1111111111111111111111111111111111111111111111111111111111111111"})";
}
uint32_t read(const risc_key_value_v1& table){uint32_t value=0,n=0;assert(table.get(table.context,"test",&value,4,&n)==RISC_KEY_VALUE_OK && n==4);return value;}
void denied(const risc_key_value_v1& table){uint32_t value=0,n=0;assert(table.get(table.context,"test",&value,4,&n)==RISC_KEY_VALUE_CONTEXT);}
risc_runtime_capability_v1 acquire(const char* cap){risc_runtime_capability_v1 g{};g.struct_size=sizeof(g);assert(api->acquire(cap,1,0,&g));return g;}
int32_t dispatch(void* context,const risc_resident_request_v1* request,risc_resident_reply_v1* reply){
 assert(context==hostStatic && *hostStatic==1 && request->reason);++dispatches;
 assert(!runningRuntime->residentResetSafe());
 assert(read(hostKv)==11);denied(childKv);auto childCopy=childGrant;assert(!api->release(&childCopy));
 uint32_t count=0;assert(childStreams.read(childStreams.context,1,nullptr,0,&count)==RISC_STREAM_CLOSED);
 risc_resident_result_v1 nested{};nested.struct_size=sizeof(nested);assert(host.run_foreground(host.invocation,"child.elf",&nested)==RISC_RESIDENT_BUSY);
 assert(nested.status==RISC_RESIDENT_BUSY);
 risc_resident_reply_v1 nestedReply{sizeof(nestedReply),0};assert(child.checkpoint(child.invocation,request,&nestedReply)==RISC_RESIDENT_DENIED);
 assert(!api->request_launch("child.elf") && !api->request_default());
 if(streamMode() && dispatches==1) {
  char byte=0;uint32_t count=0;
  assert(hostStreams.read(hostStreams.context,hostOpened.rx,&byte,1,&count)==RISC_STREAM_OK && count==1 && byte=='H');
 }
 if(mode=="busy")return RISC_RESIDENT_BUSY;
 if(mode=="callback-retained"){assert(api->retain_invocation());return RISC_RESIDENT_RETAINED;}
 if(mode=="invalid-callback")return 99;
#ifdef RISC_NATIVE_APP_MEMORY_TEST
 assert(appMemory[0][0]==0x71 && appMemory[activeAppRole][0]==0x71+activeAppRole);
 overlayMemory=native_app_psram_alloc(128);assert(overlayMemory);
 if(mode=="switch-lock-retained")resident_test_memory_lock=false;
#endif
 if(mode=="exit")assert(host.request_foreground_exit(host.invocation)==RISC_RESIDENT_OK);
 reply->flags=RISC_RESIDENT_REPLY_REDRAW|RISC_RESIDENT_REPLY_CONFIGURATION_CHANGED;return RISC_RESIDENT_OK;
}
#ifdef RISC_NATIVE_APP_MEMORY_TEST
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(){return reinterpret_cast<void*>(owned?1:2);}
#endif
#ifdef RESIDENT_LOADING_TEST
int32_t loading(void* context,const char* path){
 assert(context==hostStatic && *hostStatic==1 && path && strlen(path)<=192 && path[0]!='/');
 for(unsigned role=1;role<4;++role)assert(maps[role]==unmaps[role]);
 assert(!runningRuntime->residentResetSafe());
 assert(read(hostKv)==11);if(childKv.context)denied(childKv);
 risc_resident_client_v1 current{};current.struct_size=sizeof(current);
 assert(api->resident_shell(&current) && current.role==RISC_RESIDENT_ROLE_HOST && current.invocation==host.invocation);
 risc_resident_result_v1 result{};result.struct_size=sizeof(result);
 assert(host.run_foreground(host.invocation,"child.elf",&result)==RISC_RESIDENT_BUSY);
 assert(host.register_shell(host.invocation,nullptr)==RISC_RESIDENT_BUSY);
 assert(host.request_foreground_exit(host.invocation)==RISC_RESIDENT_DENIED);
 assert(!api->request_launch("next.elf") && !api->request_default());
 owned=false;assert(!api->resident_shell(&current));owned=true;
 loadingPath=path;++loadingCalls;
#ifdef RISC_NATIVE_APP_MEMORY_TEST
 for(unsigned i=0;i<loadingMemoryCount;++i)assert(memoryLive.count(loadingMemory[i]));
 assert(loadingMemoryCount<16);
 loadingMemory[loadingMemoryCount]=native_app_psram_alloc(13);assert(loadingMemory[loadingMemoryCount++]);
#endif
 if(mode=="loading-busy" || (mode=="loading-chain-busy" && loadingCalls==2))return RISC_RESIDENT_BUSY;
 if(mode=="loading-retained"){assert(api->retain_invocation());return RISC_RESIDENT_RETAINED;}
 if(mode=="loading-invalid")return 99;
 if(mode=="loading-native-retained")exitSafe=false;
 if(mode=="loading-provider-retained") {auto grant=acquire("test.home");assert(!api->release(&grant));}
 return RISC_RESIDENT_OK;
}
#endif
void failed(void*,const risc_resident_failure_v1* failure){++failures;assert(failure->status<0 && read(hostKv)==11);if(mode=="failure-callback-retained"){assert(api->retain_invocation());return;}assert(!terminal());}
}
extern "C" const char* stream_test_mode(){return mode=="stream-retained" && childClosing?"close-fail":"normal";}
extern "C" void stream_test_event(const char*){}
extern "C" void stream_test_slow(){assert(false);}
extern "C" void stream_test_provider(const void*){}
extern "C" void stream_test_root(const void*){}
extern "C" void stream_test_grant_failure(unsigned){assert(false);}
extern "C" void stream_test_lock(){assert(false);}
extern "C" void stream_test_unlock(){assert(false);}
extern "C" void stream_test_reenter(bool){assert(false);}
extern "C" bool test_home_quiesce(){++quiesces;return false;}
#ifdef RESIDENT_LOADING_TEST
extern "C" void test_resident_map(unsigned role){
 if(role && mode!="loading-old-prefix" && mode!="loading-partial-prefix" && mode!="loading-null") {
  assert(loadingCalls && loadingPath==std::string(role==1?"child":role==2?"next":"bad")+".elf");
 }
 ++maps[role];
}
extern "C" void test_resident_unmap(unsigned role){if(!finished)++unmaps[role];}
#endif
extern "C" int test_resident_init(unsigned role){
 ++starts[role];api=risc_runtime_get_api(1);assert(api && api->struct_size>=RISC_RUNTIME_RESIDENT_SHELL_V1_SIZE);
#ifdef RISC_NATIVE_APP_MEMORY_TEST
 activeAppRole=role;
 appMemory[role]=static_cast<unsigned char*>(native_app_psram_alloc(37+role));assert(appMemory[role]);appMemory[role][0]=0x71+role;
#endif
 risc_resident_client_v1 no{};no.struct_size=sizeof(no);assert(!api->resident_shell(&no));
 if(role==1 && mode=="init-failure"){childGrant=acquire("storage.key-value");childKv=*static_cast<const risc_key_value_v1*>(childGrant.api);return -1;}
 return 0;
}
extern "C" void test_resident_fini(unsigned role){
 ++finis[role];risc_resident_client_v1 no{};no.struct_size=sizeof(no);assert(!api->resident_shell(&no));
 if(role==1 && mode=="fini-retained")exitSafe=false;
#ifdef RISC_NATIVE_APP_MEMORY_TEST
 if(role==1 && mode=="memory-lock-retained")resident_test_memory_lock=false;
#endif
}
extern "C" void test_resident_main(unsigned role,unsigned* visits){
 ++entries[role];assert(++*visits==1);
 risc_resident_client_v1 client{};client.struct_size=sizeof(client);assert(api->resident_shell(&client));
 if(!role){
  host=client;hostStatic=visits;assert(host.role==RISC_RESIDENT_ROLE_HOST);
  hostGrant=acquire("storage.key-value");hostKv=*static_cast<const risc_key_value_v1*>(hostGrant.api);
  auto files=acquire("storage.installed-files");hostFiles=*static_cast<const risc_storage_volume_api_v1*>(files.api);hostDir=hostFiles.dir_open(hostFiles.context,"/");assert(hostDir);
  hostStreams.struct_size=sizeof(hostStreams);assert(api->stream_client(&hostStreams));
  if(streamMode()) {
   hostStreamGrant=acquire("test.stream");hostOpened.struct_size=sizeof(hostOpened);uint32_t q=42,n=0;
   assert(hostStreams.open(hostStreams.context,&hostStreamGrant,&q,sizeof(q),100,&hostOpened)==RISC_STREAM_OK);
   assert(hostStreams.write(hostStreams.context,hostOpened.tx,"H",1,&n)==RISC_STREAM_OK && n==1);api->yield_ms(1);
  }
  risc_resident_failure_v1 previous{};previous.struct_size=sizeof(previous);assert(host.last_failure(host.invocation,&previous) && previous.kind==RISC_RESIDENT_FAILURE_PRIOR_RESET && previous.native_reason==77);
  risc_resident_callbacks_v1 callbacks{1,sizeof(callbacks),visits,dispatch,failed};
#ifdef RESIDENT_LOADING_TEST
  callbacks.loading=mode=="loading-null"?nullptr:loading;
  if(mode=="loading-partial-prefix")callbacks.struct_size=RISC_RESIDENT_CALLBACKS_LOADING_V1_SIZE-1;
#endif
  owned=false;assert(host.register_shell(host.invocation,&callbacks)==RISC_RESIDENT_DENIED);owned=true;
  if(mode=="register-retained") {
   exitSafe=false;assert(host.register_shell(host.invocation,&callbacks)==RISC_RESIDENT_RETAINED);
   previous.struct_size=sizeof(previous);assert(host.last_failure(host.invocation,&previous) && previous.status==RISC_RESIDENT_RETAINED);return;
  }
#ifdef RESIDENT_LOADING_TEST
  if(mode=="loading-old-prefix") {
    // The real pre-extension allocation ends here. ASan catches any suffix read.
    struct OldCallbacks {uint32_t api_version,struct_size;void* context;decltype(callbacks.dispatch) dispatch;decltype(callbacks.failed) failed;};
    static_assert(sizeof(OldCallbacks)==RISC_RESIDENT_CALLBACKS_V1_SIZE,"preserved resident callback prefix");
    OldCallbacks old{1,sizeof(old),visits,dispatch,failed};
    assert(host.register_shell(host.invocation,reinterpret_cast<const risc_resident_callbacks_v1*>(&old))==RISC_RESIDENT_OK);
  }else
#endif
  assert(host.register_shell(host.invocation,&callbacks)==RISC_RESIDENT_OK);
  assert(host.register_shell(host.invocation,&callbacks)==RISC_RESIDENT_INVALID);
  risc_resident_result_v1 result{};result.struct_size=sizeof(result);
  assert(host.run_foreground(host.invocation,"host.elf",&result)==RISC_RESIDENT_DENIED);
  assert(result.status==RISC_RESIDENT_DENIED);
  assert(host.run_foreground(host.invocation,"../child.elf",&result)==RISC_RESIDENT_DENIED);
  assert(result.status==RISC_RESIDENT_DENIED);
  if(mode=="wake-host") {
   auto grant=acquire("runtime.retained-wake");hostWake=*static_cast<const risc_retained_wake_api_v1*>(grant.api);
   risc_retained_wake_record_v1 record{sizeof(record),1,1,1,{73}};assert(hostWake.stage(hostWake.context,&record)==RISC_RETAINED_WAKE_OK);
  }
  const char* name=mode=="descriptor"?"bad.elf":"child.elf";
  for(unsigned i=0;i<(mode=="normal"?3u:1u);++i){
   if(mode=="context-oom")failNothrow=1;
   if(mode=="inventory-oom")failNothrow=2;
   if(mode=="names-oom")failMetadata=true;
#ifdef RISC_NATIVE_APP_MEMORY_TEST
   if(mode=="memory-oom")memoryOom=true;
#endif
   assert(runningRuntime->residentResetSafe());
   if(mode=="prelaunch-retained")exitSafe=false;
   result.struct_size=sizeof(result);int32_t rc=host.run_foreground(host.invocation,name,&result);
   failNothrow=0;failMetadata=false;
#ifdef RISC_NATIVE_APP_MEMORY_TEST
   memoryOom=false;
   if(!terminal())assert(memoryLive.count(appMemory[0]) && appMemory[0][0]==0x71);
#endif
   if(terminal()){
    assert(rc==RISC_RESIDENT_RETAINED && !api->resident_shell(&client));
    previous.struct_size=sizeof(previous);assert(host.last_failure(host.invocation,&previous) && previous.status==RISC_RESIDENT_RETAINED);return;
   }
#ifdef RESIDENT_LOADING_TEST
   if(mode=="loading-busy" || mode=="loading-chain-busy") {
    assert(rc==RISC_RESIDENT_BUSY && !failures);
    assert(host.run_foreground(host.invocation,nullptr,&result)==RISC_RESIDENT_NO_PENDING);
    assert(loadingCalls==(mode=="loading-busy"?1u:2u));
    assert(starts[1]==(mode=="loading-busy"?0u:1u) && !starts[2]);
    break;
   }
#endif
   const bool metadataFailure=mode=="context-oom" || mode=="inventory-oom" || mode=="names-oom";
   const bool failure=mode=="descriptor" || mode=="init-failure" || mode=="load-failure" || mode=="memory-oom" || metadataFailure;
   assert(rc==(mode=="descriptor"?RISC_RESIDENT_INCOMPATIBLE:failure?RISC_RESIDENT_FAILED:RISC_RESIDENT_OK));
   assert(*visits==1 && read(hostKv)==11 && (metadataFailure || result.invocation) && runningRuntime->residentResetSafe());
   assert(result.status==rc);
   if(metadataFailure)assert(result.failure.kind==RISC_RESIDENT_FAILURE_ALLOCATION && !result.invocation);
   if(mode=="wake-host"){wakeStore.commit();assert(wakeImage.magic && wakeImage.record.payload[0]==73);wakeStore.rollback();}
   if(childKv.context)denied(childKv); // Child copied native context has no host authority.
   if(failure)assert(result.failure.kind && failures==1);
  }
  risc_storage_dirent_v1 item{};assert(hostFiles.dir_next(hostFiles.context,hostDir,&item));hostFiles.dir_close(hostFiles.context,hostDir);
  if(streamMode())assert(api->release(&hostStreamGrant));
  assert(api->release(&files));assert(api->release(&hostGrant));return;
 }
 assert(client.role==RISC_RESIDENT_ROLE_FOREGROUND);child=client;assert(!runningRuntime->residentResetSafe());
 if(oldChild.invocation){risc_resident_request_v1 q{sizeof(q),1,0,0};risc_resident_reply_v1 a{sizeof(a),0};assert(oldChild.checkpoint(oldChild.invocation,&q,&a)==RISC_RESIDENT_DENIED);denied(oldKv);}
 childGrant=acquire("storage.key-value");childKv=*static_cast<const risc_key_value_v1*>(childGrant.api);assert(read(childKv)==11+role);denied(hostKv);
 auto staleHost=hostGrant;assert(!api->release(&staleHost));
 childStreams.struct_size=sizeof(childStreams);assert(api->stream_client(&childStreams));
 if(streamMode()) {
  auto streamGrant=acquire("test.stream");childOpened.struct_size=sizeof(childOpened);uint32_t q=42,n=0;
  assert(childStreams.open(childStreams.context,&streamGrant,&q,sizeof(q),100,&childOpened)==RISC_STREAM_OK);
  assert(childStreams.write(childStreams.context,childOpened.tx,"C",1,&n)==RISC_STREAM_OK && n==1);api->yield_ms(1);
  char byte=0;assert(hostStreams.read(hostStreams.context,hostOpened.rx,&byte,1,&n)==RISC_STREAM_CLOSED);
  // Leave this live for ordinary automatic child cleanup.
 }
 auto files=acquire("storage.installed-files");childFiles=*static_cast<const risc_storage_volume_api_v1*>(files.api);childDir=childFiles.dir_open(childFiles.context,"/");assert(childDir);
 if((mode=="loading-chain" || mode=="loading-chain-busy") && role==1){char next[]="next.elf";assert(api->request_launch(next));memset(next,'x',sizeof(next)-1);}
 else if(mode=="file-open"){
  auto fg=acquire("file.open");auto* file=static_cast<const t5_file_open_api_v1*>(fg.api);
  if(role==1 && entries[role]==1)assert(file->open_request("/sd/sample.txt","next",41));
  else if(role==2){char path[512];assert(file->source_path_get(path,sizeof(path)) && !strcmp(path,"/sd/sample.txt"));}
  else {int32_t error;uint64_t cookie;assert(file->open_take_result(&error,&cookie) && !error && cookie==41);}
  assert(api->release(&fg));
 }else if(mode=="home"){assert(api->request_default());}
 else if(mode=="child-retained"){assert(api->retain_invocation());return;}
 else {
  if(mode=="native-retained")exitSafe=false;
  unsigned model=73;
  for(unsigned i=0;i<4;++i){
   risc_resident_request_v1 request{sizeof(request),RISC_RESIDENT_CHECKPOINT_CONTROLS,0,0};risc_resident_reply_v1 reply{sizeof(reply),0};
   owned=false;assert(client.checkpoint(client.invocation,&request,&reply)==RISC_RESIDENT_DENIED);owned=true;
   const int32_t rc=client.checkpoint(client.invocation,&request,&reply);
   if(terminal() && mode!="fini-retained" && mode!="provider-retained" && mode!="stream-retained" && mode!="memory-lock-retained" && mode!="release-retained"){assert(rc==RISC_RESIDENT_RETAINED);return;}
   assert(rc==(mode=="busy"?RISC_RESIDENT_BUSY:mode=="exit"?RISC_RESIDENT_EXIT:RISC_RESIDENT_OK));
   assert(model==73 && *visits==1 && read(childKv)==11+role);denied(hostKv);
#ifdef RISC_NATIVE_APP_MEMORY_TEST
   assert(appMemory[role][0]==0x71+role);
   if(overlayMemory){native_app_memory_free(overlayMemory);overlayMemory=nullptr;}
#endif
   if(mode=="exit") {
    const unsigned before=dispatches;assert(client.checkpoint(client.invocation,&request,&reply)==RISC_RESIDENT_EXIT);
    assert(dispatches==before);break;
   }
  }
 }
 if(streamMode()) {
  char byte=0;uint32_t n=0;
  assert(childStreams.read(childStreams.context,childOpened.rx,&byte,1,&n)==RISC_STREAM_OK && n==1 && byte=='C');
  childClosing=true;
 }
 if(mode=="release-retained") {
  auto grant=acquire("test.home");assert(!api->release(&grant));assert(!risc_runtime_get_api(1));
#ifdef RISC_NATIVE_APP_MEMORY_TEST
  const size_t live=memoryLive.size();native_app_memory_free(appMemory[1]);native_app_memory_free(appMemory[0]);assert(memoryLive.size()==live);
#endif
  return;
 }
 if(mode=="provider-retained")(void)acquire("test.home");
 risc_storage_dirent_v1 item{};assert(childFiles.dir_next(childFiles.context,childDir,&item));childFiles.dir_close(childFiles.context,childDir);
 oldChild=child;oldKv=childKv;assert(api->release(&files));assert(api->release(&childGrant));
}
int main(int argc,char**argv){
 assert(argc==3);root=argv[1];mode=argv[2];setup();
 wakeStore.boot(RISC_BOOT_POWER_ON);
 auto* runtime=new Runtime({owner,health,delay,log,nullptr,&kv,safe,nullptr,nullptr,nullptr,&wakeStore,nullptr,nullptr,prior});runningRuntime=runtime;
 const bool malformed=mode.rfind("policy-",0)==0;
 assert(runtime->prepare(root.c_str())!=malformed);if(malformed){delete runtime;puts("Resident policy refusal PASS");return 0;}
 if(mode=="load-failure" || mode=="failure-callback-retained")std::remove((root+"/child.elf").c_str());
 assert(runtime->run()!=terminal());assert(runtime->retained()==terminal());assert(starts[0]==1 && entries[0]==1);
 assert(finis[0]==(terminal()?0u:1u));
 if(mode=="normal")assert(starts[1]==3 && finis[1]==3 && dispatches==12);
 if(mode=="file-open")assert(starts[1]==2 && starts[2]==1 && finis[1]==2 && finis[2]==1);
 if(mode=="child-retained" || mode=="callback-retained" || mode=="native-retained" || mode=="invalid-callback")assert(finis[1]==0);
 if(mode=="provider-retained" || mode=="release-retained" || mode=="loading-provider-retained")assert(quiesces==1);
 risc_resident_failure_v1 failure{};failure.struct_size=sizeof(failure);assert(!host.last_failure(host.invocation,&failure));
#ifdef RESIDENT_LOADING_TEST
 if(mode=="loading-old-prefix" || mode=="loading-partial-prefix" || mode=="loading-null")assert(!loadingCalls);
 if(mode=="normal" || mode=="file-open")assert(loadingCalls==3);
 if(mode=="loading-chain")assert(loadingCalls==2 && starts[1]==1 && starts[2]==1);
 if(mode.rfind("loading-",0)==0 && terminal())assert(!starts[1] && !failures && !finis[0] && !unmaps[0]);
 finished=true;
#endif
 if(terminal())retainedRuntime=runtime;else delete runtime;
#ifdef RISC_NATIVE_APP_MEMORY_TEST
 if(terminal())assert(memoryLive.count(appMemory[0]) && memoryLive.size()>=(mode=="prelaunch-retained" || mode=="register-retained" || mode=="failure-callback-retained" || mode.rfind("loading-",0)==0?2u:4u));
 else assert(memoryLive.empty());
#endif
 printf("Resident shell: %s PASS\n",mode.c_str());
}
