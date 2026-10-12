#include "NativeSettingsAssembly.h"
#if RISC_NATIVE_SETTINGS_ASSEMBLY
#if !defined(ESP_PLATFORM) && !defined(RISC_NATIVE_SETTINGS_ASSEMBLY_HOST_TEST)
#error Settings assembly needs the selected ESP INTERNAL and PSRAM port
#endif
#include <soc/soc_memory_types.h>
#include "runtime/packages/PackageIdentity.h"
#include <cstring>
#include <new>
namespace RiscStorage {
bool AppDataFiles::configuredFor(const char*root,Hooks hooks)const{
 return ready()&&exitSafe()&&root&&!std::strcmp(root_,root)&&hooks_.context==hooks.context&&
  hooks_.now==hooks.now&&hooks_.cooperate==hooks.cooperate&&hooks_.allocate==hooks.allocate&&hooks_.deallocate==hooks.deallocate;
}
}
namespace RiscCpu { namespace NativeSettingsAssembly { namespace {
template<class T>struct Permanent {
 alignas(T) unsigned char bytes[sizeof(T)]{};
 template<class...Args>T*construct(Args&&...args){return new(bytes)T(static_cast<Args&&>(args)...);}
};
struct NamespaceControl {Permanent<Store>store;Permanent<C>core;};
struct State {
 NamespaceControl namespaces[2];Permanent<NativeSettingsBootstrap>bootstrap;
 Permanent<NativeSettingsMaintenance>maintenance;
 char names[6][96]{};NativeSettingsLifecycle::Writer writers[6]{};
 NativeSettingsMaintenance*selected=nullptr;bool attempted=false;
} state;
bool internal(const void*p,size_t n){return p&&n&&uintptr_t(p)<=UINTPTR_MAX-(n-1)&&esp_ptr_internal(p)&&esp_ptr_internal(static_cast<const uint8_t*>(p)+n-1);}
bool context(const void*p,size_t n){return p?internal(p,n):n==0;}
void*allocate(size_t n){const auto a=NativeSettingsPayloadAllocator::allocator();return a.allocate(a.context,n);}
void release(void*p){const auto a=NativeSettingsPayloadAllocator::allocator();a.release(a.context,p);}
bool valid(const Namespace&n,uint32_t ns){
 const auto&b=n.backend;
 if(!internal(b.files,sizeof(Files))||!internal(b.artifacts.concreteAddress(),b.artifacts.concreteBytes())||!internal(b.sources,sizeof(Sources))||
    !b.root||strnlen(b.root,160)>=160||!b.artifacts->boundTo(ns,b.root)||!b.artifacts->exitSafe()||
    !b.sources->boundTo(ns,b.artifacts)||!b.sources->exitSafe()||
    (b.legacy&&(!internal(b.legacy,sizeof(NativeSettingsBootstrap::Legacy))||!b.legacy->boundTo(ns,b.artifacts)||!b.legacy->exitSafe()))||
    !b.fileHooks.now||!b.fileHooks.cooperate||b.fileHooks.allocate!=allocate||b.fileHooks.deallocate!=release||
    !context(b.fileHooks.context,b.fileContextBytes)||!b.files->configuredFor(b.root,b.fileHooks)||
    !b.metadata.read||!b.metadata.write||!context(b.metadata.context,b.metadataContextBytes)||
    !b.guard.workerWithExclusiveLease||!b.guard.space||!context(b.guard.context,b.guardContextBytes)||
    !n.digest.begin||!n.digest.update||!n.digest.finish||!context(n.digest.context,n.digestContextBytes))return false;
 const auto&f=n.first;const bool first=f.otherWritersStopped||f.entropy||f.stagingCapacity||f.context||n.firstContextBytes;
 if(first&&(!f.otherWritersStopped||!f.entropy||!f.stagingCapacity||!context(f.context,n.firstContextBytes)))return false;
 if(n.initialIncarnation){uint8_t nonzero=0;for(unsigned i=0;i<16;++i)nonzero|=n.initialIncarnation[i];if(!nonzero)return false;}
 return true;
}
} // namespace
Files::Hooks fileHooks(void*p,uint32_t(*now)(void*),bool(*cooperate)(void*)){return {p,now,cooperate,allocate,release};}
const NativeSettingsLifecycle::Admission*assemble(const Inputs&i){
 if(state.attempted)return nullptr;
 state.attempted=true; // no reentrant selection or replacement, even on failure
 if(!i.owner||!i.owner()||!internal(&state,sizeof state)||!i.device||!i.writerCount||i.writerCount>6||!i.writers||
    !valid(i.preferences,1)||!valid(i.wifi,6))return nullptr;
 for(size_t n=0;n<i.writerCount;++n){const auto&w=i.writers[n];
  if(!w.app||strnlen(w.app,96)>=96||!RuntimePackages::safeId(w.app)||(w.nameSpace!=1&&w.nameSpace!=6))return nullptr;
  for(size_t before=0;before<n;++before)if(i.writers[before].nameSpace==w.nameSpace&&!std::strcmp(i.writers[before].app,w.app))return nullptr;
 }
 // Nothing supplied by the caller is borrowed except declared permanent native
 // backend/digest/authority controls. Writer strings and incarnation bytes copy.
 const Namespace*inputs[2]={&i.preferences,&i.wifi};
 NativeSettingsMaintenance::Namespace normal[2]{};
 NativeSettingsBootstrap::Namespace startup[2]{};
 const auto payload=NativeSettingsPayloadAllocator::allocator();
 for(unsigned n=0;n<2;++n){const auto&v=*inputs[n];const auto&b=v.backend;auto&out=state.namespaces[n];
  auto*s=out.store.construct(n?6u:1u,*b.files,b.metadata,b.guard,payload,b.artifacts);
  auto*c=out.core.construct(n?6u:1u,s->backend(),Store::limits(),payload);
  normal[n]={c,s,v.digest,payload,v.initialIncarnation};startup[n]={c,s,v.digest,payload,b.sources,v.first,b.legacy};
 }
 for(size_t n=0;n<i.writerCount;++n){std::strcpy(state.names[n],i.writers[n].app);state.writers[n]={state.names[n],i.writers[n].nameSpace};}
 auto*b=state.bootstrap.construct(startup[0],startup[1],i.device);
 state.selected=state.maintenance.construct(normal[0],normal[1],i.device,state.writers,i.writerCount,b);
 return &state.selected->admission();
}
const NativeSettingsLifecycle::Admission*admission(){return state.selected?&state.selected->admission():nullptr;}
NativeSettingsMaintenance*maintenance(){return state.selected;}
size_t controlBytes(){return sizeof state;}
} }
#endif
