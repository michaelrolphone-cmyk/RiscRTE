#pragma once
#include "NativeSdAttempt.h"
#include "NativeRuntimeStoragePin.h"
#include <new>
#include <cstdlib>
namespace RiscExperimental {
// Native owner lifetime wrapper. All interfaces come from the held Runtime pin;
// its caller supplies no volume pointer, graph grant, raw scope or task callback.
// Fresh-only initially; checked warm profile is a separately versioned gate.
class NativeSdPortAttempt final {
 public:
 NativeSdPortAttempt(RiscBoot::Runtime&r,RiscCpu::Port&p,uint32_t ns,uint64_t device)
  :pin_(r,p,device),port_(p),ns_(ns){}
 ~NativeSdPortAttempt(){if(!exitSafe())std::abort();if(attempt_)attempt_->~NativeSdAttempt();}
 NativeSdPortAttempt(const NativeSdPortAttempt&)=delete;NativeSdPortAttempt&operator=(const NativeSdPortAttempt&)=delete;
 int acquire(){
  if(attempt_||lease_||!ns_||ns_>INT32_MAX||!pin_.owner())return RISC_STORAGE_ASYNC_INVALID;
  int rc=pin_.acquire();if(rc)return rc;
  const auto*volume=pin_.volume();const uint64_t grant=pin_.token();
  const RiscCpu::NativeStorageCustody callbacks{this,
   [](void*p,uint64_t device){auto&s=*static_cast<NativeSdPortAttempt*>(p);return s.attempt_&&s.attempt_->fenced()&&s.attempt_->syncAdmission(device);},
   [](void*p,uint64_t scope,const void*q,uint32_t n){auto&s=*static_cast<NativeSdPortAttempt*>(p);return s.attempt_&&s.attempt_->rawSubmit(scope,q,n);},
   [](void*p,uint64_t scope,int32_t status,uint64_t token){auto&s=*static_cast<NativeSdPortAttempt*>(p);if(s.attempt_)s.attempt_->submitted(scope,status,token);},
   [](void*p,uint64_t scope,uint64_t token){auto&s=*static_cast<NativeSdPortAttempt*>(p);return s.attempt_&&s.attempt_->rawCollect(scope,token);},
   [](void*p,uint64_t scope,uint64_t token){auto&s=*static_cast<NativeSdPortAttempt*>(p);return s.attempt_&&s.attempt_->rawCancel(scope,token);},
   [](void*p){auto&s=*static_cast<NativeSdPortAttempt*>(p);return !s.attempt_||s.attempt_->exitSafe();}};
  if(!port_.nativeStorageAttach(pin_.device(),callbacks,&lease_,&scope_)){rc=pin_.release();return rc?rc:RISC_STORAGE_ASYNC_BUSY;}
  attempt_=new(storage_)NativeSdAttempt({ns_,pin_.device(),grant,scope_,volume},{this,
   [](void*p){return static_cast<NativeSdPortAttempt*>(p)->pin_.owner();},
   [](void*p,const NativeSdAttempt::Binding&b){auto&s=*static_cast<NativeSdPortAttempt*>(p);return s.matches(b)&&s.port_.nativeStorageFresh(s.lease_);},
   [](void*p){auto&s=*static_cast<NativeSdPortAttempt*>(p);return s.port_.nativeStorageIdle(s.lease_);},
   [](void*p,const NativeSdAttempt::Binding&b){return static_cast<NativeSdPortAttempt*>(p)->matches(b);},
   [](void*,bool){}, // Port attachment spans this complete inner-fence lifetime.
   [](void*p,uint64_t*epoch){auto&s=*static_cast<NativeSdPortAttempt*>(p);using S=RiscCpu::Port::NativeStorageState;const S state=s.port_.nativeStorageSnapshot(s.lease_,epoch);return state==S::Healthy?NativeSdAttempt::Scope::Healthy:state==S::Unavailable?NativeSdAttempt::Scope::Unavailable:NativeSdAttempt::Scope::Retained;}});
  rc=attempt_->acquire(volume);
  if(rc&&attempt_->exitSafe()){if(!port_.nativeStorageDetach(lease_))return RISC_STORAGE_ASYNC_RETAINED;lease_=0;const int released=pin_.release();if(released)return released;}
  return rc;
 }
 NativeSdAttempt*attempt(){return attempt_;}
 const NativeSdAttempt*attempt()const{return attempt_;}
 bool exitSafe()const{return !lease_&&pin_.exitSafe()&&(!attempt_||attempt_->exitSafe());}
 int release(){
  if(!attempt_||!lease_||!pin_.owner())return RISC_STORAGE_ASYNC_INVALID;
  const int rc=attempt_->release();if(rc)return rc;
  if(!port_.nativeStorageDetach(lease_))return RISC_STORAGE_ASYNC_RETAINED;
  lease_=0;return pin_.release();
 }
 private:
 NativeRuntimeStoragePin pin_;RiscCpu::Port&port_;uint32_t ns_;uint64_t lease_=0,scope_=0;
 alignas(NativeSdAttempt) unsigned char storage_[sizeof(NativeSdAttempt)]{};NativeSdAttempt*attempt_=nullptr;
 bool matches(const NativeSdAttempt::Binding&b)const{return pin_.held()&&port_.nativeStorageHeld(lease_)&&b.namespaceId==ns_&&b.device==pin_.device()&&b.grant==pin_.token()&&b.scope==scope_&&b.volume==pin_.volume();}
};
}
