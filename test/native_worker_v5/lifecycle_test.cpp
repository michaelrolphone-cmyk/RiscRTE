// Actual Module/Graph/Queue, native5 import admission and resident worker/scope.
// Host substitutions: ELF relocation/address span, pthread RTOS, physical lease.
// This is not actual Runtime startup, private network forwarding or hardware proof.
#include "runtime/drivers/ProviderGraphV2.h"
#include "runtime/drivers/ProviderOwnedSpecV2.h"
#include "runtime/streams/ProviderQueueHost.h"
#include "private/esp_privileged_os_cpu.h"
#include "private/elf_symbol.h"
#include "esp_elf.h"
#include <RiscCpuTaskScopeV1.h>
#include <RiscNativeRootV1.h>
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
class Runtime final {
public:
 static const NativeProviderPolicyV1* admit(GraphV2& g,const SpecV2& s,const NativeProviderPolicyV1& p) {
  if(!g.addManagerValidatedPrivileged(s,p))return nullptr;
  assert(g.nodes_[0].spec.requiredOsCpuAbi==5);
  return &g.nodes_[0].owned->nativePolicy->policy;
 }
 static bool mapping(GraphV2& g,GrantV2 v,const NativeProviderPolicyV1* p,uint64_t* out){return g.nativeTaskMapping(v,p,out);}
 static bool commit(GraphV2& g,GrantV2 v,const NativeProviderPolicyV1* p,uint64_t m){return g.commitNativeTaskOwner(v,p,m);}
 static GrantV2 stage(GraphV2& g,const NativeProviderPolicyV1* p){return g.stageNativeTaskOwner(p);}
};
}
namespace {
std::string scenario;
uintptr_t rawBegin=0x10000,codeBegin=0;size_t textBytes=0;
unsigned starts=0,quiesces=0,stops=0,unmaps=0,revokes=0,polls=0;
std::atomic<unsigned> entries{0};
std::atomic<bool> closeSignal{false};
uint64_t mapping=0,worker=0;
int anchor=42,capability=0,leaseIdentity=0;
bool healthy=true;
GraphV2* activeGraph=nullptr;GrantV2 activeGrant{};const NativeProviderPolicyV1* activePolicy=nullptr;
extern risc_driver_native_root_v1 descriptor;
template<class P> void until(P p){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!p()){assert(std::chrono::steady_clock::now()<end);std::this_thread::yield();}}
void owner(void* p){
 assert(p==&anchor&&!integration_is_creator());uint64_t scope=0;
 assert(risc_cpu_task_scope_current_v1(&scope)==RISC_CPU_SCOPE_OK);++entries;
 if(scenario=="committed-await-close")until([]{return closeSignal.load();});
 assert(risc_cpu_task_scope_request_close_v1(scope)==RISC_CPU_SCOPE_OK);
}
bool bind(const risc_stream_provider_v1* h){assert(h&&h->context);mapping=h->context;return true;}
void poll(uint32_t){++polls;assert(false);}
uint32_t nowMs(){return 1;}
bool start(const risc_provider_dependency_v1*,size_t n){
 assert(!n&&mapping&&integration_is_creator());++starts;
 risc_cpu_task_config_v1 c{};c.struct_size=sizeof(c);c.entry=owner;c.argument=&anchor;c.argument_bytes=sizeof(anchor);
 c.stack_bytes=4096;c.working_stack_bytes=1024;c.priority=3;c.core=-1;std::strcpy(c.name,"net-owner-v5");
 assert(risc_cpu_worker_start_v5(&c,&worker)==RISC_CPU_SCOPE_OK&&worker);return scenario!="failed-start";
}
bool quiesce(){
 ++quiesces;
 if(worker){assert(risc_cpu_worker_join_v5(worker)==RISC_CPU_SCOPE_OK);assert(risc_cpu_worker_release_v5(worker)==RISC_CPU_SCOPE_OK);worker=0;}
 return true;
}
void stop(){++stops;assert(!worker);}
int32_t health(uint64_t,risc_native_root_health_v1*){assert(false);return RISC_NATIVE_ROOT_INVALID;}
int32_t call(uint64_t,uint64_t,uint32_t,uint32_t,const void*,uint32_t,uint32_t,void*,uint32_t,uint32_t*){assert(false);return RISC_NATIVE_ROOT_INVALID;}
risc_native_root_v1 rootApi{1,sizeof(rootApi),call,health};
risc_driver_native_root_v1 descriptor{
 {{{2,sizeof(descriptor),"test-terminal-owner","platform.http-client",1,&capability,start,stop,quiesce},nullptr,bind},poll},
 RISC_DRIVER_NATIVE_ROOT_TAG_V1,RISC_DRIVER_NATIVE_ROOT_VERSION_V1,&rootApi};
const risc_driver_v2* get(uint32_t abi){assert(abi==2);return &descriptor.poll.streams.driver;}
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
 std::strcpy(f.imports+1,"risc_cpu_worker_start_v5");f.syms[1].name=1;f.syms[1].info=STB_GLOBAL<<4;
 unsigned at=unsigned(std::strlen(f.imports+1))+2;std::strcpy(f.imports+at,"t5_driver_get");
 f.syms[2].name=at;f.syms[2].value=0x1000;f.syms[2].size=4;f.syms[2].info=(STB_GLOBAL<<4)|STT_FUNC;f.syms[2].shndx=4;return f;
}

bool leaseBegin(void*){return integration_is_creator();}
void leaseRevoke(void*){++revokes;if(scenario=="revoke-loss")healthy=false;}
bool leaseSafe(void*){return integration_is_creator()&&healthy;}
}
extern "C" void integration_graph_tls(void*){assert(false);}
extern "C" int esp_elf_init(esp_elf_t* e){assert(e);std::memset(e,0,sizeof(*e));return 0;}
extern "C" int esp_elf_relocate(esp_elf_t* e,const uint8_t* b){
 assert(e&&b&&esp_elf_privileged_os_cpu_relocation_enter_v1(e));
 assert(elf_find_sym_default("risc_cpu_worker_start_v5")==reinterpret_cast<uintptr_t>(&risc_cpu_worker_start_v5));
 assert(!elf_find_sym_default("risc_cpu_worker_start_v2"));assert(esp_elf_privileged_os_cpu_relocation_leave_v1(e));
 e->sec[ELF_SEC_TEXT].addr=rawBegin;e->sec[ELF_SEC_TEXT].size=textBytes;
 const uintptr_t data[]={uintptr_t(&anchor),uintptr_t(&descriptor),uintptr_t(&rootApi),uintptr_t(&capability)};
 const auto lo=*std::min_element(data,data+4),hi=*std::max_element(data,data+4);
 e->sec[ELF_SEC_DATA].addr=lo;e->sec[ELF_SEC_DATA].size=hi-lo+sizeof(descriptor);
 e->num=1;e->symtab=symbols;return 0;
}
extern "C" uintptr_t elf_remap_text(esp_elf_t* e,uintptr_t p){assert(e&&(p==rawBegin||p==rawBegin+textBytes-1));return codeBegin+(p-rawBegin);}
extern "C" void esp_elf_deinit(esp_elf_t*){++unmaps;}
int main(int argc,char** argv){
 assert(argc==2);scenario=argv[1];
 const uintptr_t code[]={uintptr_t(&owner),uintptr_t(&quiesce)};
 codeBegin=*std::min_element(code,code+2);textBytes=*std::max_element(code,code+2)-codeBegin+1;
 auto bytes=image();const char* imports[]={"risc_cpu_worker_start_v5"};
 NativeProviderPolicyV1 p{};p.relativeElfPath="private.elf";p.driverId="test-terminal-owner";p.version="1.0.0";
 p.capability="platform.http-client";p.api=1;p.osCpuAbi=5;p.elfLength=sizeof(bytes);p.imports=imports;p.importCount=1;
 assert(nativeProviderDigest((const uint8_t*)&bytes,sizeof(bytes),p.sha256));
 SpecV2 spec{p.driverId,"/hosted/private.elf",p.capability,1,nullptr,0};spec.requiredOsCpuAbi=5;
 spec.verifiedElfBytes=(const uint8_t*)&bytes;spec.verifiedElfLength=sizeof(bytes);spec.declaredImports=imports;spec.declaredImportCount=1;
 std::memcpy(spec.contentSha256,p.sha256,32);spec.lease={&leaseIdentity,leaseBegin,leaseRevoke,leaseSafe};
 auto* graph=new GraphV2(RuntimeStreams::runtimeProviderStreamHost());activeGraph=graph;
 activePolicy=RiscBoot::Runtime::admit(*graph,spec,p);assert(activePolicy);
 assert(!graph->acquireFrom(p.driverId,p.capability,1).slot);
 assert(!graph->acquire(p.capability,1).slot);
 assert(!starts&&!entries.load()&&!unmaps&&integration_create_count()==0);
 auto copy=*activePolicy;
 assert(!RiscBoot::Runtime::stage(*graph,&copy).slot);
 if(scenario=="recursive-refusal"){
  RequirementV2 required{p.capability,1,p.driverId};
  SpecV2 ordinary{"ordinary","/ordinary.elf","test.ordinary",1,&required,1};
  assert(graph->addVerified(ordinary));
  assert(!graph->acquire("test.ordinary",1).slot);
  assert(!starts&&!entries.load()&&!unmaps);
 } else {
  activeGrant=RiscBoot::Runtime::stage(*graph,activePolicy);
  if(scenario=="failed-start"){
   assert(!activeGrant.slot&&starts==1&&!entries.load());
   until([]{return integration_tasks_done();});
   integration_pump_tail();integration_finish_tasks();
   assert(graph->recoverFailedFrom(p.driverId,p.capability,1));
   assert(unmaps==1&&!entries.load());
  } else {
   assert(activeGrant.slot&&starts==1&&worker&&integration_create_count()==1&&!entries.load());
   assert(!graph->interfaceFor(activeGrant)); // STAGED is not public capability authority.
   graph->poll(nowMs,nullptr);graph->service(1);
   assert(!polls&&!entries.load()&&!quiesces&&!stops&&!unmaps);
   uint64_t exact=0;assert(RiscBoot::Runtime::mapping(*graph,activeGrant,activePolicy,&exact)&&exact==mapping&&exact);
   auto stale=activeGrant;++stale.generation;
   uint64_t untouched=123;
   assert(!RiscBoot::Runtime::mapping(*graph,stale,activePolicy,&untouched)&&untouched==123);
   assert(!RiscBoot::Runtime::commit(*graph,stale,activePolicy,mapping));
   assert(!RiscBoot::Runtime::commit(*graph,activeGrant,&copy,mapping));
   assert(!entries.load()&&!unmaps);
   if(scenario=="staged-cancel"){
    (void)graph->release(activeGrant);
    until([]{return integration_tasks_done();});
    integration_pump_tail();integration_finish_tasks();
    if(graph->holdsGrant(activeGrant))assert(graph->release(activeGrant));
    assert(!entries.load()&&unmaps==1);
   } else if(scenario=="wrong-mapping"){
    assert(!RiscBoot::Runtime::commit(*graph,activeGrant,activePolicy,mapping+1));
    assert(graph->holdsGrant(activeGrant)&&!unmaps&&!quiesces&&!stops&&!entries.load());
    assert(RiscBoot::Runtime::commit(*graph,activeGrant,activePolicy,mapping));
    until([]{return integration_tasks_done();});
    integration_pump_tail();integration_finish_tasks();
    assert(entries.load()==1&&graph->release(activeGrant));
    assert(quiesces==1&&stops==1&&unmaps==1);
   } else {
    assert(scenario=="commit-clean"||scenario=="revoke-loss"||scenario=="committed-await-close");
    assert(RiscBoot::Runtime::commit(*graph,activeGrant,activePolicy,mapping));
    if(scenario=="committed-await-close"){
     until([]{return entries.load()==1;});
     assert(!graph->release(activeGrant)&&graph->holdsGrant(activeGrant));
     assert(!graph->release(activeGrant)&&graph->holdsGrant(activeGrant));
     assert(!revokes&&!quiesces&&!stops&&!unmaps);
     closeSignal=true; // Modeled owner request, not Graph/task-state mutation.
    }
    until([]{return integration_tasks_done();});
    assert(entries.load()==1);
    integration_pump_tail();integration_finish_tasks();
    const bool released=graph->release(activeGrant);
    if(scenario=="revoke-loss"){
     assert(!released&&revokes==1&&!quiesces&&!stops&&!unmaps&&graph->holdsGrant(activeGrant));
     assert(!graph->release(activeGrant)&&graph->holdsGrant(activeGrant));
     assert(revokes==1&&!quiesces&&!stops&&!unmaps);
    }
    else assert(released&&quiesces==1&&stops==1&&unmaps==1);
   }
  }
 }
 std::printf("PASS %s: actual Graph/Module/resident-v5; startup=false hardware=false\n",argv[1]);
 std::fflush(stdout);std::_Exit(0); // No process-lifetime destruction claim.
}
