#pragma once
#include "runtime/packages/PackageIdentity.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace RiscCpu { namespace NativeSettingsLifecycle {
// Owner-only permanent copies. Capacity counts identities separately from exact
// (identity, namespace) bindings; no borrowed caller strings or heap ownership.
template<size_t BindingCapacity,size_t IdentityCapacity>
class WriterRegistry {
 static_assert(BindingCapacity>=1&&BindingCapacity<=32,"Invalid writer binding capacity");
 static_assert(IdentityCapacity>=1&&IdentityCapacity<=BindingCapacity,"Invalid writer identity capacity");
 char ids_[IdentityCapacity][96]{};
 uint8_t bindings_[BindingCapacity]{};
 uint8_t count_=0,identities_=0;
public:
 bool add(const char*id,uint32_t ns){
  if(!id||strnlen(id,96)>=96||!RuntimePackages::safeId(id)||(ns!=1&&ns!=6)||count_>=BindingCapacity)return false;
  size_t index=0;
  while(index<identities_&&std::strcmp(ids_[index],id))++index;
  if(index==identities_&&identities_>=IdentityCapacity)return false;
  const uint8_t reference=uint8_t(index*2+(ns==6?1:0));
  for(size_t i=0;i<count_;++i)if(bindings_[i]==reference)return false;
  // All refusal checks precede mutation. Existing pointers never move.
  if(index==identities_){std::strcpy(ids_[index],id);++identities_;}
  bindings_[count_++]=reference;
  return true;
 }
 size_t bindingCount()const{return count_;}
 size_t identityCount()const{return identities_;}
 const char*app(size_t i)const{return i<count_?ids_[bindings_[i]/2]:nullptr;}
 uint32_t nameSpace(size_t i)const{return i<count_?(bindings_[i]&1?6u:1u):0u;}
};
} }
