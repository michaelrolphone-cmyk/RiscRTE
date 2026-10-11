// Composition test: the actual trusted executor, owned graph registration,
// graph activation and ESP_PLATFORM ModuleV2 mapping path run together. SHA256
// and structural/import/entry validation are real. Native allocation, relocation
// and the final machine-code entry are simulated; no Xtensa instructions run.
#include "runtime/drivers/DeviceProviderExecutorV2.h"
#include "runtime/drivers/NativeProviderPolicyValidationV1.h"
#include <openssl/evp.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace RuntimeProviders;
using RuntimePackages::DeviceProviderExecutorV2;
namespace {
struct ProviderState {
  unsigned slot=0, mappings=0, unmaps=0, starts=0, stops=0, quiesces=0, revokes=0;
  bool live=false, failStart=false, allowQuiesce=true;
};
struct Capability { unsigned generation=0; int value=42; } capabilities[2];
ProviderState providers[2];
std::vector<uint8_t> admittedBytes[2];
std::vector<std::vector<uint8_t>> mutatedInputs;
std::vector<std::string> events;
std::map<esp_elf_t*,unsigned> mapped;
std::set<void*> allocations;
std::set<const void*> callerInputs;
const risc_provider_dependency_v1* retainedDependencies=nullptr;
const void* retainedRootCapability=nullptr;
unsigned retainedRootGeneration=0, hashes=0;
const uint8_t* hashedImage=nullptr;
const char* const ids[]={"fixture-native-root","fixture-native-child"};
const char* const caps[]={"cap.native-root","cap.native-child"};

void event(unsigned slot,const char* text){events.push_back(std::to_string(slot)+":"+text);}
size_t eventIndex(unsigned slot,const char* text){
  const std::string wanted=std::to_string(slot)+":"+text;
  for(size_t i=0;i<events.size();++i)if(events[i]==wanted)return i;
  assert(false);return 0;
}
unsigned mappedFor(unsigned slot){unsigned n=0;for(const auto& entry:mapped)n+=entry.second==slot;return n;}
void dependencyIntact(){
  assert(retainedDependencies && retainedRootCapability==&capabilities[0]);
  assert(retainedDependencies[0].api==retainedRootCapability);
  assert(retainedDependencies[0].api_version==1);
  assert(!std::strcmp(retainedDependencies[0].capability_id,caps[0]));
  assert(!callerInputs.count(retainedDependencies));
  assert(providers[0].live && mappedFor(0)==1 && capabilities[0].generation==retainedRootGeneration);
  assert(capabilities[0].value==42);
}
bool begin(void* context){
  auto& state=*static_cast<ProviderState*>(context);assert(!state.live);
  state.live=true;event(state.slot,"begin");return true;
}
void revoke(void* context){
  auto& state=*static_cast<ProviderState*>(context);assert(state.live);
  state.live=false;++state.revokes;event(state.slot,"revoke");
}
bool start(unsigned slot,const risc_provider_dependency_v1* dependencies,size_t count){
  auto& state=providers[slot];assert(state.live && mappedFor(slot)==1);
  ++state.starts;event(slot,"start");
  if(slot){
    assert(count==1 && dependencies);retainedDependencies=dependencies;
    retainedRootCapability=dependencies[0].api;retainedRootGeneration=capabilities[0].generation;
    dependencyIntact();
  }else assert(!count && !dependencies);
  return !state.failStart;
}
bool quiesce(unsigned slot){
  auto& state=providers[slot];assert(!state.live && mappedFor(slot)==1);
  if(slot)dependencyIntact();
  ++state.quiesces;event(slot,"quiesce");return state.allowQuiesce;
}
void stop(unsigned slot){
  auto& state=providers[slot];assert(!state.live && mappedFor(slot)==1);
  if(slot)dependencyIntact();else assert(!mappedFor(1));
  ++state.stops;event(slot,"stop");
}
bool rootStart(const risc_provider_dependency_v1* deps,size_t count){return start(0,deps,count);}
bool childStart(const risc_provider_dependency_v1* deps,size_t count){return start(1,deps,count);}
bool rootQuiesce(){return quiesce(0);} bool childQuiesce(){return quiesce(1);}
void rootStop(){stop(0);} void childStop(){stop(1);}
risc_driver_v2 descriptors[2];
const risc_driver_v2* get(unsigned slot,uint32_t abi){
  assert(abi==RISC_PROVIDER_DRIVER_ABI_V2 && !providers[slot].live && mappedFor(slot)==1);
  event(slot,"entry");auto& descriptor=descriptors[slot];descriptor={};
  descriptor.abi_version=RISC_PROVIDER_DRIVER_ABI_V2;descriptor.struct_size=sizeof(descriptor);
  descriptor.driver_id=ids[slot];descriptor.capability_id=caps[slot];descriptor.capability_api=1;
  descriptor.capability=&capabilities[slot];descriptor.start=slot?childStart:rootStart;
  descriptor.stop=slot?childStop:rootStop;descriptor.quiesce=slot?childQuiesce:rootQuiesce;
  return &descriptor;
}
const risc_driver_v2* rootGet(uint32_t abi){return get(0,abi);}
const risc_driver_v2* childGet(uint32_t abi){return get(1,abi);}

std::vector<uint8_t> read(const char* path){
  FILE* f=std::fopen(path,"rb");assert(f);assert(!std::fseek(f,0,SEEK_END));
  auto n=std::ftell(f);assert(n>0);std::rewind(f);std::vector<uint8_t> out(static_cast<size_t>(n));
  assert(std::fread(out.data(),1,out.size(),f)==out.size());assert(!std::fclose(f));return out;
}
size_t textOffset(const uint8_t* bytes){
  const auto* header=reinterpret_cast<const elf32_hdr_t*>(bytes);
  const auto* sections=reinterpret_cast<const elf32_shdr_t*>(bytes+header->shoff);
  const char* names=reinterpret_cast<const char*>(bytes+sections[header->shstrndx].offset);
  for(unsigned i=0;i<header->shnum;++i)if(!std::strcmp(names+sections[i].name,".text"))return sections[i].offset;
  assert(false);return 0;
}
void registerOwned(GraphV2& graph,const std::vector<uint8_t>& fixture,unsigned slot){
  // Every caller-owned input is mutable and disappears before acquisition.
  auto bytes=fixture;bytes[textOffset(bytes.data())]=static_cast<uint8_t>(slot+1);
  admittedBytes[slot]=bytes;
  char id[64],capability[64],path[80],version[]="1.2.3";
  std::snprintf(id,sizeof(id),"%s",ids[slot]);
  std::snprintf(capability,sizeof(capability),"%s",caps[slot]);
  std::snprintf(path,sizeof(path),"%s.elf",ids[slot]);
  char importMemory[]="memcpy",importTick[]="xTaskGetTickCount";
  const char* importNames[]={importMemory,importTick};
  char requiredCap[64],requiredId[64];
  std::snprintf(requiredCap,sizeof(requiredCap),"%s",caps[0]);
  std::snprintf(requiredId,sizeof(requiredId),"%s",ids[0]);
  RequirementV2 requirements[]={{requiredCap,1,requiredId}};
  NativeProviderRequirementV1 approvedRequirements[]={{requiredCap,1}};
  NativeProviderPolicyV1 policy;
  policy.relativeElfPath=path;policy.driverId=id;policy.version=version;
  policy.capability=capability;policy.api=1;policy.osCpuAbi=1;policy.elfLength=bytes.size();
  policy.imports=importNames;policy.importCount=2;
  policy.requirements=slot?approvedRequirements:nullptr;policy.requirementCount=slot?1:0;
  unsigned n=0;assert(EVP_Digest(bytes.data(),bytes.size(),policy.sha256,&n,EVP_sha256(),nullptr)==1 && n==32);
  SpecV2 spec{id,nullptr,capability,1,slot?requirements:nullptr,slot?1u:0u};
  spec.requiredOsCpuAbi=1;spec.verifiedElfBytes=bytes.data();spec.verifiedElfLength=bytes.size();
  spec.declaredImports=importNames;spec.declaredImportCount=2;
  std::memcpy(spec.contentSha256,policy.sha256,32);spec.lease={&providers[slot],begin,revoke};
  callerInputs.insert(bytes.data());callerInputs.insert(importNames);
  callerInputs.insert(requirements);callerInputs.insert(&policy);
  assert(!graph.addVerified(spec)); // Matching public arguments do not authorize.
  assert(DeviceProviderExecutorV2::registerNativePolicy(graph,spec,policy));
  assert(graph.hasProvider(ids[slot],caps[slot],1) && mapped.empty());
  std::memset(bytes.data(),0xa5,bytes.size());
  std::memset(id,'x',sizeof(id));std::memset(capability,'x',sizeof(capability));
  std::memset(path,'x',sizeof(path));std::memset(version,'x',sizeof(version));
  std::memset(importMemory,'x',sizeof(importMemory));std::memset(importTick,'x',sizeof(importTick));
  std::memset(requiredCap,'x',sizeof(requiredCap));std::memset(requiredId,'x',sizeof(requiredId));
  importNames[0]=importNames[1]=nullptr;requirements[0]={nullptr,99};
  approvedRequirements[0]={nullptr,99};policy={};spec={};
  mutatedInputs.push_back(std::move(bytes));
}
void reset(){
  assert(mapped.empty() && allocations.empty());events.clear();callerInputs.clear();mutatedInputs.clear();hashes=0;
  retainedDependencies=nullptr;retainedRootCapability=nullptr;retainedRootGeneration=0;
  for(unsigned slot=0;slot<2;++slot){providers[slot]={};providers[slot].slot=slot;capabilities[slot]={};}
}
void poisonStack(){volatile unsigned char bytes[8192];for(size_t i=0;i<sizeof(bytes);++i)bytes[i]=static_cast<unsigned char>(i);}
void copiedAdmission(const std::vector<uint8_t>& fixture){
  reset();{
    GraphV2 graph;registerOwned(graph,fixture,0);registerOwned(graph,fixture,1);
    poisonStack();assert(graph.moduleCount()==2 && hashes==2 && mapped.empty());
    auto grant=graph.acquire(caps[1],1);assert(grant.slot && graph.interfaceFor(grant)==&capabilities[1]);
    assert(hashes==4 && providers[0].mappings==1 && providers[1].mappings==1);
    poisonStack();dependencyIntact();assert(graph.release(grant));
    assert(!graph.interfaceFor(grant) && graph.liveGrants()==0 && graph.shutdown());
    assert(mapped.empty() && providers[0].unmaps==1 && providers[1].unmaps==1);
    assert(eventIndex(1,"unmap")<eventIndex(0,"revoke"));
    assert(!allocations.empty()); // Owned policy/image remain until graph destruction.
  }assert(allocations.empty());
}
void retainedStart(const std::vector<uint8_t>& fixture){
  reset();{
    GraphV2 graph;registerOwned(graph,fixture,0);registerOwned(graph,fixture,1);
    auto root=graph.acquireFrom(ids[0],caps[0],1);assert(root.slot);
    providers[1].failStart=true;providers[1].allowQuiesce=false;
    auto child=graph.acquireFrom(ids[1],caps[1],1);
    assert(!child.slot && graph.interfaceFor(root)==&capabilities[0]);
    assert(!providers[1].live && providers[1].revokes==1 && providers[0].live);
    assert(providers[1].quiesces>=2 && mapped.size()==2 && !providers[0].unmaps && !providers[1].unmaps);
    const auto* dependencyTable=retainedDependencies;const unsigned starts=providers[1].starts;
    assert(graph.release(root) && !graph.interfaceFor(root));
    assert(!graph.shutdown() && !graph.acquire(caps[1],1).slot);
    assert(!graph.recoverFailedFrom("wrong-provider",caps[1],1));
    assert(!graph.recoverFailedFrom(ids[1],caps[1],1));
    poisonStack();dependencyIntact();assert(retainedDependencies==dependencyTable);
    assert(providers[1].starts==starts && providers[1].revokes==1 && providers[0].revokes==0);
    assert(mapped.size()==2 && !providers[0].stops && !providers[1].stops);
    providers[1].allowQuiesce=true;
    assert(graph.recoverFailedFrom(ids[1],caps[1],1) && graph.shutdown());
    assert(mapped.empty() && providers[1].unmaps==1 && providers[0].unmaps==1);
    assert(eventIndex(1,"unmap")<eventIndex(0,"revoke"));
    assert(providers[1].stops==1 && providers[0].stops==1 && providers[1].revokes==1);
    // Recovery preserves the original copied policy for a fresh mapping.
    providers[1].failStart=false;auto fresh=graph.acquire(caps[1],1);
    assert(fresh.slot && fresh.generation!=root.generation && hashes==6);
    assert(graph.interfaceFor(fresh)==&capabilities[1] && !graph.interfaceFor(root));
    dependencyIntact();assert(providers[0].mappings==2 && providers[1].mappings==2);
    // A failed active teardown keeps its exact pending-release grant retryable.
    providers[1].allowQuiesce=false;
    assert(!graph.release(fresh) && !graph.interfaceFor(fresh));
    assert(!graph.release(fresh) && providers[1].revokes==2);
    assert(!graph.recoverFailedFrom(ids[1],caps[1],1));dependencyIntact();
    assert(providers[1].unmaps==1 && providers[0].unmaps==1);
    providers[1].allowQuiesce=true;assert(graph.release(fresh) && graph.shutdown());
    assert(mapped.empty() && providers[1].unmaps==2 && providers[0].unmaps==2);
  }assert(allocations.empty());
}
}
extern "C" int mbedtls_sha256_ret(const unsigned char* bytes,size_t size,unsigned char digest[32],int mode){
  assert(mode==0 && !callerInputs.count(bytes));++hashes;hashedImage=bytes;unsigned n=0;
  return EVP_Digest(bytes,size,digest,&n,EVP_sha256(),nullptr)==1 && n==32?0:-1;
}
extern "C" void* heap_caps_malloc(size_t n,unsigned){void* p=std::malloc(n);if(p)assert(allocations.insert(p).second);return p;}
extern "C" void heap_caps_free(void* p){if(p){assert(allocations.erase(p)==1);std::free(p);}}
extern "C" bool esp_elf_privileged_selected_import_supported_with_diagnostics_v1(const char* name,uint32_t diagnosticAbi){
  if(diagnosticAbi)return false;
  return name && (!std::strcmp(name,"memcpy") || !std::strcmp(name,"xTaskGetTickCount"));}
extern "C" int esp_elf_relocate_privileged_selected_diagnostics_v1(esp_elf_t* elf,const uint8_t* bytes,size_t size,const char* const* imports,size_t count,uint32_t diagnosticAbi){
  assert(!diagnosticAbi);
  assert(bytes==hashedImage && !callerInputs.count(bytes) && !callerInputs.count(imports));
  assert(count==2 && !std::strcmp(imports[0],"memcpy") && !std::strcmp(imports[1],"xTaskGetTickCount"));
  unsigned slot=bytes[textOffset(bytes)]-1;assert(slot<2 && !mappedFor(slot));
  assert(size==admittedBytes[slot].size() && !std::memcmp(bytes,admittedBytes[slot].data(),size));
  assert(mapped.emplace(elf,slot).second);++providers[slot].mappings;
  capabilities[slot]={providers[slot].mappings,42};std::memset(elf,0,sizeof(*elf));
  static esp_symtab_t symbols[]={
    {reinterpret_cast<void*>(&rootGet),const_cast<char*>("t5_driver_get")},
    {reinterpret_cast<void*>(&childGet),const_cast<char*>("t5_driver_get")}};
  elf->num=1;elf->symtab=&symbols[slot];event(slot,"map");return 0;
}
extern "C" void esp_elf_deinit(esp_elf_t* elf){
  auto found=mapped.find(elf);assert(found!=mapped.end());unsigned slot=found->second;
  assert(!providers[slot].live);if(slot)dependencyIntact();else assert(!mappedFor(1));
  ++providers[slot].unmaps;event(slot,"unmap");mapped.erase(found);
}
int main(int argc,char** argv){
  assert(argc==2);auto fixture=read(argv[1]);copiedAdmission(fixture);retainedStart(fixture);
  std::puts("Selected provider actual graph + ESP Module: trusted registration, owned policy/import/image mutation, mapped dependencies, retained failure and exact retry PASS");
}
