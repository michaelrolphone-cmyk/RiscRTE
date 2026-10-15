#include "NativeAppDataMount.h"
#include "NativeSettingsBackend.h"
#if RISC_NATIVE_SETTINGS_BACKEND
// ESP newlib termios and Arduino binary constants use these same macro names.
// Keep the VFS declarations intact while admitting the factory's Arduino port.
#pragma push_macro("B110")
#pragma push_macro("B1000000")
#undef B110
#undef B1000000
#include "NativeSettingsAssembly.h"
#pragma pop_macro("B1000000")
#pragma pop_macro("B110")
#include "NativeSettingsWorker.h"
#include "NativeSettingsStartupEntropy.h"
#include "../../../experimental/native_replica/NvsReplicaMetadata.h"
#include <esp_timer.h>
#include <freertos/task.h>
#include "runtime/crypto/Sha256.h"
#include "NativeMemory.h"
#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <new>
namespace RiscNvs {esp_err_t initializationStatus();}
namespace RiscCpu { namespace NativeSettingsBackend { namespace {
namespace M=RiscAppDataMount;
namespace W=NativeSettingsWorker;
namespace F=NativeSettingsAssembly;
using C=RiscExperimental::NativeReplicaCore;
using Little=RiscExperimental::CheckedLittleFsFreshInitArtifacts;
using Metadata=RiscExperimental::NvsReplicaMetadata;
using Sources=RiscExperimental::CheckedNvsFreshSources;
using Legacy=RiscExperimental::CheckedNvsLegacySources;
// Stable logical firmware domain. This is not a card ID, MAC address or secret.
#ifndef RISC_NATIVE_SETTINGS_LOGICAL_OWNER
#define RISC_NATIVE_SETTINGS_LOGICAL_OWNER {'X','4','-','R','T','E','-','S','E','T','T','I','N','G','S',1}
#endif
#ifndef RISC_NATIVE_SETTINGS_VOLUME_INSTANCE
#define RISC_NATIVE_SETTINGS_VOLUME_INSTANCE 9
#endif
#ifndef RISC_NATIVE_SETTINGS_WRITERS
#define RISC_NATIVE_SETTINGS_WRITERS {"settings",1},{"paper_clock",1},{"battery",1},{"ble_scanner",1},{"contexts",1},{"wifi_settings",6}
#endif
constexpr uint8_t LogicalOwner[16]=RISC_NATIVE_SETTINGS_LOGICAL_OWNER;
constexpr uint64_t VolumeInstance=RISC_NATIVE_SETTINGS_VOLUME_INSTANCE;
constexpr NativeSettingsLifecycle::Writer Writers[]={RISC_NATIVE_SETTINGS_WRITERS};
bool internal(const void*p,size_t n){return p&&n&&uintptr_t(p)<=UINTPTR_MAX-(n-1)&&esp_ptr_internal(p)&&esp_ptr_internal(static_cast<const uint8_t*>(p)+n-1);}
struct Backend {
 uint32_t ns;
 bool(*owner)();
 M::Identity identity;
 Little::Mount binding;
 RiscCrypto::Sha256 hash;
 bool hashing=false;
#if RISC_NATIVE_SETTINGS_STARTUP_ENTROPY
 uint8_t incarnation[16]{};
#endif
 F::Files::Hooks hooks;
 F::Files files;
 Metadata metadata;
 Little artifacts;
 Sources sources;
 Legacy legacy;
 explicit Backend(uint32_t space,bool(*ownerCheck)(),const M::Identity&id,const Little::Mount&bound)
  :ns(space),owner(ownerCheck),identity(id),binding(bound),hooks(F::fileHooks(this,now,cooperate)),files(hooks),
   metadata({this,held,initialized}),artifacts(space,bound,port()),
   sources(space,LogicalOwner,artifacts,{this,held,initialized,observe}),
   legacy(space,LogicalOwner,space==1?"shared-00000001":"shared-00000006",
    F::Store::ImageMax-C::HeaderV3-4,artifacts,legacyDigest(),{this,held,epoch,initialized,observe},NativeSettingsPayloadAllocator::allocator()){
   hash.initialize();
  }
 static uint32_t now(void*){return uint32_t(esp_timer_get_time()/1000);}
 static esp_err_t initialized(void*){return RiscNvs::initializationStatus();}
 static bool held(void*p){
  auto&s=*static_cast<Backend*>(p);W::Custody worker{};M::Custody mount{};M::Identity id{};
  return W::custody(s.ns,&worker)&&worker.nameSpace==s.ns&&worker.worker&&worker.coordinator&&worker.generation&&M::custody(mount)&&M::identity(id)&&
   mount.access==M::Access::Settings&&mount.ownerTask==worker.worker&&
   id.context==s.identity.context&&id.vfs==s.identity.vfs&&id.partition==s.identity.partition&&
   id.mountGeneration==s.identity.mountGeneration&&id.rootGeneration==s.identity.rootGeneration&&
   !std::strcmp(id.mountPath,s.identity.mountPath)&&!std::strcmp(id.settingsRoot,s.identity.settingsRoot);
 }
 static uint64_t epoch(void*p){auto&s=*static_cast<Backend*>(p);W::Custody worker{};return held(p)&&W::custody(s.ns,&worker)?worker.generation:0;}
 static int observe(void*p){auto&s=*static_cast<Backend*>(p);return M::retained()||s.files.retained()||s.artifacts.retained()?int(C::Retained):int(C::Ok);}
 static bool cooperate(void*p){vTaskDelay(1);return observe(p)==C::Ok&&held(p);}
 static bool space(void*p,uint32_t*total,uint32_t*used){
  if(total)*total=0;if(used)*used=0;
  if(!total||!used||!held(p))return false;
  size_t t=0,u=0;if(!M::usage(&t,&u)||t!=F::Store::PartitionBytes||u>t||t>UINT32_MAX)return false;
  if(!held(p)||observe(p))return false;*total=uint32_t(t);*used=uint32_t(u);return true;
 }
 static int snapshot(void*p,Little::Mount*out){
  if(out)*out={};auto&s=*static_cast<Backend*>(p);
  if(observe(p))return C::Retained;
  if(!out||!held(p))return C::Busy;
  W::Custody worker{};M::Custody mount{};
  if(!W::custody(s.ns,&worker)||!M::custody(mount)||mount.ownerTask!=worker.worker)return C::Busy;
  *out=s.binding;out->worker=worker.worker;out->coordinator=worker.coordinator;
  out->nameSpace=worker.nameSpace;out->nativeLease=2;out->leaseEpoch=worker.generation;out->custodyEpoch=mount.generation;
  return C::Ok;
 }
 // Inspector paths are mount-relative and use only the captured fenced table.
 // No generic POSIX lookup, guessed VFS context or unguarded vendor entry occurs.
 static Backend* io(void*p,const void*v){auto*s=static_cast<Backend*>(p);if(v!=s->identity.context||!held(p)||observe(p)){errno=M::retained()?EIO:EBUSY;return nullptr;}return s;}
 Little::Port port(){return {this,snapshot,observe,cooperate,
  [](void*p,const void*v,const char*n,struct stat*out){auto*s=io(p,v);return s?s->identity.vfs->stat_p(const_cast<void*>(v),n,out):-1;},
  [](void*p,const void*v,const char*n){auto*s=io(p,v);return s?s->identity.vfs->open_p(const_cast<void*>(v),n,O_RDONLY,0):-1;},
  [](void*p,const void*v,int fd,void*out,size_t n)->ptrdiff_t{auto*s=io(p,v);return s?s->identity.vfs->read_p(const_cast<void*>(v),fd,out,n):-1;},
  [](void*p,const void*v,int fd){auto*s=io(p,v);return s?s->identity.vfs->close_p(const_cast<void*>(v),fd):-1;},
  [](void*p,const void*v,const char*n)->void*{auto*s=io(p,v);return s?s->identity.vfs->opendir_p(const_cast<void*>(v),n):nullptr;},
  [](void*p,const void*v,void*d,Little::Entry*out){auto*s=io(p,v);if(!s||!out)return -1;errno=0;
   const auto*e=s->identity.vfs->readdir_p(const_cast<void*>(v),static_cast<DIR*>(d));
   if(!e)return errno?-1:0;const size_t n=strnlen(e->d_name,sizeof e->d_name);
   if(n==sizeof e->d_name||n>=sizeof out->name){errno=EIO;return -1;}std::memcpy(out->name,e->d_name,n+1);return 1;},
  [](void*p,const void*v,void*d){auto*s=io(p,v);return s?s->identity.vfs->closedir_p(const_cast<void*>(v),static_cast<DIR*>(d)):-1;}};}
 static bool digestAllowed(void*p){auto&s=*static_cast<Backend*>(p);return !observe(p)&&!xPortInIsrContext()&&((s.owner&&s.owner())||held(p));}
 RiscExperimental::NativeBaselineAdmission::Digest legacyDigest(){return {this,
   [](void*p){auto&s=*static_cast<Backend*>(p);if(!digestAllowed(p))return false;s.hashing=s.hash.begin();return s.hashing;},
   [](void*p,const void*b,uint32_t n){auto&s=*static_cast<Backend*>(p);if(!s.hashing||!digestAllowed(p)||(!b&&n))return false;return s.hash.add(static_cast<const unsigned char*>(b),n);},
   [](void*p,uint8_t out[32]){auto&s=*static_cast<Backend*>(p);if(!out||!s.hashing||!digestAllowed(p))return false;const bool ok=s.hash.finish(out);s.hashing=false;return ok;}};}
#if RISC_NATIVE_SETTINGS_STARTUP_ENTROPY
 static bool ownerOrWorker(void*p){auto&s=*static_cast<Backend*>(p);return !xPortInIsrContext()&&!observe(p)&&((s.owner&&s.owner())||held(p));}
 static bool firstEntropy(void*p,uint8_t out[16]){auto&s=*static_cast<Backend*>(p);return NativeSettingsStartupEntropy::take(s.ns,NativeSettingsStartupEntropy::Purpose::FirstCheckpoint,p,held,out);}
#endif
 F::Namespace input(){
  const F::Backend backend{&files,&artifacts,&sources,identity.settingsRoot,hooks,sizeof(*this),metadata.api(),sizeof(metadata),{this,held,space},sizeof(*this),&legacy};
  const auto hashApi=legacyDigest();
  const NativeSettingsBootstrap::Catalog::Digest digest{hashApi.context,hashApi.begin,hashApi.update,hashApi.finish};
#if RISC_NATIVE_SETTINGS_STARTUP_ENTROPY
  // The first-checkpoint guard separately proves the exact namespace lease on
  // owner and worker. All native file writers share the fixed fenced mount;
  // selected KV writers share the coordinator. The immutable table/root cannot
  // be replaced. SD capacity is admitted by the typed session's worker receipt,
  // never by a synthetic stagingCapacity callback.
  uint8_t any=0;for(uint8_t b:incarnation)any|=b;
  // Keep guarded resume available even if capture failed: a durable first
  // marker already contains its identities. New identity requests still fail.
  return {backend,digest,sizeof(*this),{this,ownerOrWorker,firstEntropy,nullptr},sizeof(*this),any?incarnation:nullptr};
#else
  return {backend,digest,sizeof(*this),{},0,nullptr};
#endif
 }
};
template<class T>struct Permanent {alignas(T) unsigned char bytes[sizeof(T)]{};template<class...Args>T*construct(Args&&...args){return new(bytes)T(static_cast<Args&&>(args)...);}};
struct State {Permanent<Backend>namespaces[2];const NativeSettingsLifecycle::Admission*selected=nullptr;bool attempted=false;} state;
} // namespace
bool prepare(bool(*owner)()){
 if(state.attempted)return false;state.attempted=true;
 if(!owner||xPortInIsrContext()||!owner()||!internal(&state,sizeof state)||RiscNvs::initializationStatus()!=ESP_OK)return false;
 M::Guard guard(M::Access::Settings);M::Identity id{};
 if(!guard.held||!M::identity(id)||std::strcmp(id.mountPath,"/appdata")||std::strcmp(id.settingsRoot,"/appdata/.native-settings")||
    !id.context||!id.vfs||!id.partition||id.partition->address!=0x270000||id.partition->size!=0x80000||id.partition->encrypted||
    !id.mountGeneration||!id.rootGeneration)return false;
 Little::Mount bound{};bound.profile=Little::PinnedProfile;bound.vfs=id.context;bound.partition=id.partition;
 bound.partitionAddress=id.partition->address;bound.partitionBytes=id.partition->size;
 bound.mountEpoch=id.mountGeneration;bound.rootEpoch=id.rootGeneration;
 // These epochs derive from the same registration that permanently installs
 // the all-writer fenced table and rejects replacement/nested mount routes.
 bound.writerFenceEpoch=bound.redirectEpoch=id.mountGeneration;
 std::strcpy(bound.mountPath,id.mountPath);std::strcpy(bound.privateRoot,id.settingsRoot+std::strlen(id.mountPath));
 auto*one=state.namespaces[0].construct(1,owner,id,bound);auto*six=state.namespaces[1].construct(6,owner,id,bound);
 if(!one->files.configure(id.settingsRoot)||!six->files.configure(id.settingsRoot)||M::retained())return false;
#if RISC_NATIVE_SETTINGS_STARTUP_ENTROPY
 if(!NativeSettingsStartupEntropy::take(1,NativeSettingsStartupEntropy::Purpose::Incarnation,one,Backend::ownerOrWorker,one->incarnation)||
    !NativeSettingsStartupEntropy::take(6,NativeSettingsStartupEntropy::Purpose::Incarnation,six,Backend::ownerOrWorker,six->incarnation)) {
  // Failed entropy does not hide an existing native/SD baseline. Only operations
  // requiring a fresh identity are unavailable; no clock/MAC/counter fallback.
  C::wipe(one->incarnation,sizeof one->incarnation);C::wipe(six->incarnation,sizeof six->incarnation);
 }
#endif
 state.selected=F::assemble({one->input(),six->input(),VolumeInstance,Writers,sizeof Writers/sizeof Writers[0],owner});
 return state.selected&&!M::retained();
}
const NativeSettingsLifecycle::Admission*admission(){return state.selected;}
size_t controlBytes(){return sizeof state;}
} }
extern "C" const RiscCpu::NativeSettingsLifecycle::Admission*risc_native_settings_admission(){return RiscCpu::NativeSettingsBackend::admission();}
#endif
