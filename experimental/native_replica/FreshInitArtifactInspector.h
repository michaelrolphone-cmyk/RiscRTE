#pragma once
#include "CheckedFreshInitArtifacts.h"
#include "CheckedLittleFsFreshInitArtifacts.h"
namespace RiscExperimental {
// Narrow native adapter: only the two reviewed concrete inspectors can bind.
// No public callback constructor and no automatic target/profile selection.
class FreshInitArtifactInspector final {
 public:
 FreshInitArtifactInspector()=default;
 FreshInitArtifactInspector(std::nullptr_t){}
 FreshInitArtifactInspector(CheckedFreshInitArtifacts*p){bind(p);}
 FreshInitArtifactInspector(CheckedFreshInitArtifacts&p){bind(&p);}
 FreshInitArtifactInspector(CheckedLittleFsFreshInitArtifacts*p){bind(p);}
 FreshInitArtifactInspector(CheckedLittleFsFreshInitArtifacts&p){bind(&p);}
 explicit operator bool()const{return context_!=nullptr;}
 FreshInitArtifactInspector*operator->(){return this;}
 const FreshInitArtifactInspector*operator->()const{return this;}
 uint32_t namespaceId()const{return context_?namespace_(context_):0;}
 bool retained()const{return context_&&retained_(context_);}
 bool exitSafe()const{return !context_||exitSafe_(context_);}
 int inspectAbsent(){return context_?absent_(context_):int(NativeReplicaCore::Invalid);}
 int inspectAttempt(const uint8_t*p,uint32_t n,uint64_t next){return context_?attempt_(context_,p,n,next):int(NativeReplicaCore::Invalid);}
 private:
 void*context_=nullptr;uint32_t(*namespace_)(void*)=nullptr;bool(*retained_)(void*)=nullptr;bool(*exitSafe_)(void*)=nullptr;
 int(*absent_)(void*)=nullptr;int(*attempt_)(void*,const uint8_t*,uint32_t,uint64_t)=nullptr;
 template<class T>void bind(T*p){if(!p)return;context_=p;
  namespace_=[](void*p){return static_cast<T*>(p)->namespaceId();};retained_=[](void*p){return static_cast<T*>(p)->retained();};exitSafe_=[](void*p){return static_cast<T*>(p)->exitSafe();};
  absent_=[](void*p){return static_cast<T*>(p)->inspectAbsent();};attempt_=[](void*p,const uint8_t*b,uint32_t n,uint64_t next){return static_cast<T*>(p)->inspectAttempt(b,n,next);};
 }
};
}
