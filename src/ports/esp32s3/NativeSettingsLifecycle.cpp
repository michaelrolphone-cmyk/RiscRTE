#include "NativeSettingsLifecycle.h"
#if defined(ESP_PLATFORM) && RISC_NATIVE_SETTINGS_LIFECYCLE
#include "NativeSettingsWorker.h"
#include "CpuPort.h"
#include <soc/soc_memory_types.h>
#include <atomic>
#include <cstring>
namespace RiscCpu { namespace NativeSettingsLifecycle { namespace {
namespace W=NativeSettingsWorker;
using Job=RiscExperimental::NativeReplicaControlJob;
using C=Core;
struct Load {
 Core*core=nullptr;Store*store=nullptr;Coordinator*service=nullptr;
 uint32_t slot=0;uint64_t token=0;Coordinator::Lease lease{};bool cancelled=false;
 std::atomic<bool>fault{false};Job job{execute,this};
 static Job::Result execute(void*p,uint32_t action,const void*,uint32_t){
  auto&s=*static_cast<Load*>(p);
  int rc=s.service->nativeWorker(s.slot)&&action==1?s.core->load():int(C::Invalid);
  s.store->finishOperation();if(!s.store->exitSafe())rc=C::Retained;
  if(rc==C::Retained)s.fault.store(true,std::memory_order_release);
  return {rc,0,s.core->revision()};
 }
 bool registerClient(){return service->registerNative({this,sizeof(*this),
  [](void*p){auto&s=*static_cast<Load*>(p);return s.service->nativeWorker(s.slot)&&s.job.workerStep();},
  [](void*p){return static_cast<Load*>(p)->job.pending();},
  [](void*p){auto&s=*static_cast<Load*>(p);return !s.token&&s.job.exitSafe();},
  [](void*p){return static_cast<Load*>(p)->fault.load(std::memory_order_acquire);}},&slot)==C::Ok;}
 int begin(){int rc=service->acquireNative(slot,&lease);if(rc)return rc;rc=job.begin(1,nullptr,0,&token);if(rc){const int release=service->releaseNative(lease);if(release)return release;lease={};}return rc;}
 void cancel(){if(job.cancel(token)==RISC_STORAGE_WORK_CANCELLED){token=0;cancelled=true;}}
 int collect(bool&done){done=false;Job::Result result{};const int rc=cancelled?RISC_STORAGE_WORK_CANCELLED:job.collect(token,&result);if(rc==RISC_STORAGE_WORK_PENDING)return C::Busy;if(rc!=RISC_STORAGE_WORK_OK&&rc!=RISC_STORAGE_WORK_CANCELLED)return C::Retained;
  done=true;token=0;if(rc==RISC_STORAGE_WORK_CANCELLED)result.status=C::Stale;const int release=service->releaseNative(lease);if(release)return release;lease={};return result.status;}
};
struct State {
 Admission admission{};Hooks hooks{};Load loads[2];char appIds[6][96]{};uint32_t namespaces[6]{};
 RiscBoot::Runtime*runtime=nullptr;Port*port=nullptr;
 bool selected=false,prepared=false,bound=false,hydrated=false,attached=false,entered=false,active=false,terminal=false;
 Timing time{};uint32_t clockAt=0;
} state;
bool internal(const void*p,size_t n){return p&&n&&uintptr_t(p)<=UINTPTR_MAX-(n-1)&&esp_ptr_internal(p)&&esp_ptr_internal(static_cast<const uint8_t*>(p)+n-1);}
bool owner(){return state.hooks.owner&&state.hooks.owner();}
bool selectedRetained(){
 if(state.terminal)return true;
 for(auto&load:state.loads)if(load.fault.load(std::memory_order_acquire)||(load.service&&load.service->retained()))return true;
 return state.prepared&&state.admission.retained(state.admission.context);
}
bool clientsSafe(){return W::exitSafe()&&state.admission.exitSafe(state.admission.context)&&(!state.port||!state.port->nativeStorageAttached());}
bool cancellationRequested(){return (state.hooks.cancelRequested&&state.hooks.cancelRequested())||(state.admission.cancelRequested&&state.admission.cancelRequested(state.admission.context));}
void measure(uint32_t&previous){const uint32_t now=state.hooks.millis();state.time.lastBlockedMs+=uint32_t(now-previous);previous=now;state.clockAt=now;if(state.time.lastBlockedMs>state.time.maxBlockedMs)state.time.maxBlockedMs=state.time.lastBlockedMs;}
void startTiming(){state.time.lastBlockedMs=0;state.time.lastPolls=0;state.time.cancellationRequested=false;state.clockAt=state.hooks.millis();}
bool drain(){
 uint32_t previous=state.clockAt;
 while(state.active){
  if(selectedRetained()){state.terminal=true;measure(previous);return false;}
  if(!state.time.cancellationRequested&&cancellationRequested()){
   state.time.cancellationRequested=true;state.admission.cancel(state.admission.context);
  }
  const auto progress=state.admission.poll(state.admission.context);
  if(state.time.lastPolls!=UINT32_MAX)++state.time.lastPolls;
  if(progress==Progress::Retained||selectedRetained()){state.terminal=true;measure(previous);return false;}
  // A terminal reply does not surrender uncollected custody. Keep draining.
  // In particular, Busy and a slow vendor call are not Retained or cancellation.
  if(progress!=Progress::Pending&&clientsSafe()){state.active=false;break;}
  state.hooks.schedulerDelay(1);measure(previous);
 }
 measure(previous);return true;
}
struct Entry {Entry(){state.entered=true;}~Entry(){state.entered=false;}};
} // namespace
bool prepare(const Admission*a,Hooks hooks){
 if(state.selected||!hooks.owner||!hooks.owner()||!hooks.schedulerDelay||!hooks.millis)return false;
 state.selected=true;state.hooks=hooks;
 if(!a||!internal(a,sizeof(*a))||!internal(&state,sizeof state)||!a->context||!internal(a->context,a->controlBytes)||
    !a->registerBeforeActivate||!a->begin||!a->poll||!a->cancel||!a->exitSafe||!a->retained||a->writerCount>6||(a->writerCount&&!a->writers))return false;
 const Namespace bindings[2]={a->preferences,a->wifi};
 for(unsigned i=0;i<2;++i){const auto&b=bindings[i];const uint32_t ns=i?6:1;
  if(!internal(b.core,sizeof(Core))||!internal(b.store,sizeof(Store))||b.core->namespaceId()!=ns||b.store->namespaceId()!=ns||!b.core->usesBackend(b.store->backend())||b.core->ready()||!b.store->exitSafe())return false;
 }
 if(a->writerCount&&!internal(a->writers,a->writerCount*sizeof(Writer)))return false;
 for(size_t i=0;i<a->writerCount;++i){const auto&w=a->writers[i];if(!w.app||strnlen(w.app,96)>=96||!RuntimePackages::safeId(w.app)||(w.nameSpace!=1&&w.nameSpace!=6))return false;
  for(size_t j=0;j<i;++j)if(a->writers[j].nameSpace==w.nameSpace&&!std::strcmp(a->writers[j].app,w.app))return false;
  std::strcpy(state.appIds[i],w.app);state.namespaces[i]=w.nameSpace;
 }
 state.admission=*a;
 for(unsigned i=0;i<2;++i){auto&load=state.loads[i];load.core=bindings[i].core;load.store=bindings[i].store;
  if(!W::prepare(i?6:1,*load.core,*load.store,hooks.owner))return false;
  load.service=W::coordinator(i?6:1);if(!load.service||!load.service->binds(*load.core,*load.store)||!load.registerClient())return false;
 }
 if(!a->exitSafe(a->context)||a->retained(a->context)||!a->registerBeforeActivate(a->context,*state.loads[0].service,*state.loads[1].service))return false;
 for(auto&load:state.loads)if(load.service->activate()!=C::Ok)return false;
 state.prepared=true;return true;
}
bool bind(RiscBoot::Runtime&r){
 if(!owner()||!state.prepared||state.bound||state.entered||!clientsSafe())return false;
 for(auto&load:state.loads)if(!r.registerAtomicKeyValueReadThrough(load.core->namespaceId(),load.service->endpoint()))return false;
 for(size_t i=0;i<state.admission.writerCount;++i)if(!r.registerAtomicKeyValue(state.appIds[i],state.namespaces[i],state.loads[state.namespaces[i]==6?1:0].service->endpoint()))return false;
 state.runtime=&r;state.bound=true;return true;
}
bool hydrate(RiscBoot::Runtime&r,Port&p){
 if(!owner()||!state.bound||state.runtime!=&r||state.hydrated||state.entered||selectedRetained())return false;
 state.port=&p;int results[2]={C::Invalid,C::Invalid};unsigned index=0;
 Entry entry;startTiming();uint32_t previous=state.hooks.millis();
 for(auto&load:state.loads){
  const int accepted=load.begin();if(accepted!=C::Ok)return false;
  int rc=C::Busy;bool collected=false;
  while(!collected){
   if(!state.time.cancellationRequested&&cancellationRequested()){
    state.time.cancellationRequested=true;load.cancel();
   }
   rc=load.collect(collected);if(state.time.lastPolls!=UINT32_MAX)++state.time.lastPolls;
   if(!collected){if(rc!=C::Busy){state.terminal=true;return false;}state.hooks.schedulerDelay(1);measure(previous);}
  }
  measure(previous);
  if(rc==C::Retained||selectedRetained()){state.terminal=true;return false;}
  if(!load.service->exitSafe()||(rc!=C::Ok&&rc!=C::Missing&&rc!=C::Initializing)||(rc==C::Ok&&!load.core->ready()))return false;
  results[index++]=rc;
 }
 if(results[0]!=C::Ok||results[1]!=C::Ok){
  if(!state.admission.beginPreApp||!clientsSafe())return false;
  const auto result=state.admission.beginPreApp(state.admission.context,r,p,results[0],results[1]);
  if(result==Progress::Retained||selectedRetained()){state.terminal=true;return false;}
  state.active=result==Progress::Pending||!clientsSafe();
  if(state.active&&!drain())return false;
 }
 state.hydrated=state.loads[0].core->ready()&&state.loads[1].core->ready()&&clientsSafe();return state.hydrated;
}
bool attachOwner(RiscBoot::Runtime&r,Port&p){
 if(!owner()||!state.hydrated||state.attached||state.runtime!=&r||state.entered||!clientsSafe())return false;
 state.port=&p;state.attached=true;return true;
}
bool checkpoint(bool admitNew){
 if(!owner()||!state.attached||state.entered)return !selectedRetained();
 if(selectedRetained())return false;
 Entry entry;
 if(state.active)return drain();
 if(!admitNew)return true;
 if(!clientsSafe()||(cancellationRequested()))return true;
 startTiming();const auto result=state.admission.begin(state.admission.context,*state.runtime,*state.port);
 if(result==Progress::Retained||selectedRetained()){state.terminal=true;return false;}
 // A clean Busy/no-work refusal leaves ordinary Files/USB/app admission intact.
 // If the callback acquired custody despite its result, drain that exact work.
 state.active=result==Progress::Pending||!clientsSafe();if(state.active)return drain();uint32_t previous=state.clockAt;measure(previous);return true;
}
void cooperate(){if(owner()&&state.active&&!state.entered){Entry entry;(void)drain();}}
bool retained(){return owner()&&selectedRetained();}
bool exitSafe(){return (!state.selected||owner())&&!state.entered&&!state.active&&!selectedRetained()&&W::exitSafe()&&(!state.prepared||clientsSafe());}
bool ready(){return owner()&&state.hydrated&&!selectedRetained();}
Timing timing(){return owner()?state.time:Timing{};}
} }
#endif
