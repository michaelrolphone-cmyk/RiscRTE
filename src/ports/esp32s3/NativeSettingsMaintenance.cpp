#include "NativeSettingsMaintenance.h"
#if RISC_NATIVE_SETTINGS_LIFECYCLE
#include "NativeSettingsBootstrap.h"
#include <cstring>
#include <cstdlib>
namespace RiscCpu {
namespace { using A=RiscExperimental::NativeSdAttempt; }
NativeSettingsMaintenance::NativeSettingsMaintenance(Namespace preferences,Namespace wifi,uint64_t device,
 const NativeSettingsLifecycle::Writer*writers,size_t writerCount,NativeSettingsBootstrap*bootstrap):device_(device),bootstrap_(bootstrap){
 const Namespace inputs[2]={preferences,wifi};
 for(unsigned i=0;i<2;++i){auto&b=bindings_[i];b.input=inputs[i];b.hasIncarnation=inputs[i].initialIncarnation!=nullptr;
  if(b.hasIncarnation)std::memcpy(b.incarnation,inputs[i].initialIncarnation,16);
  b.input.initialIncarnation=nullptr;
 }
 admission_={{preferences.core,preferences.store},{wifi.core,wifi.store},writers,writerCount,this,sizeof(*this),
  [](void*p,Coordinator&a,Coordinator&b){return static_cast<NativeSettingsMaintenance*>(p)->registerBeforeActivate(a,b);},
  [](void*p,RiscBoot::Runtime&r,Port&port){return static_cast<NativeSettingsMaintenance*>(p)->begin(r,port);},
  [](void*p){return static_cast<NativeSettingsMaintenance*>(p)->poll();},
  [](void*p){static_cast<NativeSettingsMaintenance*>(p)->cancel();},
  [](void*p){return static_cast<NativeSettingsMaintenance*>(p)->exitSafe();},
  [](void*p){return static_cast<NativeSettingsMaintenance*>(p)->retained();}};
 if(bootstrap_)admission_.beginPreApp=[](void*p,RiscBoot::Runtime&r,Port&port,int one,int six){auto&s=*static_cast<NativeSettingsMaintenance*>(p);
  const auto rc=s.bootstrap_->beginPreApp(r,port,one,six);s.preAppAuxiliary_=true;s.auxiliary_=rc==Progress::Pending||!s.bootstrap_->exitSafe();s.active_=s.auxiliary_;return rc;};
}
NativeSettingsMaintenance::~NativeSettingsMaintenance(){
 if(!exitSafe())std::abort();
 destroyAttempt();
 for(auto&b:bindings_){if(b.sync)b.sync->~Sync();C::wipe(b.incarnation,sizeof b.incarnation);}
}
bool NativeSettingsMaintenance::registerBeforeActivate(Coordinator&preferences,Coordinator&wifi){
 static_assert(Coordinator::MaxClients>=2,"Lifecycle load and maintenance each require one permanent native client");
 if(registered_||!device_||!exitSafe())return false;
 Coordinator*services[2]={&preferences,&wifi};
 for(unsigned i=0;i<2;++i){const auto&b=bindings_[i];const auto&n=b.input;
  if(!n.core||!n.store||!services[i]->owner()||n.core->namespaceId()!=(i?6u:1u)||n.store->namespaceId()!=n.core->namespaceId()||
     !services[i]->binds(*n.core,*n.store)||!n.core->usesBackend(n.store->backend())||
     !n.digest.begin||!n.digest.update||!n.digest.finish||!n.allocator.allocate||!n.allocator.release)return false;
 }
 if(bootstrap_&&!bootstrap_->binds(*bindings_[0].input.core,*bindings_[0].input.store,*bindings_[1].input.core,*bindings_[1].input.store,device_))return false;
 // Partial registration is permanent too. There is no rollback destruction or
 // later replacement of a scheduler-reachable callback after a failed prepare.
 registered_=true;
 for(unsigned i=0;i<2;++i){auto&b=bindings_[i];b.service=services[i];b.sync=new(b.storage)Sync(*b.input.core,*b.input.store,*b.service,b.input.digest,b.input.allocator);
  if(b.sync->registerBeforeActivate()!=C::Ok)return false;
 }
 return !bootstrap_||bootstrap_->registerBeforeActivate(preferences,wifi);
}
bool NativeSettingsMaintenance::retained()const{
 if(retained_||(bootstrap_&&bootstrap_->retained()))return true;
 for(const auto&b:bindings_)if((b.service&&b.service->retained())||(b.sync&&b.sync->state()==Sync::State::Retained))return true;
 return false;
}
bool NativeSettingsMaintenance::exitSafe()const{
 if(active_||retained()||(attempt_&&!attempt_->exitSafe())||(bootstrap_&&!bootstrap_->exitSafe()))return false;
 for(const auto&b:bindings_)if(b.sync&&!b.sync->exitSafe())return false;
 return true;
}
NativeSettingsMaintenance::Sync::State NativeSettingsMaintenance::result(uint32_t ns)const{return ns==1?bindings_[0].result:ns==6?bindings_[1].result:Sync::State::Failed;}
int NativeSettingsMaintenance::error(uint32_t ns)const{return ns==1?bindings_[0].error:ns==6?bindings_[1].error:C::Invalid;}
void NativeSettingsMaintenance::destroyAttempt(){if(attempt_){if(!attempt_->exitSafe())std::abort();attempt_->~Attempt();attempt_=nullptr;C::wipe(attemptStorage_,sizeof attemptStorage_);}}
NativeSettingsMaintenance::Progress NativeSettingsMaintenance::retain(){retained_=true;bindings_[current_].result=Sync::State::Retained;bindings_[current_].error=C::Retained;return Progress::Retained;}
NativeSettingsMaintenance::Progress NativeSettingsMaintenance::finished(){
 if(!attempt_||!attempt_->exitSafe())return retain();
 destroyAttempt();active_=refusing_=false;
 const auto s=bindings_[current_].result;
 return continueSweep(s==Sync::State::Deferred?Progress::Deferred:s==Sync::State::Conflict||s==Sync::State::Failed?Progress::Failed:Progress::Idle);
}
NativeSettingsMaintenance::Progress NativeSettingsMaintenance::begin(RiscBoot::Runtime&r,Port&p){
 if(retained())return Progress::Retained;
 if(!registered_||!bindings_[0].service||!bindings_[1].service||!bindings_[0].service->owner()||!bindings_[1].service->owner())return Progress::Failed;
 if(active_)return Progress::Pending;
 if(!exitSafe()||!bindings_[0].service->exitSafe()||!bindings_[1].service->exitSafe())return Progress::Deferred;
 // One bounded sweep: each dirty namespace at most once, never retry a refused
 // namespace within this checkpoint. A conflict cannot starve its sibling.
 runtime_=&r;port_=&p;remaining_=0;sweepResult_=Progress::Idle;
 for(unsigned n=0;n<2;++n){const unsigned i=(next_+n)%2;const auto&b=bindings_[i];
  if(b.input.core->ready()&&(b.input.core->pendingCount()||b.input.core->exportPrepared()||(!b.input.core->identity().known&&bootstrap_&&bootstrap_->firstAvailable(i))))remaining_|=1u<<i;}
 return continueSweep(Progress::Idle);
}
NativeSettingsMaintenance::Progress NativeSettingsMaintenance::continueSweep(Progress completed){
 if(completed==Progress::Retained)return retain();
 if(completed==Progress::Failed||(completed==Progress::Deferred&&sweepResult_!=Progress::Failed))sweepResult_=completed;
 // remaining_ has only two bits and each is consumed before any attempt.
 while(remaining_){const unsigned selected=(remaining_&(1u<<next_))?next_:1-next_;remaining_&=~(1u<<selected);
  const auto rc=startOne(selected);if(rc==Progress::Pending||rc==Progress::Retained)return rc;
  if(rc==Progress::Failed||(rc==Progress::Deferred&&sweepResult_!=Progress::Failed))sweepResult_=rc;
 }
 return sweepResult_;
}
NativeSettingsMaintenance::Progress NativeSettingsMaintenance::startOne(unsigned selected){
 auto&r=*runtime_;auto&p=*port_;
 current_=selected;next_=(selected+1)%2;auto&b=bindings_[current_];
 bool firstCreation=!b.input.core->identity().known;
 if(b.input.core->exportPrepared()){C::ExportMarker marker;if(!b.input.core->preparedExport(&marker)&&marker.firstCreation())firstCreation=true;C::wipe(&marker,sizeof marker);}
 if(firstCreation){
  if(bootstrap_&&bootstrap_->firstAvailable(current_)){const auto rc=bootstrap_->beginFirst(r,p,current_);preAppAuxiliary_=false;auxiliary_=rc==Progress::Pending||!bootstrap_->exitSafe();active_=auxiliary_;
   if(!auxiliary_){b.result=bootstrap_->result(current_);b.error=bootstrap_->error(current_);}return rc;}
  b.result=Sync::State::Conflict;b.error=C::Conflict;return Progress::Failed;
 }
 // A boot-supplied seed cannot replace an already persisted incarnation.
 if(!b.input.core->exportIncarnationRequired()){C::wipe(b.incarnation,sizeof b.incarnation);b.hasIncarnation=false;}
 destroyAttempt();attempt_=new(attemptStorage_)Attempt(r,p,b.input.core->namespaceId(),device_,A::Profile::CheckedWarm);
 const int acquired=attempt_->acquire();
 if(acquired){
  b.result=Sync::State::Deferred;b.error=acquired;
  if(acquired==RISC_STORAGE_ASYNC_RETAINED||!attempt_->exitSafe())return retain();
  destroyAttempt();return Progress::Deferred;
 }
 active_=true;
 const int rc=b.sync->begin(*attempt_,b.hasIncarnation?b.incarnation:nullptr);
 if(!rc)return Progress::Pending;
 b.error=rc;b.result=rc==C::Busy?Sync::State::Deferred:Sync::State::Failed;
 if(rc==C::Retained)return retain();
 // Even a clean namespace-lease refusal owns an SD admission session. Drain it
 // before returning to Files/USB; never destroy a one-use live wrapper.
 refusing_=true;attempt_->attempt()->cancel();return Progress::Pending;
}
NativeSettingsMaintenance::Progress NativeSettingsMaintenance::poll(){
 if(retained())return Progress::Retained;
 if(!registered_||!bindings_[current_].service||!bindings_[current_].service->owner())return Progress::Failed;
 if(!active_)return Progress::Idle;
 if(auxiliary_){const auto rc=bootstrap_->poll();if(rc==Progress::Retained)return retain();if(rc==Progress::Pending||!bootstrap_->exitSafe())return Progress::Pending;
  for(unsigned i=0;i<2;++i){auto&b=bindings_[i];if((preAppAuxiliary_||i==current_)&&bootstrap_->result(i)!=Sync::State::Idle){b.result=bootstrap_->result(i);b.error=bootstrap_->error(i);}if(b.input.core->ready()&&!b.input.core->exportIncarnationRequired()){C::wipe(b.incarnation,sizeof b.incarnation);b.hasIncarnation=false;}}active_=auxiliary_=false;return preAppAuxiliary_?rc:continueSweep(rc);}
 auto&b=bindings_[current_];
 if(refusing_){auto*a=attempt_->attempt();
  if(a->hasSession()){const int rc=a->close();if(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY)return Progress::Pending;if(rc==RISC_STORAGE_ASYNC_RETAINED)return retain();}
  if(attempt_->release())return retain();
  return finished();
 }
 b.sync->poll();
 if(b.sync->state()==Sync::State::Retained)return retain();
 if(!b.sync->exitSafe())return Progress::Pending;
 b.result=b.sync->state();b.error=b.sync->error();
 if(!b.input.core->exportIncarnationRequired()){C::wipe(b.incarnation,sizeof b.incarnation);b.hasIncarnation=false;}
 return finished();
}
void NativeSettingsMaintenance::cancel(){
 if(!active_||retained()||!bindings_[current_].service->owner())return;
 remaining_=0; // accepted work still drains; unstarted siblings stay pending
 if(auxiliary_)bootstrap_->cancel();else if(refusing_)attempt_->attempt()->cancel();else bindings_[current_].sync->cancel();
}
}
#endif
