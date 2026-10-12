#include "NativeSettingsBootstrap.h"
#if RISC_NATIVE_SETTINGS_LIFECYCLE
namespace RiscCpu {
namespace {using A=RiscExperimental::NativeSdAttempt;}
NativeSettingsBootstrap::~NativeSettingsBootstrap(){if(!exitSafe())std::abort();destroy();destroyAttempt();}
bool NativeSettingsBootstrap::binds(const C&a,const Store&as,const C&b,const Store&bs,uint64_t device)const{return inputs_[0].core==&a&&inputs_[0].store==&as&&inputs_[1].core==&b&&inputs_[1].store==&bs&&device_==device;}
bool NativeSettingsBootstrap::owner()const{return services_[0]&&services_[1]&&services_[0]->owner()&&services_[1]->owner();}
bool NativeSettingsBootstrap::registerBeforeActivate(Coordinator&a,Coordinator&b){
 static_assert(Coordinator::MaxClients>=3,"Load, synchronization and bootstrap require three native clients");
 if(registered_||!device_||!exitSafe())return false;
 Coordinator*services[2]={&a,&b};
 for(unsigned i=0;i<2;++i){const auto&n=inputs_[i];if(!n.core||!n.store||n.core->namespaceId()!=(i?6u:1u)||!services[i]->owner()||!services[i]->binds(*n.core,*n.store)||!n.core->usesBackend(n.store->backend())||!n.digest.begin||!n.digest.update||!n.digest.finish||!n.allocator.allocate||!n.allocator.release||(n.sources&&!n.sources->exitSafe())||(n.legacy&&!n.legacy->exitSafe()))return false;}
 registered_=true;services_[0]=&a;services_[1]=&b;
 for(unsigned i=0;i<2;++i){const int rc=services_[i]->registerNative({this,sizeof(*this),
  [](void*p){return static_cast<NativeSettingsBootstrap*>(p)->workerStep();},
  [](void*p){return static_cast<NativeSettingsBootstrap*>(p)->active_;},
  [](void*p){auto&s=*static_cast<NativeSettingsBootstrap*>(p);return !s.active_&&!s.fault_.load(std::memory_order_acquire);},
  [](void*p){return static_cast<NativeSettingsBootstrap*>(p)->fault_.load(std::memory_order_acquire);}},&slots_[i]);if(rc)return false;}
 return true;
}
bool NativeSettingsBootstrap::retained()const{if(fault_.load(std::memory_order_acquire))return true;for(auto*s:services_)if(s&&s->retained())return true;return false;}
bool NativeSettingsBootstrap::exitSafe()const{return !active_&&!lease_.serial&&!gate_.load(std::memory_order_acquire)&&!retained()&&(!attempt_||attempt_->exitSafe());}
bool NativeSettingsBootstrap::firstAvailable(unsigned i)const{if(i>1)return false;const auto&a=inputs_[i].first;return a.otherWritersStopped&&a.entropy&&a.stagingCapacity;}
NativeSettingsBootstrap::Catalog::Guard NativeSettingsBootstrap::guard(){return {this,[](void*p){auto&s=*static_cast<NativeSettingsBootstrap*>(p);return s.services_[s.current_]->nativeOwner(s.lease_);},[](void*p){auto&s=*static_cast<NativeSettingsBootstrap*>(p);return s.services_[s.current_]->nativeWorker(s.slots_[s.current_]);}};}
NativeSettingsBootstrap::Restore::Guard NativeSettingsBootstrap::restoreGuard(){return {this,[](void*p){return static_cast<NativeSettingsBootstrap*>(p)->owner();},[](void*p){auto&s=*static_cast<NativeSettingsBootstrap*>(p);return s.services_[s.current_]->nativeWorker(s.slots_[s.current_]);},[](void*p){auto&s=*static_cast<NativeSettingsBootstrap*>(p);return s.services_[s.current_]->nativeWorker(s.slots_[s.current_]);},[](void*p){return static_cast<NativeSettingsBootstrap*>(p)->lease_.serial;}};}
void NativeSettingsBootstrap::destroy(){if(kind_==1)catalog()->~Catalog();else if(kind_==2)restore()->~Restore();else if(kind_==3)first()->~First();else if(kind_==4)native()->~NativeInitialization();kind_=0;C::wipe(storage_,sizeof storage_);}
void NativeSettingsBootstrap::destroyAttempt(){if(attempt_){if(!attempt_->exitSafe())std::abort();attempt_->~Attempt();attempt_=nullptr;C::wipe(attemptStorage_,sizeof attemptStorage_);}}
NativeSettingsBootstrap::Progress NativeSettingsBootstrap::retain(){fault_.store(true,std::memory_order_release);results_[current_]=Result::Retained;errors_[current_]=C::Retained;if(lease_.serial)(void)services_[current_]->releaseNative(lease_);return Progress::Retained;}
void NativeSettingsBootstrap::finish(int rc,bool cancelled){if(rc==C::Retained||rc==C::Uncertain){(void)retain();return;}errors_[current_]=rc;results_[current_]=cancelled||rc==C::Stale?Result::Cancelled:!rc?Result::Synchronized:rc==C::Conflict||rc==C::Missing?Result::Conflict:rc==C::Busy||rc==C::Io?Result::Deferred:Result::Failed;phase_=Phase::Release;}
NativeSettingsBootstrap::Progress NativeSettingsBootstrap::start(RiscBoot::Runtime&r,Port&p,unsigned i,bool first){
 if(retained())return Progress::Retained;
 if(!registered_||!owner()||i>1)return Progress::Failed;
 if(!exitSafe()||!services_[0]->exitSafe()||!services_[1]->exitSafe())return Progress::Deferred;
 // acquireNative publishes the holder before its worker can enter this client.
 // Hold the same reconstruction gate across that publication and every phase/
 // current-object write. Check exitSafe before locking: it includes this gate.
 Lock lock(gate_);if(!lock)return Progress::Deferred;
 runtime_=&r;port_=&p;current_=i;firstMode_=first;cancelled_=false;return startNamespace();
}
NativeSettingsBootstrap::Baseline::Guard NativeSettingsBootstrap::nativeGuard(){return {this,
 [](void*p){auto&s=*static_cast<NativeSettingsBootstrap*>(p);return s.services_[s.current_]->nativeWorker(s.slots_[s.current_]);},
 [](void*p){return static_cast<NativeSettingsBootstrap*>(p)->lease_.serial;}};}
int NativeSettingsBootstrap::inspectNative(){
 auto&n=inputs_[current_];const auto backend=n.store->backend();
 // Receipt dispatch is purpose-specific. Missing from one reader only permits
 // trying another exact reader; an I/O or malformed record never means absence.
 C::FreshInitReceipt fresh;int rc=backend.readInitialization(backend.context,&fresh);
 if(!rc){nativeMode_=NativeMode::FreshResume;return C::Ok;}if(rc!=C::Missing)return rc;
 C::ImportInitReceipt imported;rc=backend.readImportedInitialization(backend.context,&imported);
 if(!rc){if(!n.legacy)return C::Conflict;nativeMode_=NativeMode::ImportResume;return C::Ok;}if(rc!=C::Missing)return rc;
 C::SdInitReceipt sd;rc=backend.readSdInitialization(backend.context,&sd);
 if(!rc){nativeMode_=NativeMode::SdResume;return C::Ok;}if(rc!=C::Missing)return rc;
 if(n.legacy){rc=n.legacy->inventory(&nativeWitness_);if(!rc){nativeMode_=NativeMode::Import;return C::Ok;}if(rc!=C::Missing)return rc;}
 const auto source=n.sources->sources();rc=source.freshInventory(source.context,n.core->namespaceId(),&nativeWitness_);
 if(!rc)nativeMode_=NativeMode::Fresh;
 return rc;
}
void NativeSettingsBootstrap::queueNative(){
 destroy();auto&n=inputs_[current_];const bool imported=nativeMode_==NativeMode::Import||nativeMode_==NativeMode::ImportResume;const auto&d=n.digest;
 new(storage_)NativeInitialization(this,*n.core,nativeGuard(),{d.context,d.begin,d.update,d.finish},imported?n.legacy->sources():n.sources->sources(),n.allocator);kind_=4;phase_=Phase::NativeInitialize;
}
int NativeSettingsBootstrap::initializeNative(){
 auto&factory=native()->factory;auto&admission=native()->admission;auto&core=*inputs_[current_].core;int rc=C::Invalid;
 switch(nativeMode_){
 case NativeMode::Fresh:rc=factory.admitFreshEmpty(&admission);break;
 case NativeMode::FreshResume:rc=factory.admitFreshResume(&admission);break;
 case NativeMode::Import:{Baseline::Selection selected{nativeWitness_.kind,nativeWitness_.revision,{}};std::memcpy(selected.fingerprint,nativeWitness_.fingerprint,32);rc=factory.admitImported(selected,&admission);C::wipe(&selected,sizeof selected);break;}
 case NativeMode::ImportResume:rc=factory.admitImportedResume(&admission);break;
 default:return C::Invalid;
 }
 if(rc)return rc;
 return nativeMode_==NativeMode::Import||nativeMode_==NativeMode::ImportResume?core.initializeImportedBaseline(admission):core.initializeFreshEmpty(admission);
}
NativeSettingsBootstrap::Progress NativeSettingsBootstrap::startNamespace(){
 destroy();destroyAttempt();nativeMode_=NativeMode::None;C::wipe(&nativeWitness_,sizeof nativeWitness_);nativeResult_=C::Invalid;
 const int rc=services_[current_]->acquireNative(slots_[current_],&lease_);
 if(rc){errors_[current_]=rc;results_[current_]=rc==C::Retained?Result::Retained:Result::Deferred;remaining_=0;return rc==C::Retained?retain():Progress::Deferred;}
 active_=true;results_[current_]=Result::Running;errors_[current_]=C::Ok;
 if(firstMode_)return startAttempt();
 phase_=Phase::NativeInspect;return Progress::Pending;
}
NativeSettingsBootstrap::Progress NativeSettingsBootstrap::startAttempt(){
 destroyAttempt();attempt_=new(attemptStorage_)Attempt(*runtime_,*port_,inputs_[current_].core->namespaceId(),device_,A::Profile::CheckedWarm);
 const int acquired=attempt_->acquire();
 if(acquired){
  if(acquired==RISC_STORAGE_ASYNC_RETAINED||!attempt_->exitSafe())return retain();
  destroyAttempt();
  // Complete legacy settings are a usable native source in their own right.
  // Failed SD admission is NOT an absence witness and cannot create FreshEmpty.
  if(!firstMode_&&nativeMode_==NativeMode::Import)queueNative();
  else finish(acquired==RISC_STORAGE_ASYNC_BUSY?C::Busy:C::Io);
  return Progress::Pending;
 }
 phase_=Phase::Refresh;return Progress::Pending;
}
NativeSettingsBootstrap::Progress NativeSettingsBootstrap::beginFirst(RiscBoot::Runtime&r,Port&p,unsigned i){if(!owner()||!exitSafe()||!firstAvailable(i))return retained()?Progress::Retained:Progress::Deferred;remaining_=0;return start(r,p,i,true);}
NativeSettingsBootstrap::Progress NativeSettingsBootstrap::beginPreApp(RiscBoot::Runtime&r,Port&p,int one,int six){
 if(!owner()||!exitSafe()||!services_[0]->exitSafe()||!services_[1]->exitSafe())return retained()?Progress::Retained:Progress::Deferred;
 const int codes[2]={one,six};unsigned mask=0;
 for(unsigned i=0;i<2;++i){if(codes[i]==C::Ok){if(!inputs_[i].core->ready())return Progress::Failed;continue;}if((codes[i]!=C::Missing&&codes[i]!=C::Initializing)||inputs_[i].core->ready())return Progress::Failed;if(!inputs_[i].sources)return Progress::Deferred;mask|=1u<<i;}
 if(!mask)return Progress::Idle;
 const unsigned first=(mask&1)?0:1;remaining_=mask&~(1u<<first);return start(r,p,first,false);
}
void NativeSettingsBootstrap::cancel(){if(owner()&&active_&&!retained())cancelled_=true;}
bool NativeSettingsBootstrap::workerStep(){
 Lock lock(gate_);if(!lock||retained())return false;
 if(!services_[current_]->nativeWorker(slots_[current_]))return false;
 bool ran=false;
 if(phase_==Phase::NativeInspect){nativeResult_=cancelled_?int(C::Stale):inspectNative();phase_=Phase::NativeInspectResult;ran=true;}
 else if(phase_==Phase::NativeInitialize){nativeResult_=cancelled_?int(C::Stale):initializeNative();phase_=Phase::NativeInitializeResult;ran=true;}
 else if(phase_==Phase::CatalogPrepare)ran=catalog()->preparationWorkerStep();
 else if(phase_==Phase::RestoreCollect)ran=restore()->workerStep();
 else if(phase_==Phase::FirstPrepare)ran=first()->preparationWorkerStep();
 else if(phase_==Phase::FirstArm)ran=first()->nativeArmWorkerStep();
 else if(phase_==Phase::FirstAck)ran=first()->nativeCompletionWorkerStep();
 if(ran){inputs_[current_].store->finishOperation();if(!inputs_[current_].store->exitSafe()||(inputs_[current_].sources&&!inputs_[current_].sources->exitSafe())||(inputs_[current_].legacy&&!inputs_[current_].legacy->exitSafe())||(kind_==4&&!native()->factory.exitSafe()))fault_.store(true,std::memory_order_release);}
 return ran;
}
NativeSettingsBootstrap::Progress NativeSettingsBootstrap::poll(){
 if(!owner())return Progress::Failed;
 Lock lock(gate_);if(!lock)return Progress::Pending;
 if(retained())return retain();
 if(!active_)return Progress::Idle;
 auto&n=inputs_[current_];auto*a=attempt_?attempt_->attempt():nullptr;
 if(cancelled_){if(kind_==1)catalog()->cancel();else if(kind_==2)restore()->cancel();else if(kind_==3)first()->cancel();else if(phase_==Phase::Refresh||phase_==Phase::RefreshResult){a->cancel();phase_=Phase::Close;}}
 switch(phase_){
 case Phase::NativeInspect:case Phase::NativeInitialize:break; // worker-owned
 case Phase::NativeInspectResult:
  if(nativeResult_||cancelled_){finish(nativeResult_?nativeResult_:int(C::Stale),cancelled_);break;}
  if(nativeMode_==NativeMode::FreshResume||nativeMode_==NativeMode::ImportResume)queueNative();
  else return startAttempt();
  break;
 case Phase::NativeInitializeResult:finish(nativeResult_,cancelled_&&nativeResult_==C::Stale);break;
 case Phase::Refresh:{const int rc=a->refresh();if(rc==RISC_STORAGE_ASYNC_BUSY)break;if(rc==RISC_STORAGE_ASYNC_RETAINED)return retain();phase_=rc?Phase::Close:Phase::RefreshResult;break;}
 case Phase::RefreshResult:{auto*r=new(storage_)risc_storage_async_result_v1{};r->status=INT32_MIN;const int rc=a->collect(r);const bool waiting=rc==RISC_STORAGE_ASYNC_PENDING||(rc==RISC_STORAGE_ASYNC_BUSY&&r->status!=RISC_STORAGE_ASYNC_BUSY);C::wipe(storage_,sizeof storage_);if(waiting)break;if(rc==RISC_STORAGE_ASYNC_RETAINED)return retain();phase_=Phase::Close;break;}
 case Phase::Close:{if(a->hasSession()){const int rc=a->close();if(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY)break;if(rc==RISC_STORAGE_ASYNC_RETAINED)return retain();}
  if(!lease_.serial){phase_=Phase::Release;break;}
  if(cancelled_){finish(C::Stale,true);break;}
  if(a->state()!=A::State::ReadyClosed){
   // IO/UNAVAILABLE does not distinguish physical absence from transport or
   // media failure. Only independent complete legacy data can continue here.
   if(!firstMode_&&nativeMode_==NativeMode::Import)queueNative();
   else finish(C::Io);
   break;
  }
  if(firstMode_){new(storage_)First(*n.core,n.digest,guard(),n.first);kind_=3;const int rc=first()->prepareNative(*a);if(rc)finish(rc);else phase_=Phase::FirstPrepare;}
  else {new(storage_)Catalog(*n.core,n.digest,guard(),n.allocator);kind_=1;const int rc=catalog()->prepareRestoreNative(*a);if(rc)finish(rc);else phase_=Phase::CatalogPrepare;}
  break;}
 case Phase::CatalogPrepare:{const int rc=catalog()->collectPreparation();if(rc==C::Busy&&catalog()->state()==Catalog::State::Running)break;if(rc)finish(rc);else phase_=Phase::CatalogRead;break;}
 case Phase::CatalogRead:{catalog()->step();const auto state=catalog()->state();if(state==Catalog::State::Running)break;if(state==Catalog::State::Retained)return retain();if(state!=Catalog::State::Verified){finish(catalog()->error(),state==Catalog::State::Cancelled);break;}
  if(catalog()->vacant()||nativeMode_==NativeMode::Import){
   Catalog::Report report;const int rc=catalog()->take(&report);if(rc==C::Busy)break;if(rc){finish(rc);break;}
   if(!report.vacant()||nativeMode_==NativeMode::SdResume){destroy();finish(C::Conflict);break;}
   // Report vacancy is historical SD evidence, not an SD write permit. Native
   // admission rechecks every source under the still-held global native lease.
   // Keep the closed SD attempt fenced until native publication completes;
   // another ordinary SD writer must not enter after this vacancy observation.
   queueNative();break;
  }
  RiscExperimental::CheckedSdReplicaReader::Candidate candidate;const int rc=catalog()->takeRestoreCandidate(&candidate);if(rc){finish(rc);break;}
  destroy();const auto&d=n.digest;new(storage_)Restore(*n.core,*n.store,*n.sources,{d.context,d.begin,d.update,d.finish},restoreGuard());kind_=2;
  const int begin=restore()->begin(*a,candidate);if(begin)finish(begin);else phase_=Phase::RestoreCollect;break;}
 case Phase::RestoreCollect:{RiscExperimental::NativeSdCompletion::Result result{};const int rc=restore()->collect(&result);if(rc==C::Busy)break;finish(rc?rc:result.status,cancelled_&&(rc==C::Stale||result.status==C::Stale));break;}
 case Phase::FirstPrepare:{const int rc=first()->collectPreparation();if(rc==C::Busy&&first()->state()==First::State::Running)break;if(rc)finish(rc);else phase_=Phase::FirstStep;break;}
 case Phase::FirstStep:{first()->step();const auto s=first()->state();if(s==First::State::Running)break;if(s==First::State::Retained)return retain();
  if(s==First::State::Admitted){const int rc=first()->issueNativeArm();if(rc==C::Busy)break;if(rc)finish(rc);else phase_=Phase::FirstArm;}
  else if(s==First::State::Verified){const int rc=first()->issueNativeCompletion();if(rc==C::Busy)break;if(rc)finish(rc);else phase_=Phase::FirstAck;}
  else finish(first()->error(),s==First::State::Cancelled);
  break;}
 case Phase::FirstArm:{const int rc=first()->collectNativeArm();if(rc==C::Busy)break;if(rc||first()->state()==First::State::Cancelled)finish(rc,first()->state()==First::State::Cancelled);else phase_=Phase::FirstStep;break;}
 case Phase::FirstAck:{const int rc=first()->collectNativeCompletion();if(rc==C::Busy)break;finish(rc,first()->state()==First::State::Cancelled);break;}
 case Phase::Release:{if((kind_==1&&!catalog()->exitSafe())||(kind_==2&&!restore()->exitSafe())||(kind_==3&&!first()->exitSafe())||(kind_==4&&!native()->factory.exitSafe())||(a&&(a->hasSession()||a->receiptPinned())))return retain();
  if(attempt_&&attempt_->release())return retain();
  active_=false;
  if(lease_.serial){const int rc=services_[current_]->releaseNative(lease_);if(rc){active_=true;return retain();}lease_={};}
  destroy();destroyAttempt();C::wipe(&nativeWitness_,sizeof nativeWitness_);nativeMode_=NativeMode::None;phase_=Phase::Idle;
  if(!cancelled_&&results_[current_]==Result::Synchronized&&remaining_){current_=(remaining_&1)?0:1;remaining_&=~(1u<<current_);return startNamespace();}
  remaining_=0;return results_[current_]==Result::Synchronized||results_[current_]==Result::Cancelled?Progress::Idle:results_[current_]==Result::Deferred?Progress::Deferred:Progress::Failed;}
 default:return retain();
 }
 return retained()?Progress::Retained:Progress::Pending;
}
}
#endif
