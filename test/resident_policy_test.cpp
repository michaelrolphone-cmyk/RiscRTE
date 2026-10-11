#include "ports/esp32s3/CpuPort.h"
#include <RiscResidentShellV1.h>
#include <RiscStreamClientV1.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static_assert(RISC_RESIDENT_POLL_ACTIVITY==1 && RISC_RESIDENT_POLL_INHIBIT_IDLE==2 &&
              RISC_RESIDENT_POLL_INHIBIT_POLICY==4,"Append-only POLL hints");
static_assert(RISC_RESIDENT_CHECKPOINT_POLICY==7 && RISC_RESIDENT_REPLY_POLICY_REQUEST==4,
              "Append-only policy handshake");
static_assert(sizeof(risc_resident_request_v1)==16 && sizeof(risc_resident_reply_v1)==8,
              "Existing request and reply layouts remain unchanged");
static_assert(RISC_RUNTIME_DEFAULT_REQUEST_V1_SIZE==offsetof(risc_runtime_api_v1,resident_shell),
              "Existing Runtime prefix remains unchanged");
struct NativeProbe {uint32_t version,size;int32_t (*deepSleep)(unsigned);};
namespace {
using RiscBoot::Runtime;
std::string root,mode;
Runtime* runtime=nullptr;
Runtime* volatile retainedRuntime=nullptr;
RiscCpu::Port* cpu=nullptr;
const risc_runtime_api_v1* api=nullptr;
bool owned=true,exitSafe=true,finished=false,lifecycleCheckpoint=false,streamClosing=false;
unsigned starts[3]{},finis[3]{},maps[3]{},unmaps[3]{},entries[3]{};
unsigned roleNow=0,dispatches=0,hardwareCalls=0,arms=0,clears=0,graphBusyChecks=0;
std::vector<std::string> events;
risc_resident_client_v1 clients[3]{},oldChild{};
risc_key_value_v1 keys[3]{};
const NativeProbe* probes[3]{};
risc_runtime_capability_v1 keyGrants[3]{},nativeGrants[3]{};
risc_stream_client_v1 streams{};
risc_stream_opened_v1 opened{};
int32_t callbackStatus=RISC_RESIDENT_OK;
uint32_t callbackFlags=0;
bool callbackExit=false,mutateAlias=false;
risc_resident_request_v1* originalRequest=nullptr;
risc_resident_reply_v1* originalReply=nullptr;
risc_resident_request_v1 expectedRequest{};
risc_resident_reply_v1 beforeReply{};
bool is(const char* name){return mode==name;}
bool terminal(){return mode.rfind("malformed-",0)==0 || mode.rfind("retained-",0)==0;}
bool owner(){return owned;}
bool health(risc_runtime_health_v1*){return true;}
void delay(uint32_t){}
bool log(const char*){return true;}
bool bind(Runtime& r){return cpu->bind(r);}
bool safe(){return exitSafe && cpu->appExitSafe();}
int32_t get(void*,uint32_t ns,const char*,void* out,uint32_t cap,uint32_t* size){
  assert(cap>=4);std::memcpy(out,&ns,4);*size=4;return RISC_KEY_VALUE_OK;
}
int32_t put(void*,uint32_t,const char*,const void*,uint32_t){return RISC_KEY_VALUE_OK;}
RiscBoot::KeyValueBackend keyBackend{nullptr,get,put};
uint32_t read(const risc_key_value_v1& key){
  uint32_t value=0,n=0;assert(key.get(key.context,"key",&value,4,&n)==RISC_KEY_VALUE_OK && n==4);return value;
}
void denied(const risc_key_value_v1& key){
  uint32_t value=0,n=0;assert(key.get(key.context,"key",&value,4,&n)==RISC_KEY_VALUE_CONTEXT);
}
risc_runtime_capability_v1 acquire(const char* cap,uint64_t instance=0){
  risc_runtime_capability_v1 grant{};grant.struct_size=sizeof(grant);
  assert(api->acquire(cap,1,instance,&grant));return grant;
}
void nativeBlocked(unsigned role){
  assert(!runtime->residentResetSafe() && !cpu->restartResourcesSafe());
  const unsigned before=hardwareCalls;
  for(unsigned form=0;form<4;++form)assert(probes[role]->deepSleep(form)==RISC_DEEP_SLEEP_BUSY);
  assert(hardwareCalls==before);
}
void nativeAllowed(){
  assert(runtime->residentResetSafe() && cpu->restartResourcesSafe());
  for(unsigned form=0;form<4;++form){
    const auto beforeArms=arms,beforeClears=clears;
    assert(probes[0]->deepSleep(form)==RISC_DEEP_SLEEP_PLATFORM);
    assert(arms==beforeArms+1 && clears==beforeClears+1 && cpu->appExitSafe());
  }
}
RiscCpu::Hardware hardware(){
  RiscCpu::Hardware h{};h.owner=owner;h.now=[]()->uint64_t{return 100;};h.sleep=delay;
  h.gpioOpen=[](uint8_t pin,bool output,bool,bool pullup){assert(pin==7 && !output && pullup);++hardwareCalls;return true;};
  h.gpioWrite=[](uint8_t,bool){assert(false);return false;};
  h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){assert(false);return false;};
  h.gpioRead=[](uint8_t pin,bool* value){assert(pin==7);++hardwareCalls;*value=true;return true;};
  h.gpioClose=[](uint8_t pin){assert(pin==7);++hardwareCalls;return true;};
  h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){assert(false);return false;};
  h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){assert(false);return false;};
  h.i2cClose=[](uint8_t){assert(false);return false;};
  h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){assert(false);return false;};
  h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){assert(false);return false;};
  h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){assert(false);return false;};
  h.spiEnd=[](uint8_t,uint8_t,uint32_t){assert(false);return false;};
  h.spiClose=[](uint8_t){assert(false);return false;};
  h.deepWakeValid=[](uint8_t pin){assert(pin==7);++hardwareCalls;return true;};
  h.deepReady=[](){++hardwareCalls;return true;};
  h.deepWakeArm=[](uint8_t pin,bool high,bool pullup){assert(pin==7 && !high && pullup);++hardwareCalls;++arms;return false;};
  h.deepWakeClear=[](uint8_t pin,bool pullup){assert(pin==7 && pullup);++hardwareCalls;++clears;return true;};
  h.deepSleep=[](){assert(false);};
  h.timerArm=[](uint32_t){assert(false);return false;};
  h.timerClear=[](){++hardwareCalls;return true;};
  h.deepWakeSetValid=[](uint64_t mask,uint64_t high){assert(mask==(uint64_t(1)<<7) && !high);++hardwareCalls;return true;};
  h.deepWakeSetArm=[](uint64_t mask,uint64_t high,uint64_t pulls){assert(mask==(uint64_t(1)<<7) && !high && pulls==mask);++hardwareCalls;++arms;return false;};
  h.deepWakeSetClear=[](uint64_t mask,uint64_t high,uint64_t pulls){assert(mask==(uint64_t(1)<<7) && !high && pulls==mask);++hardwareCalls;++clears;return true;};
  return h;
}
void save(const std::string& name,const JsonDocument& doc){std::string text;serializeJson(doc,text);std::ofstream(root+"/"+name)<<text;}
void setup(){
  std::ofstream(root+"/board.json")<<R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[{"instance_id":7,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[7],"active_high":true,"pull_up":true,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})";
  std::ofstream(root+"/probe.json")<<R"({"type":"driver","id":"resident-native-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"probe.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.gpio","api":1}],"provides":[{"capability":"test.resident-native","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})";
  std::ofstream(root+"/stream.json")<<R"({"type":"driver","id":"stream","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"stream_session_provider.elf","requires":[{"capability":"test.root","api":1}],"provides":[{"capability":"test.stream","api":1}]})";
  std::ofstream(root+"/root.json")<<R"({"type":"driver","id":"stream-root","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"stream_session_root.elf","requires":[],"provides":[{"capability":"test.root","api":1}]})";
  JsonDocument boot;boot["board"]="board.json";boot["default_app"]="host.elf";boot["provider_activation"]="demand";
  auto drivers=boot["drivers"].to<JsonArray>();auto probe=drivers.add<JsonObject>();probe["manifest"]="probe.json";probe["instance_id"]=7;
  for(const char* name:{"stream","root"})drivers.add<JsonObject>()["manifest"]=std::string(name)+".json";
  auto resident=boot["resident_shell"].to<JsonObject>();resident["api"]=1;resident["host"]="host.elf";
  auto foreground=resident["foreground"].to<JsonArray>();foreground.add("child.elf");foreground.add("next.elf");
  auto policies=boot["app_capabilities"].to<JsonArray>();
  const char* names[]={"host","child","next"};
  for(unsigned role=0;role<3;++role){
    JsonDocument app;app["type"]="application";app["id"]=names[role];app["version"]="1.0.0";app["architecture"]="xtensa-esp32s3";app["file_name"]=std::string(names[role])+".elf";app["entry"]="app_main";
    auto requirements=app["requires"].to<JsonArray>();auto policy=policies.add<JsonObject>();policy["manifest"]=std::string(names[role])+".json";auto grants=policy["grants"].to<JsonArray>();
    auto add=[&](const char* cap,uint32_t instance){auto req=requirements.add<JsonObject>();req["capability"]=cap;req["api"]=1;auto grant=grants.add<JsonObject>();grant["capability"]=cap;grant["api"]=1;grant["instance_id"]=instance;};
    add("storage.key-value",11+role);add("test.resident-native",7);add("test.stream",0);
    save(std::string(names[role])+".json",app);
  }
  save("boot.json",boot);
}
int32_t invoke(risc_resident_request_v1& request,risc_resident_reply_v1& reply){
  originalRequest=&request;originalReply=&reply;expectedRequest=request;beforeReply=reply;
  const auto status=clients[roleNow].checkpoint(clients[roleNow].invocation,&request,&reply);
  originalRequest=nullptr;originalReply=nullptr;
  if(status!=RISC_RESIDENT_RETAINED){assert(read(keys[roleNow])==11+roleNow);denied(keys[0]);}
  return status;
}
void checkpoint(uint32_t reason,uint32_t flags,int32_t expected,uint32_t expectedFlags=0){
  risc_resident_request_v1 request{sizeof(request),reason,flags,0};
  risc_resident_reply_v1 reply{sizeof(reply),0xa5a5a5a5};
  const auto before=dispatches;
  assert(invoke(request,reply)==expected);
  if(expected==RISC_RESIDENT_INVALID || expected==RISC_RESIDENT_RETAINED ||
     (expected==RISC_RESIDENT_EXIT && before==dispatches)){
    assert(reply.struct_size==sizeof(reply) && reply.flags==0xa5a5a5a5);
  }else assert(reply.struct_size==sizeof(reply) && reply.flags==expectedFlags);
  if(expected==RISC_RESIDENT_INVALID)assert(dispatches==before);
}
void policyRequest(uint32_t flags=0){
  callbackStatus=RISC_RESIDENT_OK;callbackFlags=RISC_RESIDENT_REPLY_POLICY_REQUEST;
  checkpoint(RISC_RESIDENT_CHECKPOINT_POLL,flags,RISC_RESIDENT_OK,callbackFlags);callbackFlags=0;
}
void consumePolicy(){checkpoint(RISC_RESIDENT_CHECKPOINT_POLICY,0,RISC_RESIDENT_OK);checkpoint(RISC_RESIDENT_CHECKPOINT_POLICY,0,RISC_RESIDENT_INVALID);}
void invalidRequests(){
  for(uint32_t reason:{0u,2u,3u,4u,5u,6u,7u,8u,UINT32_MAX})for(uint32_t flags=1;flags<8;++flags)checkpoint(reason,flags,RISC_RESIDENT_INVALID);
  for(uint32_t flags:{8u,9u,15u,0x80000000u,UINT32_MAX})checkpoint(RISC_RESIDENT_CHECKPOINT_POLL,flags,RISC_RESIDENT_INVALID);
  checkpoint(0,0,RISC_RESIDENT_INVALID);
  for(unsigned which=0;which<3;++which){
    risc_resident_request_v1 request{sizeof(request),RISC_RESIDENT_CHECKPOINT_POLL,0,0};risc_resident_reply_v1 reply{sizeof(reply),0x1234};
    if(which==0)--request.struct_size;
    if(which==1)request.reserved=1;
    if(which==2)--reply.struct_size;
    const auto before=dispatches;const auto old=reply;
    assert(invoke(request,reply)==RISC_RESIDENT_INVALID && dispatches==before && !std::memcmp(&reply,&old,sizeof(reply)));
  }
}
void handshake(){
  policyRequest();
  // Ordinary POLL does not consume a pending policy, including idle inhibition.
  checkpoint(RISC_RESIDENT_CHECKPOINT_POLL,0,RISC_RESIDENT_OK);
  checkpoint(RISC_RESIDENT_CHECKPOINT_POLL,RISC_RESIDENT_POLL_INHIBIT_IDLE,RISC_RESIDENT_OK);
  callbackStatus=RISC_RESIDENT_BUSY;
  checkpoint(RISC_RESIDENT_CHECKPOINT_POLICY,0,RISC_RESIDENT_BUSY);
  checkpoint(RISC_RESIDENT_CHECKPOINT_POLL,RISC_RESIDENT_POLL_INHIBIT_POLICY|RISC_RESIDENT_POLL_ACTIVITY,RISC_RESIDENT_BUSY);
  callbackStatus=RISC_RESIDENT_OK;consumePolicy();
  policyRequest(RISC_RESIDENT_POLL_INHIBIT_IDLE);
  checkpoint(RISC_RESIDENT_CHECKPOINT_POLL,RISC_RESIDENT_POLL_INHIBIT_POLICY,RISC_RESIDENT_OK);
  checkpoint(RISC_RESIDENT_CHECKPOINT_POLICY,0,RISC_RESIDENT_INVALID);
  policyRequest(RISC_RESIDENT_POLL_ACTIVITY|RISC_RESIDENT_POLL_INHIBIT_IDLE);consumePolicy();
  policyRequest();invalidRequests();consumePolicy();
}
void aliases(){
  risc_resident_request_v1 request{sizeof(request),RISC_RESIDENT_CHECKPOINT_POLL,RISC_RESIDENT_POLL_ACTIVITY,0};
  // The public byte buffers may overlap. memcpy avoids C++ type-punning reads.
  auto* reply=reinterpret_cast<risc_resident_reply_v1*>(&request);
  originalRequest=&request;originalReply=reply;expectedRequest=request;std::memcpy(&beforeReply,&request,sizeof(beforeReply));
  mutateAlias=true;callbackFlags=RISC_RESIDENT_REPLY_POLICY_REQUEST;
  assert(clients[roleNow].checkpoint(clients[roleNow].invocation,&request,reply)==RISC_RESIDENT_OK);
  risc_resident_reply_v1 copied{};std::memcpy(&copied,&request,sizeof(copied));
  assert(copied.struct_size==sizeof(copied) && copied.flags==callbackFlags);
  originalRequest=nullptr;originalReply=nullptr;mutateAlias=false;callbackFlags=0;
  assert(read(keys[roleNow])==11+roleNow);denied(keys[0]);consumePolicy();
}
void childScenario(){
  checkpoint(RISC_RESIDENT_CHECKPOINT_POLICY,0,RISC_RESIDENT_INVALID);
  if(starts[roleNow]>1 || roleNow==2){handshake();return;}
  if(is("flags")){
    for(uint32_t flags=0;flags<8;++flags)checkpoint(RISC_RESIDENT_CHECKPOINT_POLL,flags,RISC_RESIDENT_OK);
    for(uint32_t flags:{0u,1u,2u,3u}){policyRequest(flags);consumePolicy();}
  }else if(is("invalid"))invalidRequests();
  else if(is("handshake"))handshake();
  else if(is("aliases"))aliases();
  else if(is("old-semantics")){
    callbackFlags=RISC_RESIDENT_REPLY_REDRAW|RISC_RESIDENT_REPLY_CONFIGURATION_CHANGED;
    for(uint32_t reason:{1u,2u,3u,4u,5u,6u,8u,UINT32_MAX})checkpoint(reason,0,RISC_RESIDENT_OK,callbackFlags);
    callbackFlags=0;callbackStatus=RISC_RESIDENT_BUSY;
    for(uint32_t reason:{1u,2u,3u,4u,5u,6u,8u,UINT32_MAX})checkpoint(reason,0,RISC_RESIDENT_BUSY);
    callbackStatus=RISC_RESIDENT_OK;
  }else if(is("graph-busy")){
    policyRequest();lifecycleCheckpoint=true;auto grant=acquire("test.stream");assert(api->release(&grant));lifecycleCheckpoint=false;
    assert(graphBusyChecks>=2);consumePolicy();
  }else if(is("cleanup-isolation")){policyRequest();}
  else if(is("chain-isolation")){policyRequest();assert(api->request_launch("next.elf"));}
  else if(is("exit") || is("home")){
    policyRequest();
    if(is("exit")){callbackExit=true;checkpoint(RISC_RESIDENT_CHECKPOINT_POLL,0,RISC_RESIDENT_EXIT);callbackExit=false;}
    else assert(api->request_default());
    checkpoint(RISC_RESIDENT_CHECKPOINT_POLICY,0,RISC_RESIDENT_EXIT);
  }else if(is("deep-gate")){nativeBlocked(roleNow);policyRequest();consumePolicy();nativeBlocked(roleNow);}
  else if(is("retained-stream")){
    policyRequest();auto grant=acquire("test.stream");(void)grant;
    streams.struct_size=sizeof(streams);assert(api->stream_client(&streams));opened.struct_size=sizeof(opened);uint32_t request=42;
    assert(streams.open(streams.context,&grant,&request,sizeof(request),100,&opened)==RISC_STREAM_OK);streamClosing=true;
  }else {
    uint32_t reason=RISC_RESIDENT_CHECKPOINT_POLL,flags=0;
    if(is("malformed-policy")){policyRequest();reason=RISC_RESIDENT_CHECKPOINT_POLICY;}
    if(is("malformed-controls"))reason=RISC_RESIDENT_CHECKPOINT_CONTROLS;
    if(is("malformed-inhibited"))flags=RISC_RESIDENT_POLL_INHIBIT_POLICY;
    callbackFlags=RISC_RESIDENT_REPLY_POLICY_REQUEST;
    if(is("malformed-busy"))callbackStatus=RISC_RESIDENT_BUSY;
    if(is("malformed-exit"))callbackStatus=RISC_RESIDENT_EXIT;
    if(is("malformed-failed"))callbackStatus=RISC_RESIDENT_FAILED;
    if(is("malformed-exit-request"))callbackExit=true;
    if(is("malformed-bits"))callbackFlags=8;
    if(is("malformed-busy-redraw")){callbackStatus=RISC_RESIDENT_BUSY;callbackFlags=RISC_RESIDENT_REPLY_REDRAW;}
    if(is("retained-unsafe-poll")){exitSafe=false;flags=RISC_RESIDENT_POLL_INHIBIT_POLICY;}
    const unsigned before=dispatches;checkpoint(reason,flags,RISC_RESIDENT_RETAINED);
    if(is("retained-unsafe-poll"))assert(dispatches==before);
    assert(runtime->retained() && !runtime->residentResetSafe());denied(keys[0]);denied(keys[roleNow]);
    risc_resident_request_v1 request{sizeof(request),RISC_RESIDENT_CHECKPOINT_POLICY,0,0};risc_resident_reply_v1 reply{sizeof(reply),0};
    const auto retainedDispatches=dispatches;
    assert(clients[roleNow].checkpoint(clients[roleNow].invocation,&request,&reply)==RISC_RESIDENT_DENIED);
    assert(dispatches==retainedDispatches);return;
  }
}
}
extern "C" const char* stream_test_mode(){return streamClosing?"close-fail":lifecycleCheckpoint?"lifecycle-reentry":"normal";}
extern "C" void stream_test_event(const char* event){events.emplace_back(event);if(!std::strcmp(event,"provider:close"))assert(!runtime->residentResetSafe());}
extern "C" void stream_test_slow(){assert(false);}
extern "C" void stream_test_provider(const void*){}
extern "C" void stream_test_root(const void*){}
extern "C" void stream_test_grant_failure(unsigned){assert(false);}
extern "C" void stream_test_lock(){assert(false);}
extern "C" void stream_test_unlock(){assert(false);}
extern "C" void stream_test_reenter(bool){
  assert(lifecycleCheckpoint);const auto before=dispatches;
  risc_resident_request_v1 request{sizeof(request),RISC_RESIDENT_CHECKPOINT_POLL,RISC_RESIDENT_POLL_INHIBIT_POLICY,0};
  risc_resident_reply_v1 reply{sizeof(reply),0x1234};
  assert(clients[roleNow].checkpoint(clients[roleNow].invocation,&request,&reply)==RISC_RESIDENT_BUSY);
  assert(dispatches==before && reply.flags==0x1234);++graphBusyChecks;
}
extern "C" void test_policy_map(unsigned role){++maps[role];events.push_back(std::to_string(role)+":map");}
extern "C" void test_policy_unmap(unsigned role){
  if(finished)return;
  if(role)assert(!runtime->residentResetSafe());
  ++unmaps[role];events.push_back(std::to_string(role)+":unmap");
}
extern "C" int test_policy_init(unsigned role){
  ++starts[role];roleNow=role;api=risc_runtime_get_api(1);assert(api);
  risc_resident_client_v1 no{};no.struct_size=sizeof(no);assert(!api->resident_shell(&no));return 0;
}
extern "C" void test_policy_fini(unsigned role){
  ++finis[role];events.push_back(std::to_string(role)+":fini");
  if(role)nativeBlocked(role);
}
extern "C" int32_t test_policy_dispatch(void* context,const risc_resident_request_v1* request,risc_resident_reply_v1* reply){
  assert(*static_cast<unsigned*>(context)==1);++dispatches;
  assert(originalRequest && originalReply && request!=originalRequest && reply!=originalReply);
  assert(!std::memcmp(request,&expectedRequest,sizeof(*request)));
  assert(!std::memcmp(originalReply,&beforeReply,sizeof(beforeReply)));
  assert(read(keys[0])==11);denied(keys[roleNow]);nativeBlocked(0);
  risc_resident_reply_v1 nested{sizeof(nested),0};
  assert(clients[roleNow].checkpoint(clients[roleNow].invocation,request,&nested)==RISC_RESIDENT_DENIED);
  if(mutateAlias){std::memset(originalRequest,0xff,sizeof(*originalRequest));assert(!std::memcmp(request,&expectedRequest,sizeof(*request)));}
  if(callbackExit)assert(clients[0].request_foreground_exit(clients[0].invocation)==RISC_RESIDENT_OK);
  reply->flags=callbackFlags;
  if(is("malformed-size"))--reply->struct_size;
  if(is("retained-callback")){assert(api->retain_invocation());return RISC_RESIDENT_RETAINED;}
  if(is("retained-restoration"))exitSafe=false;
  return callbackStatus;
}
extern "C" void test_policy_main(unsigned role,unsigned* visits,const risc_resident_callbacks_v1* callbacks){
  assert(++*visits==1);++entries[role];roleNow=role;
  auto& client=clients[role];client.struct_size=sizeof(client);assert(api->resident_shell(&client));
  keyGrants[role]=acquire("storage.key-value");keys[role]=*static_cast<const risc_key_value_v1*>(keyGrants[role].api);
  nativeGrants[role]=acquire("test.resident-native",7);probes[role]=static_cast<const NativeProbe*>(nativeGrants[role].api);
  assert(read(keys[role])==11+role);
  if(!role){
    assert(client.register_shell(client.invocation,callbacks)==RISC_RESIDENT_OK);nativeAllowed();
    const unsigned runs=is("cleanup-isolation") || is("exit") || is("home")?2:1;
    for(unsigned run=0;run<runs;++run){
      risc_resident_result_v1 result{};result.struct_size=sizeof(result);
      const auto status=client.run_foreground(client.invocation,"child.elf",&result);roleNow=0;
      assert(status==(terminal()?RISC_RESIDENT_RETAINED:RISC_RESIDENT_OK) && result.status==status);
      if(terminal()){
        assert(!runtime->residentResetSafe());denied(keys[0]);denied(keys[1]);
        risc_resident_failure_v1 failure{};failure.struct_size=sizeof(failure);
        assert(client.last_failure(client.invocation,&failure) && failure.status==RISC_RESIDENT_RETAINED);
        assert(finis[0]==0 && unmaps[0]==0 && unmaps[1]==0);return;
      }
      assert(finis[1]==run+1 && unmaps[1]==run+1);denied(keys[1]);assert(read(keys[0])==11);nativeAllowed();
    }
  }else{
    if(oldChild.invocation){
      risc_resident_request_v1 request{sizeof(request),RISC_RESIDENT_CHECKPOINT_POLICY,0,0};risc_resident_reply_v1 reply{sizeof(reply),0};
      assert(oldChild.invocation!=client.invocation && oldChild.checkpoint(oldChild.invocation,&request,&reply)==RISC_RESIDENT_DENIED);
    }
    denied(keys[0]);nativeBlocked(role);childScenario();oldChild=client;
  }
}
int main(int argc,char** argv){
  assert(argc==3);root=argv[1];mode=argv[2];setup();RiscCpu::Port cpuPort(hardware());cpu=&cpuPort;
  runtime=new Runtime({owner,health,delay,log,bind,&keyBackend,safe});
  if(!runtime->prepare(root.c_str())){std::fprintf(stderr,"Policy fixture admission failed: %s\n",runtime->error());return 1;}
  assert(!hardwareCalls);
  const bool success=runtime->run();if(success==terminal())std::fprintf(stderr,"Unexpected runtime result %s: %s\n",mode.c_str(),runtime->error());
  assert(success!=terminal() && runtime->retained()==terminal());
  assert(starts[0]==1 && entries[0]==1 && finis[0]==(terminal()?0u:1u));
  if(terminal()){retainedRuntime=runtime;assert(!unmaps[0] && !unmaps[1]);}
  else{assert(maps[0]==unmaps[0] && maps[1]==unmaps[1] && maps[2]==unmaps[2]);assert(cpu->quiescent());delete runtime;}
  finished=true;std::printf("Resident .89 Runtime/Graph/ELF policy: %s PASS\n",mode.c_str());
}
