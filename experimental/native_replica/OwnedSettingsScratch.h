#pragma once
#include "NativeReplicaCore.h"
#include <new>
#include <type_traits>
namespace RiscExperimental {
// Owned copied bytes only. Atomics, task state and lease controls must never be
// placed here. The selected native allocator is checked PSRAM-only; ordinary
// disconnected tests retain the allocator explicitly supplied to their client.
// Allocation happens before the client can issue storage mutations. Failure
// does not fall back to another memory class. Destruction follows the existing
// consumer lifetime/retention contract and wipes every byte before release.
template<class T> class OwnedSettingsScratch final {
 static_assert(std::is_trivially_destructible<T>::value,"Scratch must contain only copied values");
 using C=NativeReplicaCore;T*value_=nullptr;C::Allocator allocator_{};
 public:
 OwnedSettingsScratch()=default;
 OwnedSettingsScratch(const OwnedSettingsScratch&)=delete;
 OwnedSettingsScratch&operator=(const OwnedSettingsScratch&)=delete;
 ~OwnedSettingsScratch(){reset();}
 void reset(){if(value_){wipe();allocator_.release(allocator_.context,value_);value_=nullptr;allocator_={};}}
 bool ensure(C::Allocator a){
  if(value_)return true;
  if(!a.allocate||!a.release)return false;
  void*p=a.allocate(a.context,sizeof(T));if(!p)return false;
  if(uintptr_t(p)%alignof(T)){a.release(a.context,p);return false;}
  allocator_=a;value_=new(p)T{};return true;
 }
 bool empty()const{return value_==nullptr;}
 T*operator->(){return value_;}const T*operator->()const{return value_;}
 T&get(){return *value_;}const T&get()const{return *value_;}
 void wipe(){if(value_)C::wipe(value_,sizeof(T));}
 template<class M>void wipe(M T::*member){if(value_)C::wipe(&(value_->*member),sizeof(M));}
};
}
