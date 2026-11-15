// Actual Runtime/production DeviceProviderExecutor/Graph/Module/resident worker.
// Explicit hosted boundaries: native ELF relocation spans and pthread RTOS.
// Host app uses normal dlopen. This is not target or physical retirement proof.
#include "bootstrap/Runtime.h"
#include "RuntimeFileFixture.h"
#include "runtime/drivers/DeviceProviderExecutorV2.h"
#include "runtime/drivers/ProviderGraphV2.h"
#include <fstream>
#include <filesystem>
#include <sys/wait.h>
#include <unistd.h>
#include <type_traits>
#include "runtime/drivers/ProviderOwnedSpecV2.h"
#include "runtime/streams/ProviderQueueHost.h"
#include "private/esp_privileged_os_cpu.h"
#include "private/elf_symbol.h"
#include "esp_elf.h"
#include <RiscCpuTaskScopeV1.h>
#include <RiscNativeRootV1.h>
#include <RiscNativeTerminalV1.h>
#include <atomic>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
extern "C" bool integration_is_creator();
extern "C" void integration_pump_tail();
extern "C" unsigned integration_tail_count();
extern "C" unsigned integration_create_count();
extern "C" bool integration_tasks_done();
extern "C" void integration_finish_tasks();
using namespace RuntimeProviders;

namespace RiscBoot {
class RuntimeNativeCallerTestPeer {
public:
 static size_t index(Runtime& r){for(size_t i=0;i<r.driverCount_;++i)if(!std::strcmp(r.drivers_[i].id,"test-terminal-owner"))return i;assert(false);return 0;}
 static auto& owner(Runtime& r){return r.nativeOwners_[index(r)];}
 static auto& grant(Runtime& r){return r.grants_[index(r)];}
 static auto ordinaryGrant(Runtime& r){return r.grants_[0];}
 static auto& graph(Runtime& r){return r.graph_;}
 static bool close(Runtime& r){return r.closeBootProvider(index(r));}
 static bool stage(Runtime& r){
  const auto i=index(r);const auto& d=r.drivers_[i];
  return RuntimePackages::DeviceProviderExecutorV2::stageNativeOwner(
   r.graph_,d.id,d.provides,d.api,d.instance,r.grants_[i],r.nativeOwners_[i]);
 }
 static bool metadataHeld(Runtime& r){
  return r.nativePolicies_ && r.port_.nativeProviders.count==1 &&
   !std::strcmp(r.port_.nativeProviders.entries[0].driverId,"test-terminal-owner") &&
   !std::strcmp(r.drivers_[index(r)].id,"test-terminal-owner");
 }
};
}
using Executor=RuntimePackages::DeviceProviderExecutorV2;
using Peer=RiscBoot::RuntimeNativeCallerTestPeer;
static_assert(!std::is_copy_constructible<Executor::NativeOwner>::value);
static_assert(!std::is_copy_assignable<Executor::NativeOwner>::value);
static_assert(!std::is_move_constructible<Executor::NativeOwner>::value);
static_assert(!std::is_move_assignable<Executor::NativeOwner>::value);


namespace {
std::string scenario;
bool stagedBoundaryCase(){return scenario=="nonbusy-staged-identity"||scenario=="staged-cancel-immediate"||scenario=="staged-cancel-failure";}
bool abi2Case(){return scenario=="abi2-root"||scenario=="abi2-extended-root";}
RiscBoot::Runtime* activeRuntime=nullptr;
bool injected=false;
uintptr_t rawBegin=0x10000,codeBegin=0;size_t textBytes=0;
unsigned starts=0,quiesces=0,stops=0,unmaps=0,polls=0,terminalCalls=0,apps=0;
unsigned depStarts=0,depQuiesces=0,depStops=0,revokeAttempts=0;
const risc_provider_dependency_v1* heldDependencies=nullptr;
std::string depPath;
bool identityChecked=false,stageBoundaryChecked=false;
void nonbusyStageBoundary();
unsigned networkCalls=0,cancelCalls=0;
GraphV2* foreignGraph=nullptr;GrantV2 foreignGrant{};
std::atomic<unsigned> entries{0};
std::atomic<bool> closeSignal{false};
uint64_t mapping=0,worker=0;
int anchor=42,capability=0;

extern risc_driver_native_terminal_v1 descriptor;
template<class P> void until(P p){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!p()){assert(std::chrono::steady_clock::now()<end);std::this_thread::yield();}}
void owner(void* p){
 assert(p==&anchor&&!integration_is_creator());uint64_t scope=0;
 assert(risc_cpu_task_scope_current_v1(&scope)==RISC_CPU_SCOPE_OK);++entries;
 if(scenario!="legacy-completed")until([]{return closeSignal.load();});
 assert(risc_cpu_task_scope_request_close_v1(scope)==RISC_CPU_SCOPE_OK);
}
bool bind(const risc_stream_provider_v1* h){assert(h&&h->context);mapping=h->context;return true;}
void poll(uint32_t){++polls;}

bool start(const risc_provider_dependency_v1* deps,size_t n){
 assert(n==1&&deps&&!std::strcmp(deps[0].capability_id,"test.dep")&&deps[0].api&&mapping&&integration_is_creator());heldDependencies=deps;++starts;
 if(abi2Case())return true;
 risc_cpu_task_config_v1 c{};c.struct_size=sizeof(c);c.entry=owner;c.argument=&anchor;c.argument_bytes=sizeof(anchor);
 c.stack_bytes=4096;c.working_stack_bytes=1024;c.priority=3;c.core=-1;std::strcpy(c.name,"net-owner-v5");
 assert(risc_cpu_worker_start_v5(&c,&worker)==RISC_CPU_SCOPE_OK&&worker);return scenario!="stage-failure"&&scenario!="destructor-grantless-retained";
}
bool quiesce(){
 ++quiesces;
 if(worker){assert(risc_cpu_worker_join_v5(worker)==RISC_CPU_SCOPE_OK);assert(risc_cpu_worker_release_v5(worker)==RISC_CPU_SCOPE_OK);worker=0;}
 return scenario!="close-post-revoke";
}
void stop(){++stops;assert(!worker);}
int32_t health(uint64_t,risc_native_root_health_v1*){assert(false);return RISC_NATIVE_ROOT_INVALID;}
int32_t call(uint64_t,uint64_t,uint32_t,uint32_t,const void*,uint32_t,uint32_t,void*,uint32_t,uint32_t*){assert(false);return RISC_NATIVE_ROOT_INVALID;}
risc_native_root_v1 rootApi{1,sizeof(rootApi),call,health};
risc_native_terminal_result_v1 terminalClose(uint64_t exact,uint32_t budget){
 assert(exact==mapping&&budget>0&&budget<=1000&&integration_is_creator());++terminalCalls;
 if(scenario=="terminal-pending"||scenario=="pending-safe-tail")return RISC_NATIVE_TERMINAL_PENDING_V1;
 if(scenario=="terminal-unknown-result")return static_cast<risc_native_terminal_result_v1>(1234);
 if(scenario=="terminal-refused")return RISC_NATIVE_TERMINAL_REFUSED_V1;
 if(scenario=="terminal-retained")return RISC_NATIVE_TERMINAL_RETAINED_V1;
 if(scenario=="nested-close"){
  auto& o=Peer::owner(*activeRuntime);auto g=Peer::grant(*activeRuntime);
  assert(o.closeInProgress && o.state==Executor::NativeState::Closing);
  assert(!Peer::close(*activeRuntime));assert(o.state==Executor::NativeState::Closing);
  assert(Peer::grant(*activeRuntime).slot==g.slot&&!o.releaseAttempted);
 }
 closeSignal=true;until([]{return integration_tasks_done();});integration_pump_tail();integration_finish_tasks();
 return RISC_NATIVE_TERMINAL_ACKNOWLEDGED_V1;
}
risc_native_terminal_v1 terminalApi{1,sizeof(terminalApi),terminalClose};
risc_driver_native_terminal_v1 descriptor{
 { {{{2,sizeof(descriptor),"test-terminal-owner","platform.http-client",1,&capability,start,stop,quiesce},nullptr,bind},poll},
 RISC_DRIVER_NATIVE_ROOT_TAG_V1,RISC_DRIVER_NATIVE_ROOT_VERSION_V1,&rootApi},
 RISC_DRIVER_NATIVE_TERMINAL_TAG_V1,RISC_DRIVER_NATIVE_TERMINAL_VERSION_V1,&terminalApi};
const risc_driver_v2* get(uint32_t abi){assert(abi==2);return &descriptor.root.poll.streams.driver;}
esp_symtab_t symbols[]={{reinterpret_cast<void*>(get),const_cast<char*>("t5_driver_get")}};
struct Image {elf32_hdr_t h;elf32_shdr_t s[5];char names[80],imports[160];elf32_sym_t syms[3];uint32_t text;};
Image image() {
 Image f{};std::memcpy(f.h.ident,"\177ELF\1\1\1",7);f.h.machine=94;f.h.type=3;f.h.version=1;
 f.h.ehsize=sizeof(f.h);f.h.shentsize=sizeof(f.s[0]);f.h.shnum=5;f.h.shstrndx=1;f.h.shoff=offsetof(Image,s);
 std::memcpy(f.names,"\0.shstrtab\0.dynstr\0.dynsym\0.text\0",33);
 f.s[1].name=1;f.s[1].type=SHT_STRTAB;f.s[1].offset=offsetof(Image,names);f.s[1].size=sizeof(f.names);
 f.s[2].name=11;f.s[2].type=SHT_STRTAB;f.s[2].offset=offsetof(Image,imports);f.s[2].size=sizeof(f.imports);
 f.s[3].name=19;f.s[3].type=SHT_SYNSYM;f.s[3].offset=offsetof(Image,syms);f.s[3].size=sizeof(f.syms);f.s[3].link=2;
 f.s[4].name=27;f.s[4].type=SHT_PROGBITS;f.s[4].flags=SHF_ALLOC|SHF_EXECINSTR;f.s[4].addr=0x1000;f.s[4].offset=offsetof(Image,text);f.s[4].size=4;
 std::strcpy(f.imports+1,abi2Case()?"memcpy":"risc_cpu_worker_start_v5");f.syms[1].name=1;f.syms[1].info=STB_GLOBAL<<4;
 unsigned at=unsigned(std::strlen(f.imports+1))+2;std::strcpy(f.imports+at,"t5_driver_get");
 f.syms[2].name=at;f.syms[2].value=0x1000;f.syms[2].size=4;f.syms[2].info=(STB_GLOBAL<<4)|STT_FUNC;f.syms[2].shndx=4;return f;
}

bool runtimeOwner(){return integration_is_creator();}
bool runtimeHealth(risc_runtime_health_v1* h){h->uptime_ms=1;return true;}
void runtimeDelay(uint32_t){if(starts && scenario=="legacy-completed"){until([]{return integration_tasks_done();});integration_pump_tail();integration_finish_tasks();}}
bool runtimeLog(const char* s){
 std::fprintf(stderr,"RUNTIME %s\\n",s);
 if(stagedBoundaryCase()&&!stageBoundaryChecked&&
    std::strstr(s,"id=test-terminal-owner phase=start")){
  stageBoundaryChecked=true;nonbusyStageBoundary();
 }
 return true;
}
bool coldBoot(){return true;}
bool appExitSafe(){return scenario!="app-exit-refusal";}
bool providerStorageSafe(){return true;}
bool boundarySafe(void*){
 if(injected)return false;
 if(activeRuntime){
  auto& o=Peer::owner(*activeRuntime);
  const bool map=(scenario=="map-failure"||scenario=="staged-grant-identity") && o.failure==Executor::NativeFailure::Mapping;
  const bool commit=scenario=="commit-failure" && o.failure==Executor::NativeFailure::Commit;
  if(o.state==Executor::NativeState::Staged && (map||commit)){
   if(scenario=="staged-grant-identity"){
    auto& g=Peer::graph(*activeRuntime);auto exact=Peer::grant(*activeRuntime),stale=exact; ++stale.generation;
    assert(!Executor::closeNativeOwner(g,stale,o,1000)&&o.state==Executor::NativeState::Staged&&g.holdsGrant(exact));
    auto other=Peer::ordinaryGrant(*activeRuntime);assert(other.slot&&other.slot!=exact.slot&&g.holdsGrant(other));
    assert(!Executor::closeNativeOwner(g,other,o,1000)&&o.state==Executor::NativeState::Staged&&g.holdsGrant(other)&&g.holdsGrant(exact));
    assert(foreignGraph&&foreignGraph->holdsGrant(foreignGrant));
    assert(!Executor::closeNativeOwner(*foreignGraph,foreignGrant,o,1000)&&o.state==Executor::NativeState::Staged&&foreignGraph->holdsGrant(foreignGrant)&&g.holdsGrant(exact));
    identityChecked=true;
   }
   injected=true;return false;
  }
 }
 return true;
}
bool bindBackends(RiscBoot::Runtime& r){
 static const RiscBoot::TcpListenerBackend backend{
  nullptr,
  [](void*,uint64_t,const risc_tcp_listen_v1*,uint64_t*)->int32_t{++networkCalls;assert(false);return -1;},
  [](void*,uint64_t,uint64_t,uint64_t*)->int32_t{++networkCalls;assert(false);return -1;},
  [](void*,uint64_t,uint64_t,void*,uint32_t,uint32_t*)->int32_t{++networkCalls;assert(false);return -1;},
  [](void*,uint64_t,uint64_t,const void*,uint32_t,uint32_t*)->int32_t{++networkCalls;assert(false);return -1;},
  [](void*,uint64_t,uint64_t)->int32_t{++networkCalls;assert(false);return -1;},
  [](void*,uint64_t)->bool{return true;},
  boundarySafe};
 return r.registerTcpListener(&backend);
}


void nonbusyStageBoundary(){
 assert(activeRuntime&&Peer::stage(*activeRuntime));
 auto& graph=Peer::graph(*activeRuntime);auto& record=Peer::owner(*activeRuntime);
 auto exact=Peer::grant(*activeRuntime),stale=exact;auto other=Peer::ordinaryGrant(*activeRuntime);
 const auto* policy=record.policy;const auto mapped=record.mapping;
 assert(!graph.lifecycleBusy()&&record.state==Executor::NativeState::Staged&&!record.committed);
 assert(exact.slot&&other.slot&&other.slot!=exact.slot&&graph.holdsGrant(exact)&&graph.holdsGrant(other));
 assert(record.graphOwner==&graph&&policy&&foreignGraph&&foreignGraph->holdsGrant(foreignGrant));
 ++stale.generation;
 assert(!Executor::closeNativeOwner(graph,stale,record,1000));
 assert(!Executor::closeNativeOwner(graph,other,record,1000));
 assert(!Executor::closeNativeOwner(*foreignGraph,foreignGrant,record,1000));
 assert(record.state==Executor::NativeState::Staged&&record.policy==policy&&record.mapping==mapped);
 assert(!record.releaseAttempted&&!record.closeInProgress&&graph.holdsGrant(exact)&&graph.holdsGrant(other));
 assert(foreignGraph->holdsGrant(foreignGrant)&&starts==1&&!entries&&!terminalCalls&&!quiesces&&!unmaps);
 const bool first=Peer::close(*activeRuntime);
 assert(cancelCalls==1);
 if(scenario=="staged-cancel-failure"){
  assert(!first&&record.state==Executor::NativeState::Retained&&!record.releaseAttempted&&graph.holdsGrant(exact));
  assert(!Peer::close(*activeRuntime)&&cancelCalls==1&&graph.holdsGrant(exact)&&!quiesces&&!unmaps&&!entries&&!terminalCalls);
  return;
 }
 if(scenario=="staged-cancel-immediate")assert(first);
 else {
  assert(!first&&record.state==Executor::NativeState::Closing&&!record.releaseAttempted&&graph.holdsGrant(exact));
  assert(!quiesces&&!unmaps&&!entries&&!terminalCalls);
  until([]{return integration_tasks_done();});integration_pump_tail();integration_finish_tasks();
  assert(Peer::close(*activeRuntime));
 }
 assert(cancelCalls==1);
 assert(record.state==Executor::NativeState::Empty&&!Peer::grant(*activeRuntime).slot);
 assert(!graph.holdsGrant(exact)&&!graph.interfaceFor(exact));
 assert(!graph.grantStream(exact,1,1,1));
 assert(graph.holdsGrant(other)&&foreignGraph->holdsGrant(foreignGrant));
 assert(!graph.lifecycleBusy()&&graph.activationSafe()&&!worker&&!entries&&!terminalCalls);
 assert(quiesces==1&&stops==1&&unmaps==1);
 integration_finish_tasks();
}

void write(const std::string& name,const std::string& bytes){std::ofstream f(name,std::ios::binary);f.write(bytes.data(),bytes.size());assert(f.good());}
}

extern "C" int fixture_actual_cancel_staged(esp_cpu_task_binding_v1);
extern "C" int esp_cpu_task_binding_cancel_staged_v1(esp_cpu_task_binding_v1 binding){
 ++cancelCalls;
 // Explicit modeled scheduler/error boundary. Actual resident cancellation,
 // ticket close and safe-tail checks remain production code.
 if(scenario=="staged-cancel-failure")esp_cpu_task_binding_retain_v1(binding);
 const int rc=fixture_actual_cancel_staged(binding);
 if(scenario=="staged-cancel-immediate"&&rc==RISC_CPU_SCOPE_PENDING){
  until([]{return integration_tasks_done();});integration_pump_tail();integration_finish_tasks();
  return risc_cpu_worker_join_v5(binding.token);
 }
 return rc;
}

extern "C" void integration_graph_tls(void*){assert(false);}
extern "C" int esp_elf_init(esp_elf_t* e){assert(e);std::memset(e,0,sizeof(*e));return 0;}
extern "C" int esp_elf_relocate(esp_elf_t* e,const uint8_t* b){
 assert(e&&b&&esp_elf_privileged_os_cpu_relocation_enter_v1(e));
 if(abi2Case()){assert(elf_find_sym_default("memcpy"));assert(!elf_find_sym_default("risc_cpu_worker_start_v5"));}
 else assert(elf_find_sym_default("risc_cpu_worker_start_v5")==reinterpret_cast<uintptr_t>(&risc_cpu_worker_start_v5));
 assert(!elf_find_sym_default("risc_cpu_worker_start_v2"));assert(esp_elf_privileged_os_cpu_relocation_leave_v1(e));
 e->sec[ELF_SEC_TEXT].addr=rawBegin;e->sec[ELF_SEC_TEXT].size=textBytes;
 const uintptr_t data[]={uintptr_t(&anchor),uintptr_t(&descriptor),uintptr_t(&rootApi),uintptr_t(&capability),uintptr_t(&terminalApi)};
 const auto lo=*std::min_element(data,data+5),hi=*std::max_element(data,data+4);
 e->sec[ELF_SEC_DATA].addr=lo;e->sec[ELF_SEC_DATA].size=hi-lo+sizeof(descriptor);
 e->num=1;e->symtab=symbols;return 0;
}
extern "C" uintptr_t elf_remap_text(esp_elf_t* e,uintptr_t p){assert(e&&(p==rawBegin||p==rawBegin+textBytes-1));return codeBegin+(p-rawBegin);}
extern "C" void esp_elf_deinit(esp_elf_t*){++unmaps;}

extern "C" void runtime_fixture_app(){++apps;}
extern "C" void runtime_fixture_dependency(unsigned event){
 if(event==1)++depStarts;else if(event==2)++depQuiesces;else if(event==3)++depStops;else assert(false);
}
namespace RuntimeStreams {
 const RuntimeProviders::StreamHostV1* fixture_actualStreamHost();
 const RuntimeProviders::StreamHostV1* runtimeProviderStreamHost(){
  static RuntimeProviders::StreamHostV1 copy=*fixture_actualStreamHost();
  copy.revokeGrantChecked=[](uint64_t context,uint64_t grant)->bool{
   if(context==mapping&&terminalCalls) {
    ++revokeAttempts;
    if(scenario=="close-pre-revoke")return false;
   }
   return fixture_actualStreamHost()->revokeGrantChecked(context,grant);
  };
  return &copy;
 }
}
int main(int argc,char** argv){
 assert(argc==5);scenario=argv[1];const std::string root=argv[2],app=argv[3];
 std::filesystem::create_directory(root);std::filesystem::copy_file(app,root+"/default.elf");
 depPath=root+"/dependency.elf";std::filesystem::copy_file(argv[4],depPath);
 const uintptr_t code[]={uintptr_t(&owner),uintptr_t(&quiesce),uintptr_t(&terminalClose),uintptr_t(&get)};
 codeBegin=*std::min_element(code,code+4);textBytes=*std::max_element(code,code+4)-codeBegin+1;
 auto bytes=image();const char* imports[]={abi2Case()?"memcpy":"risc_cpu_worker_start_v5"};
 NativeProviderPolicyV1 p{};p.relativeElfPath="private.elf";p.driverId="test-terminal-owner";p.version="1.0.0";
 p.capability="platform.http-client";p.api=1;p.osCpuAbi=abi2Case()?2:5;p.elfLength=sizeof(bytes);p.imports=imports;p.importCount=1;
 const NativeProviderRequirementV1 requirements[]={{"test.dep",1}};p.requirements=requirements;p.requirementCount=1;
 assert(nativeProviderDigest((const uint8_t*)&bytes,sizeof(bytes),p.sha256));
 assert(esp_elf_validate_file((const uint8_t*)&bytes,sizeof(bytes)));
 write(root+"/private.elf",std::string((const char*)&bytes,sizeof(bytes)));
 write(root+"/board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 write(root+"/provider.json",R"({"type":"driver","id":"test-terminal-owner","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"private.elf","requires":[{"capability":"test.dep","api":1}],"provides":[{"capability":"platform.http-client","api":1}]})");
 write(root+"/dependency.json",R"({"type":"driver","id":"fixture-dependency","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"dependency.elf","requires":[],"provides":[{"capability":"test.dep","api":1}]})");
 const bool cold=scenario=="cold";
 const bool deferred=scenario=="demand-deferred";
 write(root+"/boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"provider.json")")+
       (cold?R"(,"boot_start":"cold")":"")+"},{\"manifest\":\"dependency.json\"}]"+((cold||deferred)?R"(,"provider_activation":"demand")":"")+"}");
 if(scenario=="staged-grant-identity"||stagedBoundaryCase()){
  write(root+"/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"dependency.json"},{"manifest":"provider.json"}]})");
  foreignGraph=new GraphV2(RuntimeStreams::runtimeProviderStreamHost());
  SpecV2 ordinary{"fixture-dependency",depPath.c_str(),"test.dep",1,nullptr,0};assert(foreignGraph->addVerified(ordinary));
  foreignGrant=foreignGraph->acquireFrom("fixture-dependency","test.dep",1);assert(foreignGrant.slot);
 }
 if(scenario=="legacy-active"||scenario=="legacy-completed")descriptor.root.poll.streams.driver.struct_size=sizeof(risc_driver_native_root_v1);
 if(scenario=="abi2-root")descriptor.root.poll.streams.driver.struct_size=sizeof(risc_driver_native_root_v1);
 if(scenario=="abi2-extended-root"){descriptor.terminal_tag=0xdeadbeef;descriptor.terminal=reinterpret_cast<const risc_native_terminal_v1*>(uintptr_t(1));}

 if(scenario=="malformed-terminal"||scenario=="destructor-clean-refusal")descriptor.terminal_version=999;

 const bool malformedExtra=scenario=="terminal-prefix-truncated"||scenario=="terminal-suffix-truncated"||
  scenario=="terminal-suffix-oversized"||scenario=="terminal-unknown-tag"||
  scenario=="terminal-table-version"||scenario=="terminal-table-size"||
  scenario=="terminal-null-table"||scenario=="terminal-null-callback";
 if(scenario=="terminal-prefix-truncated"){
  descriptor.root.poll.streams.driver.struct_size=sizeof(risc_driver_native_root_v1)-1;
  descriptor.root.native_root=reinterpret_cast<const risc_native_root_v1*>(uintptr_t(1));
 }
 if(scenario=="terminal-suffix-truncated"){
  descriptor.root.poll.streams.driver.struct_size=sizeof(risc_driver_native_root_v1)+1;
  descriptor.terminal=reinterpret_cast<const risc_native_terminal_v1*>(uintptr_t(1));
 }
 if(scenario=="terminal-suffix-oversized"){
  descriptor.root.poll.streams.driver.struct_size=sizeof(descriptor)+1;
  descriptor.terminal=reinterpret_cast<const risc_native_terminal_v1*>(uintptr_t(1));
 }
 if(scenario=="terminal-unknown-tag")descriptor.terminal_tag=0xdeadbeef;
 if(scenario=="terminal-table-version")terminalApi.api_version=999;
 if(scenario=="terminal-table-size")terminalApi.struct_size=sizeof(terminalApi)-1;
 if(scenario=="terminal-null-table")descriptor.terminal=nullptr;
 if(scenario=="terminal-null-callback")terminalApi.prepare_close=nullptr;

 auto* files=new RiscTest::FileSources({root.c_str()});
 RiscBoot::Port port{runtimeOwner,runtimeHealth,runtimeDelay,runtimeLog};
 port.bindPlatforms=bindBackends;port.nativeProviders={&p,1};port.coldBoot=coldBoot;port.appExitSafe=appExitSafe;port.providerStorageSafe=providerStorageSafe;
 auto* runtime=new RiscBoot::Runtime(port);activeRuntime=runtime;assert(files->bind(*runtime));
 const bool prepared=runtime->prepare(root.c_str());
 std::fprintf(stderr,"PREPARE %d %s\n",int(prepared),runtime->error());assert(prepared);
 const bool ran=runtime->run();
 std::fprintf(stderr,"RUN %d retained=%d starts=%u entries=%u terminal=%u quiesce=%u stops=%u unmaps=%u apps=%u error=%s\n",
   int(ran),int(runtime->retained()),starts,entries.load(),terminalCalls,quiesces,stops,unmaps,apps,runtime->error());
 if(runtime->retained()){
  assert(Peer::metadataHeld(*runtime));
  if(starts){assert(heldDependencies&&!std::strcmp(heldDependencies[0].capability_id,"test.dep")&&heldDependencies[0].api);}
 }
 if(deferred){assert(ran&&!runtime->retained()&&!starts&&!entries&&!terminalCalls&&apps==1);delete runtime;delete files;}
 else if(scenario=="nonbusy-staged-identity"||scenario=="staged-cancel-immediate"){
  assert(stageBoundaryChecked&&ran&&!runtime->retained()&&starts==2&&entries==1&&terminalCalls==1&&quiesces==2&&stops==2&&unmaps==2&&apps==1);
  assert(Peer::owner(*runtime).state==Executor::NativeState::Empty&&!Peer::grant(*runtime).slot);
  assert(depStarts==2&&depStops==1&&foreignGraph->holdsGrant(foreignGrant));
  assert(foreignGraph->release(foreignGrant));delete foreignGraph;assert(depStops==2);
  delete runtime;delete files;
 } else if(scenario=="staged-cancel-failure"){
  assert(stageBoundaryChecked&&!ran&&runtime->retained()&&starts==1&&!entries&&!terminalCalls&&!quiesces&&!stops&&!unmaps&&!apps);
  assert(Peer::owner(*runtime).state==Executor::NativeState::Retained&&Peer::graph(*runtime).holdsGrant(Peer::grant(*runtime)));
  assert(cancelCalls==1&&depStarts==2&&depStops==0&&foreignGraph->holdsGrant(foreignGrant));
  assert(foreignGraph->release(foreignGrant));delete foreignGraph;assert(depStops==1);
 } else if(abi2Case()){
  assert(ran&&!runtime->retained()&&starts==1&&!entries&&!terminalCalls&&quiesces==1&&stops==1&&unmaps==1&&apps==1);
  assert(Peer::owner(*runtime).state==Executor::NativeState::Empty);
  delete runtime;delete files;
 } else if(scenario=="eager"||cold||scenario=="legacy-completed"||scenario=="nested-close"){
  assert(ran&&!runtime->retained()&&starts==1&&entries==1&&quiesces==1&&stops==1&&unmaps==1&&apps==1);
  assert(terminalCalls==(scenario=="legacy-completed"?0u:1u));delete runtime;delete files;
 } else if(scenario=="terminal-pending"||scenario=="legacy-active"||scenario=="pending-safe-tail"){
  assert(!ran&&runtime->retained()&&starts==1&&apps==1&&!quiesces&&!stops&&!unmaps);
  assert(terminalCalls==(scenario=="legacy-active"?0u:1u));
  if(scenario=="pending-safe-tail"){
   auto exact=Peer::grant(*runtime);auto& o=Peer::owner(*runtime);
   assert(o.state==Executor::NativeState::Closing&&!o.releaseAttempted);
   assert(!Peer::close(*runtime)&&terminalCalls==1&&Peer::grant(*runtime).slot==exact.slot);
   closeSignal=true;until([]{return integration_tasks_done();});integration_pump_tail();integration_finish_tasks();
   assert(Peer::close(*runtime));assert(terminalCalls==1&&quiesces==1&&stops==1&&unmaps==1);
   assert(!Peer::grant(*runtime).slot&&o.state==Executor::NativeState::Empty);
   assert(runtime->retained()); // Helper can retire; outer Runtime remains one-pass retained.
  }
 } else if(scenario=="app-exit-refusal"){
  assert(!ran&&runtime->retained()&&starts==1&&!apps&&!terminalCalls&&!quiesces&&!stops&&!unmaps);
 } else if(scenario=="stage-failure"||scenario=="destructor-grantless-retained"){
  assert(!ran&&runtime->retained()&&starts==1&&!entries&&!terminalCalls&&!quiesces&&!stops&&!unmaps);
 } else if(scenario=="map-failure"||scenario=="commit-failure"||scenario=="staged-grant-identity"){
  assert(injected&&!ran&&starts==1&&!entries&&!terminalCalls&&!apps);
  auto& o=Peer::owner(*runtime);auto g=Peer::grant(*runtime);
  assert(o.failure==Executor::NativeFailure::Close || o.failure==(scenario!="commit-failure"?Executor::NativeFailure::Mapping:Executor::NativeFailure::Commit));
  assert(!unmaps || (!g.slot&&!runtime->retained()));
 } else if(scenario=="malformed-terminal"||scenario=="destructor-clean-refusal"||malformedExtra){
  assert(!ran&&!starts&&!entries&&!terminalCalls&&!apps);
 } else if(scenario=="terminal-unknown-result"||scenario=="terminal-refused"||scenario=="terminal-retained"){
  assert(!ran&&runtime->retained()&&starts==1&&terminalCalls==1&&!quiesces&&!stops&&!unmaps&&apps==1);
  auto exact=Peer::grant(*runtime);auto& o=Peer::owner(*runtime);
  assert(o.state==Executor::NativeState::Retained&&!o.releaseAttempted&&Peer::graph(*runtime).holdsGrant(exact));
  assert(!Peer::close(*runtime)&&terminalCalls==1&&Peer::graph(*runtime).holdsGrant(exact));
 } else if(scenario=="close-pre-revoke"||scenario=="close-post-revoke"){
  assert(!ran&&runtime->retained()&&starts==1&&entries==1&&terminalCalls==1&&!stops&&!unmaps&&apps==1);
  auto& o=Peer::owner(*runtime);auto exact=Peer::grant(*runtime);
  assert(o.state==Executor::NativeState::Retained&&o.releaseAttempted&&Peer::graph(*runtime).holdsGrant(exact));
  const auto attempts=revokeAttempts,qs=quiesces;
  assert(!Peer::close(*runtime)&&revokeAttempts==attempts&&quiesces==qs&&terminalCalls==1);
  assert(quiesces==(scenario=="close-pre-revoke"?0u:1u)&&depStops==0);
 } else assert(false);
 if(scenario=="staged-grant-identity"){
  assert(identityChecked&&depStarts==2&&depStops==0&&foreignGraph->holdsGrant(foreignGrant));
  // The sticky backend fault correctly promotes this owner to Retained.
  // These post-run checks prove custody, not independent nonbusy Staged refusal.
  auto& g=Peer::graph(*runtime);auto& o=Peer::owner(*runtime);auto exact=Peer::grant(*runtime);
  assert(!g.lifecycleBusy()&&o.state==Executor::NativeState::Retained);
  assert(g.holdsGrant(exact)&&!o.releaseAttempted&&!o.closeInProgress&&depStops==0);
  assert(!Peer::close(*runtime)&&g.holdsGrant(exact)&&o.state==Executor::NativeState::Retained);
  assert(Peer::graph(*runtime).holdsGrant(Peer::ordinaryGrant(*runtime)));
  assert(foreignGraph->release(foreignGrant));delete foreignGraph;assert(depStops==1);
 }
 if(scenario=="destructor-grantless-retained"){
  assert(!Peer::grant(*runtime).slot&&Peer::owner(*runtime).state==Executor::NativeState::Retained&&depStarts==1&&depStops==0);
  const pid_t child=fork();assert(child>=0);
  if(!child){delete runtime;std::_Exit(91);}
  int status=0;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
  assert(Peer::metadataHeld(*runtime)&&depStops==0&&!unmaps);
 }
 if(scenario=="destructor-clean-refusal"){
  assert(!runtime->retained()&&Peer::owner(*runtime).state==Executor::NativeState::Empty&&depStops==1);
  delete runtime;delete files;
 }
 assert(networkCalls==0);std::fprintf(stderr,"BOUNDARY network_calls=%u\n",networkCalls);
 std::printf("PASS %s: actual Runtime selected caller; hosted boundaries; hardware=false\n",argv[1]);std::fflush(stdout);
 std::_Exit(0);
}
