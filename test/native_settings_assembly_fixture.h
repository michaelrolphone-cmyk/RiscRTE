#pragma once
#include "ports/esp32s3/NativeSettingsAssembly.h"
#include <esp_heap_caps.h>
#include <mutex>
namespace Assembly=RiscCpu::NativeSettingsAssembly;
namespace {
std::mutex payloadMutex;std::map<void*,size_t>payloads;
std::map<void*,size_t>internalControls;unsigned internalRequests=0;
unsigned payloadRequests=0;size_t payloadBytes=0,payloadPeak=0;
const void*refusedInternal=nullptr;bool failPayload=false;
}
void*heap_caps_malloc(size_t n,uint32_t caps){
 std::lock_guard<std::mutex>lock(payloadMutex);
 if(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)){
  ++internalRequests;if(scenario=="assembly-control-oom")return nullptr;
  void*p=std::calloc(1,n);assert(p);internalControls[p]=n;
  if(scenario=="assembly-control-range")refusedInternal=static_cast<uint8_t*>(p)+n-1;
  return p;
 }
 assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));++payloadRequests;
 if(failPayload)return nullptr;
 void*p=std::malloc(n);assert(p);payloads[p]=n;payloadBytes+=n;if(payloadBytes>payloadPeak)payloadPeak=payloadBytes;return p;
}
void heap_caps_free(void*p){
 std::lock_guard<std::mutex>lock(payloadMutex);
 const auto internal=internalControls.find(p);if(internal!=internalControls.end()){internalControls.erase(internal);std::free(p);return;}
 const auto i=payloads.find(p);assert(i!=payloads.end());
 for(size_t n=0;n<i->second;++n)assert(static_cast<uint8_t*>(p)[n]==0);
 payloadBytes-=i->second;payloads.erase(i);std::free(p);
}
bool esp_ptr_external_ram(const void*p){std::lock_guard<std::mutex>lock(payloadMutex);for(const auto&v:payloads)if(uintptr_t(p)>=uintptr_t(v.first)&&uintptr_t(p)-uintptr_t(v.first)<v.second)return true;return false;}
bool esp_ptr_internal(const void*p){return p&&p!=refusedInternal&&!esp_ptr_external_ram(p);}
struct AssemblyBackend {
 RiscStorage::AppDataFiles::Hooks hooks=Assembly::fileHooks(nullptr,F::now,F::cooperate);
 RiscStorage::AppDataFiles files{hooks};
 N metadata{{nullptr,[](void*){return F::onWorker&&F::lease;},[](void*){return F::initialized?ESP_OK:ESP_ERR_NVS_NOT_INITIALIZED;}}};
 RiscExperimental::CheckedFreshInitArtifacts artifacts;
 Sources sources;
 Hash sourceHash;LegacySources legacy;
 explicit AssemblyBackend(unsigned ns):artifacts(ns,F::rootPath.c_str(),{&files,[](void*){return F::onWorker&&F::lease;},F::cooperate,[](void*p){return static_cast<RiscStorage::AppDataFiles*>(p)->retained()?int(C::Retained):int(C::Ok);}}),
  sources(ns,nativeOwner,artifacts,{&files,[](void*){return F::onWorker&&F::lease;},[](void*){return F::initialized?ESP_OK:ESP_ERR_NVS_NOT_INITIALIZED;},[](void*p){return static_cast<RiscStorage::AppDataFiles*>(p)->retained()?int(C::Retained):int(C::Ok);}}),
  legacy(ns,nativeOwner,ns==1?"shared-00000001":"shared-00000006",S::ImageMax-C::HeaderV3-4,artifacts,
   {&sourceHash,sourceHash.api().begin,sourceHash.api().update,sourceHash.api().finish},
   {&files,[](void*){return F::onWorker&&F::lease;},[](void*){return uint64_t(1);},[](void*){return F::initialized?ESP_OK:ESP_ERR_NVS_NOT_INITIALIZED;},[](void*p){return static_cast<RiscStorage::AppDataFiles*>(p)->retained()?int(C::Retained):int(C::Ok);}},RiscCpu::NativeSettingsPayloadAllocator::allocator()){assert(files.configure(F::rootPath.c_str()));}
 Assembly::Backend input(){return {&files,&artifacts,&sources,F::rootPath.c_str(),hooks,0,metadata.api(),sizeof metadata,{nullptr,[](void*){return F::onWorker&&F::lease;},F::space},0,&legacy};}
};
static bool assembleMaintenance(){
 assert(!Assembly::admission()&&!Assembly::maintenance());
 auto*a=new AssemblyBackend(1);auto*b=new AssemblyBackend(6);auto*h1=new Hash;auto*h6=new Hash;
 uint8_t incarnation1[16]={0x71},incarnation6[16]={0x76};char writerName[]="maintenance-test";
 char writerNames[L::MaxWriters][32]{};L::Writer writers[L::MaxWriters]{};
 writers[0]={writerName,1};writers[1]={writerName,6};
 for(size_t n=2;n<L::MaxWriters;n++){snprintf(writerNames[n],sizeof(writerNames[n]),"assembly-writer-%u",unsigned(n));writers[n]={writerNames[n],1};}
 B::First::Authority first{nullptr,[](void*){return F::lease.load();},[](void*,uint8_t out[16]){static uint8_t serial=33;memset(out,0,16);out[0]=serial++;return true;},[](void*,uint32_t,uint32_t){return F::lease.load();}};
 if(startupCase()||scenario=="restore-no-checkpoint")first={};
 Assembly::Inputs input{{a->input(),h1->api(),sizeof(Hash),first,0,scenario=="no-incarnation"?nullptr:incarnation1},
  {b->input(),h6->api(),sizeof(Hash),first,0,incarnation6},9,writers,scenario=="max-writers"?L::MaxWriters:scenario=="six-writers"?6u:2u,owner};
 if(scenario=="assembly-no-owner")input.owner=nullptr;
 if(scenario=="assembly-owner-refuses")input.owner=[](){return false;};
 if(scenario=="assembly-no-device")input.device=0;
 if(scenario=="assembly-no-writers")input.writerCount=0;
 if(scenario=="assembly-many-writers")input.writerCount=L::MaxWriters+1;
 if(scenario=="assembly-bad-writer")writers[0].app="../bad";
 if(scenario=="assembly-duplicate-writer")writers[1].nameSpace=1;
 if(scenario=="assembly-wrong-writer-namespace")writers[0].nameSpace=3;
 if(scenario=="assembly-files-root")input.preferences.backend.root="/wrong";
 if(scenario=="assembly-files-hooks")input.preferences.backend.fileHooks.now=nullptr;
 if(scenario=="assembly-files-allocator")input.preferences.backend.fileHooks.allocate=F::secureAlloc;
 if(scenario=="assembly-files-context")input.preferences.backend.fileContextBytes=1;
 if(scenario=="assembly-artifact-namespace")input.preferences.backend.artifacts=&b->artifacts;
 if(scenario=="assembly-artifact-identity"){
  // Same namespace/root and callbacks do not substitute a different inspector.
  auto*other=new AssemblyBackend(1);input.preferences.backend.artifacts=&other->artifacts;
 }
 if(scenario=="assembly-artifact-missing")input.preferences.backend.artifacts=nullptr;
 if(scenario=="assembly-artifact-tail")refusedInternal=reinterpret_cast<uint8_t*>(&a->artifacts)+sizeof(a->artifacts)-1;
 if(scenario=="littlefs-assembly-only"||scenario.rfind("assembly-littlefs-",0)==0){
  using Little=RiscExperimental::CheckedLittleFsFreshInitArtifacts;
  Little::Mount mount{};mount.profile=Little::PinnedProfile;mount.partitionAddress=0x10000;mount.partitionBytes=0x80000;
  mount.vfs=&a->files;mount.partition=&a->metadata;// Construction precedes registration: there is no worker or accepted lease.
  assert(!mount.worker&&!mount.coordinator&&!mount.leaseEpoch&&!mount.custodyEpoch&&!mount.nativeLease);
  mount.mountEpoch=mount.rootEpoch=mount.writerFenceEpoch=mount.redirectEpoch=1;
  static const std::string fullRoot="/tmp/lfs-assembly-"+std::to_string(getpid());
  std::filesystem::create_directory(fullRoot);
  strcpy(mount.mountPath,"/tmp");strcpy(mount.privateRoot,fullRoot.c_str()+4);
  // Existing-directory configuration precedes the RAM-only assembly boundary.
  // It does not qualify an actual native mount or its POSIX mutation route.
  auto*littleFiles=new RiscStorage::AppDataFiles(a->hooks);assert(littleFiles->configure(fullRoot.c_str()));
  input.preferences.backend.files=littleFiles;input.preferences.backend.root=fullRoot.c_str();
  auto*little=new Little(1,mount,{});auto*source=new Sources(1,nativeOwner,*little,{});
  input.preferences.backend.artifacts=little;input.preferences.backend.sources=source;input.preferences.backend.legacy=nullptr;
  if(scenario=="assembly-littlefs-identity")input.preferences.backend.artifacts=new Little(1,mount,{});
  if(scenario=="assembly-littlefs-root")input.preferences.backend.root=fullRoot.c_str()+4;
  if(scenario=="assembly-littlefs-type")input.preferences.backend.artifacts=&a->artifacts;
  if(scenario=="assembly-littlefs-tail")refusedInternal=reinterpret_cast<uint8_t*>(little)+sizeof(*little)-1;
  // RAM binding admits supplied concrete permanent controls only. The empty
  // port is intentional: no mount qualification or storage callback is claimed.
 }

 if(scenario=="assembly-legacy-namespace")input.preferences.backend.legacy=&b->legacy;
 if(scenario=="assembly-legacy-tail")refusedInternal=reinterpret_cast<uint8_t*>(&a->legacy)+sizeof(a->legacy)-1;
 if(scenario=="assembly-source-namespace")input.preferences.backend.sources=&b->sources;
 if(scenario=="assembly-missing-source"||scenario=="restore-no-source")input.preferences.backend.sources=nullptr;
 if(scenario=="assembly-no-metadata")input.preferences.backend.metadata.read=nullptr;
 if(scenario=="assembly-metadata-range")input.preferences.backend.metadataContextBytes=0;
 if(scenario=="assembly-guard-range")input.preferences.backend.guardContextBytes=1;
 if(scenario=="assembly-no-guard")input.preferences.backend.guard.workerWithExclusiveLease=nullptr;
 if(scenario=="assembly-no-space")input.preferences.backend.guard.space=nullptr;
 if(scenario=="assembly-no-digest")input.preferences.digest.finish=nullptr;
 if(scenario=="assembly-digest-range")input.preferences.digestContextBytes=0;
 if(scenario=="assembly-partial-first")input.preferences.first.entropy=nullptr;
 if(scenario=="assembly-first-range")input.preferences.firstContextBytes=1;
 if(scenario=="assembly-zero-incarnation")memset(incarnation1,0,16);
 if(scenario=="assembly-external-control")refusedInternal=&a->files;
 if(scenario=="assembly-external-tail")refusedInternal=reinterpret_cast<uint8_t*>(&a->files)+sizeof(a->files)-1;
 const auto fs=F::fsTotal,nvs=F::nvsTotal,allocations=payloadRequests;
 const auto*admitted=Assembly::assemble(input);
 assert(fs==F::fsTotal&&nvs==F::nvsTotal&&allocations==payloadRequests&&iterators.empty()&&F::handles.empty()&&!services[0]&&!services[1]);
 if(scenario.rfind("assembly-",0)==0||scenario=="restore-no-source"){
  assert(!admitted&&!Assembly::admission()&&!Assembly::maintenance());assert(!Assembly::assemble(input));
  if(scenario=="assembly-control-oom"||scenario=="assembly-control-range")assert(internalRequests==1&&internalControls.empty());
  printf("Assembly %s refuses without backend I/O, payload allocation or registration PASS\n",scenario.c_str());return false;
 }
 assert(admitted&&admitted==Assembly::admission()&&!Assembly::assemble(input));
 if(scenario=="littlefs-assembly-only"){
  puts("RAM-only LittleFS factory: exact full mount/root, concrete identity and permanent range compose without I/O or activation PASS");return false;
 }

 maintenance=Assembly::maintenance();assert(maintenance&&admitted==&maintenance->admission());
 cores[0]=admitted->preferences.core;cores[1]=admitted->wifi.core;
 assert(cores[0]->namespaceId()==1&&cores[1]->namespaceId()==6&&!cores[0]->ready()&&!cores[1]->ready());
 assert(cores[0]->usesBackend(admitted->preferences.store->backend())&&cores[1]->usesBackend(admitted->wifi.store->backend()));
 memset(writerName,'x',sizeof writerName-1);memset(incarnation1,0,16);memset(incarnation6,0,16);input={};
 assert(!strcmp(admitted->writers[0].app,"maintenance-test")&&!strcmp(admitted->writers[1].app,"maintenance-test"));
 printf("Permanent factory assembly PASS; allocated INTERNAL-control=%zu; no selection I/O/payload allocation; copied writer/entropy inputs\n",Assembly::controlBytes());return true;
}
