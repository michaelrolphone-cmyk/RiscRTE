#pragma once
#include "AtomicSettingsEndpoint.h"
#include "../../experimental/native_replica/NativeAtomicKeyValue.h"
namespace RiscBoot {
inline AtomicSettingsEndpoint nativeAtomicSettings(RiscExperimental::NativeAtomicKeyValue& native){
  using Native=RiscExperimental::NativeAtomicKeyValue;
  static const AtomicSettingsEndpoint::Operations ops{
    [](void*c){return static_cast<Native*>(c)->namespaceId();},
    [](void*c){return static_cast<Native*>(c)->exitSafe();},
    [](void*c){return static_cast<Native*>(c)->retained();},
    [](void*c,risc_key_value_atomic_state_v1*out)->int32_t{return static_cast<Native*>(c)->state(out);},
    [](void*c,uint64_t rev,const char*k,void*out,uint32_t cap,uint32_t*n)->int32_t{return static_cast<Native*>(c)->get(rev,k,out,cap,n);},
    [](void*c,uint64_t rev,const risc_key_value_atomic_mutation_v1*m,uint32_t n,const void*i,uint32_t size,uint64_t*t)->int32_t{return static_cast<Native*>(c)->submit(rev,m,n,i,size,t);},
    [](void*c,uint64_t t,risc_key_value_atomic_receipt_v1*out)->int32_t{return static_cast<Native*>(c)->collect(t,out);},
    [](void*c,uint64_t t)->int32_t{return static_cast<Native*>(c)->cancel(t);}};
  return {&native,&ops};
}
}
