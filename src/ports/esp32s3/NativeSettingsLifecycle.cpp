#include "NativeSettingsLifecycle.h"
#if defined(ESP_PLATFORM) && RISC_NATIVE_SETTINGS_LIFECYCLE
#include "NativeSettingsWorker.h"
#include "CpuPort.h"
#include "NativeMemory.h"
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
 Admission admission{};Hooks hooks{};Load loads[2];char appIds[MaxWriters][96]{};uint32_t namespaces[MaxWriters]{};
 RiscBoot::Runtime*runtime=nullptr;Port*port=nullptr;
 bool selected=false,prepared=false,bound=false,hydrated=false,attached=false,entered=false,active=false,terminal=false;
 Timing time{};uint32_t clockAt=0,nextRetry=0;
 int loadResults[2]={C::Invalid,C::Invalid};
 uint32_t availability[2]={RISC_KEY_VALUE_UNAVAILABLE,RISC_KEY_VALUE_UNAVAILABLE};
 uint64_t generation[2]={1,1};
 bool discovery=false,stopping=false;
} state;
bool internal(const void*p,size_t n){return p&&n&&uintptr_t(p)<=UINTPTR_MAX-(n-1)&&esp_ptr_internal(p)&&esp_ptr_internal(static_cast<const uint8_t*>(p)+n-1);}
bool owner(){return state.hooks.owner&&state.hooks.owner();}
bool selectedRetained(){
 if(state.terminal||(state.runtime&&state.runtime->keyValueRetentionObserved()))return true;
 for(auto&load:state.loads)if(load.fault.load(std::memory_order_acquire)||(load.service&&load.service->retained()))return true;
 return state.prepared&&state.admission.retained(state.admission.context);
}
bool clientsSafe(){return W::exitSafe()&&state.admission.exitSafe(state.admission.context)&&(!state.port||!state.port->nativeStorageAttached());}
bool cancellationRequested(){return (state.hooks.cancelRequested&&state.hooks.cancelRequested())||(state.admission.cancelRequested&&state.admission.cancelRequested(state.admission.context));}
void measure(uint32_t&previous){const uint32_t now=state.hooks.millis();state.time.lastBlockedMs+=uint32_t(now-previous);previous=now;state.clockAt=now;if(state.time.lastBlockedMs>state.time.maxBlockedMs)state.time.maxBlockedMs=state.time.lastBlockedMs;}
void startTiming(){state.time.lastBlockedMs=0;state.time.lastPolls=0;state.time.cancellationRequested=false;state.clockAt=state.hooks.millis();}
bool pauseAtomic(bool paused){
 for(auto&load:state.loads)if(!load.service->pauseAtomic(paused)){state.terminal=true;return false;}
 return true;
}
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
  if(progress!=Progress::Pending&&clientsSafe()){state.active=false;if(!state.discovery&&!pauseAtomic(false))return false;break;}
  state.hooks.schedulerDelay(1);measure(previous);
 }
 measure(previous);return true;
}
void setAvailability(unsigned i,uint32_t value){
 if(state.availability[i]!=value){state.availability[i]=value;if(state.generation[i]!=UINT64_MAX)++state.generation[i];}
}
bool unresolved(){for(unsigned i=0;i<2;++i)if(namespaceSelected(i)&&state.availability[i]!=RISC_KEY_VALUE_AVAILABLE)return true;return false;}
void reconcile(){
 if(state.active||!clientsSafe())return;
 for(unsigned i=0;i<2;++i)if(namespaceSelected(i)&&state.availability[i]!=RISC_KEY_VALUE_AVAILABLE&&state.loads[i].core->ready()){
  setAvailability(i,RISC_KEY_VALUE_RECONCILIATION_PENDING);
  if(state.runtime->keyValueReconciliationSafe(i?6:1)){setAvailability(i,RISC_KEY_VALUE_AVAILABLE);state.loadResults[i]=C::Ok;}
 }
}
bool pollDiscovery(){
 if(!state.active||!state.discovery)return true;
 if(selectedRetained()){state.terminal=true;return false;}
 if(!state.time.cancellationRequested&&cancellationRequested()){
  state.time.cancellationRequested=true;state.admission.cancel(state.admission.context);
 }
 const auto progress=state.admission.poll(state.admission.context);
 if(state.time.lastPolls!=UINT32_MAX)++state.time.lastPolls;
 if(progress==Progress::Retained||selectedRetained()){state.terminal=true;return false;}
 if(progress!=Progress::Pending&&clientsSafe()){
  state.active=state.discovery=false;state.nextRetry=state.hooks.millis()+1000;
  for(unsigned i=0;i<2;++i)if(namespaceSelected(i)&&state.availability[i]!=RISC_KEY_VALUE_AVAILABLE){
   if(state.loads[i].core->ready())setAvailability(i,RISC_KEY_VALUE_RECONCILIATION_PENDING);
   else {const auto result=state.admission.sourceStatus?state.admission.sourceStatus(state.admission.context,i?6:1):progress;
    setAvailability(i,result==Progress::Failed?RISC_KEY_VALUE_CONFLICT:RISC_KEY_VALUE_UNAVAILABLE);}
  }
 }
 return true;
}
bool pollMaintenance(){
 if(!state.active)return true;
 uint32_t previous=state.hooks.millis();
 if(selectedRetained()){state.terminal=true;return false;}
 if(!state.time.cancellationRequested&&cancellationRequested()){
  state.time.cancellationRequested=true;state.admission.cancel(state.admission.context);
 }
 const auto progress=state.admission.poll(state.admission.context);
 if(state.time.lastPolls!=UINT32_MAX)++state.time.lastPolls;
 measure(previous);
 if(progress==Progress::Retained||selectedRetained()){state.terminal=true;return false;}
 // Neither a terminal reply nor a cancellation surrenders uncollected custody.
 if(progress!=Progress::Pending&&clientsSafe()){
  state.active=false;if(!pauseAtomic(false))return false;
 }
 return true;
}
bool discover(){
 if(state.stopping)return true;
 if(state.active)return state.discovery?pollDiscovery():pollMaintenance();
 reconcile();
 if(!unresolved()||!state.admission.beginPreApp||!clientsSafe()||cancellationRequested()||
    int32_t(state.hooks.millis()-state.nextRetry)<0)return true;
 // A ready but unpublished source is private until all data grants are gone.
 // Do not run ordinary maintenance or start discovery for that source again.
 int codes[2];bool missing=false;
 for(unsigned i=0;i<2;++i){codes[i]=!namespaceSelected(i)||state.loads[i].core->ready()?C::Ok:state.loadResults[i];missing|=codes[i]!=C::Ok;}
 if(!missing)return true;
 startTiming();
 const auto result=state.admission.beginPreApp(state.admission.context,*state.runtime,*state.port,codes[0],codes[1]);
 if(result==Progress::Retained||selectedRetained()){state.terminal=true;return false;}
 state.active=result==Progress::Pending||!clientsSafe();state.discovery=state.active;
 if(!state.active){state.nextRetry=state.hooks.millis()+1000;
  for(unsigned i=0;i<2;++i)if(namespaceSelected(i)&&!state.loads[i].core->ready())setAvailability(i,result==Progress::Failed?RISC_KEY_VALUE_CONFLICT:RISC_KEY_VALUE_UNAVAILABLE);
  reconcile();
 }
 return true;
}
struct Entry {Entry(){state.entered=true;}~Entry(){state.entered=false;}};
} // namespace
bool prepare(const Admission*a,Hooks hooks){
 if(state.selected||!hooks.owner||!hooks.owner()||!hooks.schedulerDelay||!hooks.millis)return false;
 state.selected=true;state.hooks=hooks;
 if(!a||!internal(a,sizeof(*a))||!internal(&state,sizeof state)||!a->context||!internal(a->context,a->controlBytes)||
    !a->registerBeforeActivate||!a->begin||!a->poll||!a->cancel||!a->exitSafe||!a->retained||a->writerCount>MaxWriters||(a->writerCount&&!a->writers))return false;
 const Namespace bindings[2]={a->preferences,a->wifi};
 for(unsigned i=0;i<2;++i){const auto&b=bindings[i];const uint32_t ns=i?6:1;
  if(!internal(b.core,sizeof(Core))||!internal(b.store,sizeof(Store))||b.core->namespaceId()!=ns||b.store->namespaceId()!=ns||!b.core->usesBackend(b.store->backend())||b.core->ready()||!b.store->exitSafe())return false;
 }
 if(a->writerCount&&!internal(a->writers,a->writerCount*sizeof(Writer)))return false;
 for(size_t i=0;i<a->writerCount;++i){const auto&w=a->writers[i];if(!w.app||strnlen(w.app,96)>=96||!RuntimePackages::safeId(w.app)||(w.nameSpace!=1&&w.nameSpace!=6)||!namespaceSelected(w.nameSpace==6?1:0))return false;
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
 for(unsigned i=0;i<2;++i){if(!namespaceSelected(i))continue;auto&load=state.loads[i];
  if(!r.registerAtomicKeyValueReadThrough(load.core->namespaceId(),load.service->endpoint())||
     !r.registerKeyValueAvailability(load.core->namespaceId(),{nullptr,[](void*,uint32_t ns,risc_key_value_availability_state_v1*out){return availability(ns,out);}}))return false;
 }
 for(size_t i=0;i<state.admission.writerCount;++i)if(!r.registerAtomicKeyValue(state.appIds[i],state.namespaces[i],state.loads[state.namespaces[i]==6?1:0].service->endpoint()))return false;
 state.runtime=&r;state.bound=true;return true;
}
bool hydrate(RiscBoot::Runtime&r,Port&p){
 if(!owner()||!state.bound||state.runtime!=&r||state.hydrated||state.entered||selectedRetained())return false;
 state.port=&p;int results[2]={C::Invalid,C::Invalid};unsigned index=0;
 Entry entry;startTiming();uint32_t previous=state.hooks.millis();
 for(auto&load:state.loads){
  if(!namespaceSelected(index)){results[index++]=C::Ok;continue;}
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
 if((results[0]!=C::Ok||results[1]!=C::Ok)&&!state.admission.beginPreApp)return false;
 if(!clientsSafe())return false;
 for(unsigned i=0;i<2;++i){state.loadResults[i]=results[i];setAvailability(i,results[i]==C::Ok?RISC_KEY_VALUE_AVAILABLE:RISC_KEY_VALUE_UNAVAILABLE);}
 state.hydrated=true;return true;
}

bool attachOwner(RiscBoot::Runtime&r,Port&p){
 if(!owner()||!state.hydrated||state.attached||state.runtime!=&r||state.entered||!clientsSafe())return false;
 state.port=&p;state.attached=true;return true;
}
bool checkpoint(bool admitNew){
 if(!owner()||!state.attached||state.entered)return !selectedRetained();
 if(selectedRetained())return false;
 Entry entry;
 if(state.active){if(state.discovery){if(!pollDiscovery())return false;reconcile();return true;}return pollMaintenance();}
 if(unresolved()){
  reconcile();
  // Only the explicit post-entry discovery checkpoint can start discovery.
  // Eager/cold provider activation must finish before SD/radio custody begins.
  // Maintenance may synchronize a healthy sibling while the other is absent.
  // Never export a loaded source that still awaits the data-grant boundary.
  for(unsigned i=0;i<2;++i)if(namespaceSelected(i)&&state.availability[i]!=RISC_KEY_VALUE_AVAILABLE&&state.loads[i].core->ready())return true;
 }
 if(!admitNew||state.stopping)return true;
 if(!clientsSafe()||(cancellationRequested()))return true;
 // Both namespaces stay BUSY to new atomic snapshots until this complete
 // sweep is collected. Existing draft grants already fence admission in Runtime.
 if(!pauseAtomic(true))return false;
 startTiming();const auto result=state.admission.begin(state.admission.context,*state.runtime,*state.port);
 if(result==Progress::Retained||selectedRetained()){state.terminal=true;return false;}
 // A clean Busy/no-work refusal leaves ordinary Files/USB/app admission intact.
 // Custody acquired despite a terminal result still needs later collection.
 state.active=result==Progress::Pending||!clientsSafe();
 uint32_t previous=state.clockAt;measure(previous);
 return state.active||pauseAtomic(false);
}
bool discoveryCheckpoint(){
 if(!owner()||!state.attached||state.entered)return !selectedRetained();
 if(selectedRetained())return false;
 Entry entry;const bool ok=discover();if(ok)reconcile();return ok;
}
void cooperate(){if(owner()&&state.active&&!state.entered){Entry entry;if(state.discovery)(void)pollDiscovery();else (void)pollMaintenance();}}
bool continuing(){return owner()&&state.hydrated&&state.active&&!selectedRetained();}
bool discovering(){return owner()&&state.hydrated&&state.discovery&&state.active&&!selectedRetained();}
bool shutdown(){
 if(!owner()||state.entered||selectedRetained())return false;
 Entry entry;state.stopping=true;
 if(state.active){
  // Discovery has no durable-save obligation. Accepted maintenance must finish
  // its existing SD acknowledgment before final graph teardown, unless an
  // explicit cancellation latch requests cancellation during the drain.
  if(state.discovery){state.admission.cancel(state.admission.context);state.time.cancellationRequested=true;}
  state.clockAt=state.hooks.millis();if(!drain())return false;state.discovery=false;
 }
 return clientsSafe();
}
int32_t availability(uint32_t ns,risc_key_value_availability_state_v1*out){
 if(!owner()||!state.hydrated||selectedRetained())return RISC_KEY_VALUE_AVAILABILITY_CONTEXT;
 if(!out||out->struct_size<sizeof(*out)||(ns!=1&&ns!=6)||!namespaceSelected(ns==6?1:0))return RISC_KEY_VALUE_AVAILABILITY_INVALID;
 const unsigned i=ns==6?1:0;*out={sizeof(*out),ns,state.availability[i],0,state.generation[i]};return RISC_KEY_VALUE_AVAILABILITY_OK;
}
bool retained(){return owner()&&selectedRetained();}
bool exitSafe(){return (!state.selected||owner())&&!state.entered&&!state.active&&!selectedRetained()&&W::exitSafe()&&(!state.prepared||clientsSafe());}
bool ready(){return owner()&&state.hydrated&&!selectedRetained();}
Timing timing(){return owner()?state.time:Timing{};}
} }
#endif
