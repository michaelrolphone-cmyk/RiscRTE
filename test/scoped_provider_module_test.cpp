// The production ESP_PLATFORM mapping branch uses real SHA256 and ELF policy
// validation. Only native allocation, relocation and provider machine code are
// substituted. Graph's private binder is exercised by this narrow friend probe;
// the separate graph suite exercises the real copied-policy registration path.
#include "runtime/drivers/ProviderModuleV2.h"
#include "runtime/drivers/NativeProviderPolicyValidationV1.h"
#include <openssl/evp.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace RuntimeProviders {
class GraphV2 {
 public:
  static void bind(ModuleV2& module, const uint8_t* bytes, size_t size,
                   const NativeProviderPolicyV1* policy) {
    module.bindGraphOwnedImage(bytes,size,policy);
  }
};
}
using namespace RuntimeProviders;
namespace {
std::vector<std::string> events;
const uint8_t* boundImage;
const uint8_t* hashedImage;
unsigned mappings, unmaps, hashes, snapshotAllocations;
bool failAllocation, failRelocation, failStart, allowQuiesce=true;
bool failStreamClose, failStreamRevoke, leaseLive, leaseRetained, retainAtStart;
bool withStreams, missingEntry, wrongIdentity;
const char* const imports[]={"memcpy","xTaskGetTickCount"};
void event(const char* text){events.emplace_back(text);}
bool begin(void*){assert(!leaseLive);leaseLive=true;event("begin");return true;}
void revoke(void*){assert(leaseLive);leaseLive=false;event("revoke");}
bool safe(void*){return !leaseRetained;}
bool start(const risc_provider_dependency_v1*,size_t){assert(leaseLive);event("start");if(retainAtStart)leaseRetained=true;return !failStart;}
bool quiesce(){assert(!leaseLive);event("quiesce");return allowQuiesce;}
void stop(){assert(!leaseLive);event("stop");}
bool bindStreams(const risc_stream_provider_v1*){event("bind");return true;}
bool open(risc_stream_provider_v1* out){*out={};out->context=44;event("stream-open");return true;}
void revokeStream(uint64_t){event("stream-revoke");}
void closeStream(uint64_t){event("stream-close");}
bool revokeChecked(uint64_t){assert(!leaseLive);event("stream-revoke");return !failStreamRevoke;}
bool closeChecked(uint64_t){assert(!leaseLive);event("stream-close");return !failStreamClose;}
const StreamHostV1 streamHost{open,revokeStream,closeStream,nullptr,nullptr,nullptr,revokeChecked,closeChecked,nullptr,nullptr};
const int capability=17;
risc_driver_streams_v2 descriptor;
const risc_driver_v2* get(uint32_t abi){
  assert(abi==RISC_PROVIDER_DRIVER_ABI_V2 && !leaseLive);event("entry");
  descriptor={};descriptor.driver.abi_version=RISC_PROVIDER_DRIVER_ABI_V2;
  descriptor.driver.struct_size=withStreams?sizeof(descriptor):sizeof(descriptor.driver);
  descriptor.driver.driver_id=wrongIdentity?"unexpected":"fixture-selected";
  descriptor.driver.capability_id="cap.selected";descriptor.driver.capability_api=1;
  descriptor.driver.capability=&capability;descriptor.driver.start=start;
  descriptor.driver.stop=stop;descriptor.driver.quiesce=quiesce;
  descriptor.bind_streams=withStreams?bindStreams:nullptr;
  return &descriptor.driver;
}
void reset(){
  assert(!leaseLive && !snapshotAllocations);events.clear();
  mappings=unmaps=hashes=0;failAllocation=failRelocation=failStart=false;
  allowQuiesce=true;failStreamClose=failStreamRevoke=leaseRetained=retainAtStart=false;
  withStreams=missingEntry=wrongIdentity=false;hashedImage=nullptr;
}
std::vector<uint8_t> read(const char* path){
  FILE* f=std::fopen(path,"rb");assert(f);assert(!std::fseek(f,0,SEEK_END));
  auto n=std::ftell(f);assert(n>0);std::rewind(f);std::vector<uint8_t> out(static_cast<size_t>(n));
  assert(std::fread(out.data(),1,out.size(),f)==out.size());assert(!std::fclose(f));return out;
}
NativeProviderPolicyV1 policyFor(const std::vector<uint8_t>& bytes){
  NativeProviderPolicyV1 p;
  p.relativeElfPath="fixture-selected.elf";p.driverId="fixture-selected";
  p.version="1.0.0";p.capability="cap.selected";p.api=1;p.osCpuAbi=1;
  p.elfLength=bytes.size();p.imports=imports;p.importCount=2;
  unsigned n=0;assert(EVP_Digest(bytes.data(),bytes.size(),p.sha256,&n,EVP_sha256(),nullptr)==1 && n==32);
  return p;
}
void configure(ModuleV2& module,std::vector<uint8_t>& bytes,const NativeProviderPolicyV1* policy){
  boundImage=bytes.data();GraphV2::bind(module,boundImage,bytes.size(),policy);
  static int leaseContext;
  assert(module.setLease({&leaseContext,begin,revoke,safe}));
  assert(module.setStreamHost(&streamHost));
}
bool load(ModuleV2& module,const std::vector<uint8_t>& bytes,const NativeProviderPolicyV1& policy){
  return module.loadVerifiedBytes(bytes.data(),bytes.size(),policy.sha256,
      imports,2,"fixture-selected","cap.selected",1,nullptr,0);
}
void expect(std::initializer_list<const char*> values){
  assert(events.size()==values.size());size_t i=0;
  for(const char* value:values){if(events[i]!=value)std::fprintf(stderr,"event %zu got %s expected %s\n",i,events[i].c_str(),value);assert(events[i++]==value);}
}
void admission(std::vector<uint8_t>& bytes,NativeProviderPolicyV1& policy){
  reset();ModuleV2 unbound;
  assert(!load(unbound,bytes,policy) && !mappings && !hashes);
  configure(unbound,bytes,nullptr);assert(!load(unbound,bytes,policy));assert(unbound.unload());
  ModuleV2 module;configure(module,bytes,&policy);
  auto foreign=bytes;assert(!load(module,foreign,policy) && !mappings && !hashes);
  auto changed=policy;changed.sha256[0]^=1;assert(!load(module,bytes,changed));
  const char* hidden[]={"memcpy","vTaskDelay"};
  assert(!module.loadVerifiedBytes(bytes.data(),bytes.size(),policy.sha256,hidden,2,
      "fixture-selected","cap.selected",1,nullptr,0));
  assert(load(module,bytes,policy));assert(mappings==1 && hashes==1 && hashedImage!=boundImage);
  assert(module.capability()==&capability && module.unload());assert(unmaps==1);
  expect({"entry","begin","start","revoke","quiesce","stop","unmap"});
  // Keep the same owned allocation, policy, public digest and length. A stale
  // successful load must never authorize modified bytes after a clean unload.
  bytes.back()^=1;assert(!load(module,bytes,policy));assert(mappings==1 && hashes==2);
  assert(std::strstr(module.lastError(),"snapshot-integrity"));assert(module.unload());
  bytes.back()^=1;assert(load(module,bytes,policy));assert(mappings==2 && hashes==3);assert(module.unload());
  assert(!snapshotAllocations);
}
void failures(std::vector<uint8_t>& bytes,NativeProviderPolicyV1& policy){
  reset();ModuleV2 oom;configure(oom,bytes,&policy);failAllocation=true;
  assert(!load(oom,bytes,policy) && !mappings && !hashes);assert(oom.unload());
  reset();ModuleV2 relocation;configure(relocation,bytes,&policy);failRelocation=true;
  assert(!load(relocation,bytes,policy) && mappings==1 && !unmaps);assert(relocation.unload());
  reset();ModuleV2 entry;configure(entry,bytes,&policy);missingEntry=true;
  assert(!load(entry,bytes,policy) && mappings==1 && unmaps==1);assert(entry.unload());
  reset();ModuleV2 identity;configure(identity,bytes,&policy);wrongIdentity=true;
  assert(!load(identity,bytes,policy) && unmaps==1);assert(identity.unload());
  reset();ModuleV2 startFailure;configure(startFailure,bytes,&policy);failStart=true;
  assert(!load(startFailure,bytes,policy) && unmaps==1 && !leaseLive);
  expect({"entry","begin","start","revoke","quiesce","stop","unmap"});
  assert(startFailure.unload());
}
void retained(std::vector<uint8_t>& bytes,NativeProviderPolicyV1& policy){
  reset();ModuleV2 partial;configure(partial,bytes,&policy);failStart=true;allowQuiesce=false;
  assert(!load(partial,bytes,policy));assert(!unmaps && !leaseLive && !partial.capability());
  assert(partial.state()==ModuleV2::State::Failed && !partial.unload() && !unmaps);
  allowQuiesce=true;assert(partial.unload() && unmaps==1);
  expect({"entry","begin","start","revoke","quiesce","quiesce","quiesce","stop","unmap"});
  reset();ModuleV2 active;configure(active,bytes,&policy);assert(load(active,bytes,policy));
  assert(active.pinConsumer());assert(!active.unload() && leaseLive && !unmaps);
  assert(active.unpinConsumer());allowQuiesce=false;assert(!active.unload());
  assert(!leaseLive && !unmaps && !active.capability());allowQuiesce=true;
  assert(active.unload() && unmaps==1);
  reset();ModuleV2 hostRetained;configure(hostRetained,bytes,&policy);retainAtStart=true;
  assert(!load(hostRetained,bytes,policy) && !unmaps && !leaseLive);
  expect({"entry","begin","start","revoke"});assert(!hostRetained.unload());
  // The production native retention fence is sticky. Clearing this test-only
  // control lets the harness dispose the allocation after proving preservation.
  leaseRetained=false;assert(hostRetained.unload() && unmaps==1);
}
void streams(std::vector<uint8_t>& bytes,NativeProviderPolicyV1& policy){
  reset();ModuleV2 clean;configure(clean,bytes,&policy);withStreams=true;
  assert(load(clean,bytes,policy) && clean.unload());
  expect({"entry","stream-open","bind","begin","start","revoke","stream-revoke","quiesce","stop","stream-close","unmap"});
  // Sticky stream cleanup retention intentionally remains mapped to process
  // exit. Failed activation must not call closeMapped after close/revoke fails.
  reset();static ModuleV2 closeRetained;configure(closeRetained,bytes,&policy);
  withStreams=failStart=failStreamClose=true;
  assert(!load(closeRetained,bytes,policy) && !unmaps && !leaseLive);
  assert(!closeRetained.unload() && !unmaps);
  expect({"entry","stream-open","bind","begin","start","revoke","stream-revoke","quiesce","stop","stream-close"});
  reset();static ModuleV2 revokeRetained;configure(revokeRetained,bytes,&policy);
  withStreams=failStart=failStreamRevoke=true;
  assert(!load(revokeRetained,bytes,policy) && !unmaps && !leaseLive);
  assert(!revokeRetained.unload() && !unmaps);
  expect({"entry","stream-open","bind","begin","start","revoke","stream-revoke"});
}
}
extern "C" int mbedtls_sha256_ret(const unsigned char* bytes,size_t size,unsigned char digest[32],int mode){
  assert(mode==0);++hashes;hashedImage=bytes;unsigned n=0;
  return EVP_Digest(bytes,size,digest,&n,EVP_sha256(),nullptr)==1 && n==32?0:-1;
}
extern "C" void* heap_caps_malloc(size_t n,unsigned){if(failAllocation)return nullptr;void* out=std::malloc(n);if(out)++snapshotAllocations;return out;}
extern "C" void heap_caps_free(void* p){if(p){assert(snapshotAllocations);--snapshotAllocations;std::free(p);}}
extern "C" bool esp_elf_privileged_selected_import_supported_with_diagnostics_v1(const char* name,uint32_t diagnosticAbi){
  if(diagnosticAbi)return false;
  return name && (!std::strcmp(name,"memcpy") || !std::strcmp(name,"xTaskGetTickCount"));
}
extern "C" int esp_elf_relocate_privileged_selected_diagnostics_v1(esp_elf_t* elf,const uint8_t* bytes,size_t size,const char* const* names,size_t count,uint32_t diagnosticAbi){
  assert(!diagnosticAbi);
  assert(bytes!=boundImage && bytes==hashedImage && size && names && count==2);++mappings;
  if(failRelocation)return -1;
  std::memset(elf,0,sizeof(*elf));
  static esp_symtab_t symbols[]={{reinterpret_cast<void*>(&get),const_cast<char*>("t5_driver_get")}};
  elf->num=missingEntry?0:1;elf->symtab=symbols;return 0;
}
extern "C" void esp_elf_deinit(esp_elf_t*){assert(!leaseLive);++unmaps;event("unmap");}
int main(int argc,char** argv){
  assert(argc==2);auto bytes=read(argv[1]);auto policy=policyFor(bytes);
  admission(bytes,policy);failures(bytes,policy);retained(bytes,policy);streams(bytes,policy);
  std::puts("Selected provider ModuleV2: private graph binding, copied-byte hashing, failed start, leases and retained cleanup PASS");
}
