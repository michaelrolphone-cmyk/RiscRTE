#pragma once
#include "CheckedFreshInitArtifacts.h"
#include "CheckedLittleFsFreshInitArtifacts.h"
namespace RiscExperimental {
// Narrow native value adapter: only the two reviewed concrete inspectors bind.
// There is no callback constructor, profile inference or ownership transfer.
// Copies preserve the underlying concrete type and address, never adapter identity.
class FreshInitArtifactInspector final {
 public:
 FreshInitArtifactInspector()=default;
 FreshInitArtifactInspector(std::nullptr_t){}
 FreshInitArtifactInspector(CheckedFreshInitArtifacts*p):linux_(p){}
 FreshInitArtifactInspector(CheckedFreshInitArtifacts&p):linux_(&p){}
 FreshInitArtifactInspector(CheckedLittleFsFreshInitArtifacts*p):littlefs_(p){}
 FreshInitArtifactInspector(CheckedLittleFsFreshInitArtifacts&p):littlefs_(&p){}
 explicit operator bool()const{return linux_||littlefs_;}
 FreshInitArtifactInspector*operator->(){return this;}
 const FreshInitArtifactInspector*operator->()const{return this;}
 const void*concreteAddress()const{return linux_?static_cast<const void*>(linux_):littlefs_;}
 size_t concreteBytes()const{return linux_?sizeof(*linux_):littlefs_?sizeof(*littlefs_):0;}
 bool sameInspector(FreshInitArtifactInspector other)const{return bool(*this)&&linux_==other.linux_&&littlefs_==other.littlefs_;}
 uint32_t namespaceId()const{return linux_?linux_->namespaceId():littlefs_?littlefs_->namespaceId():0;}
 bool boundTo(uint32_t ns,const char*root)const{return linux_?linux_->boundTo(ns,root):littlefs_&&littlefs_->boundTo(ns,root);}
 bool retained()const{return linux_?linux_->retained():littlefs_&&littlefs_->retained();}
 bool exitSafe()const{return linux_?linux_->exitSafe():!littlefs_||littlefs_->exitSafe();}
 int inspectAbsent(){return linux_?linux_->inspectAbsent():littlefs_?littlefs_->inspectAbsent():int(NativeReplicaCore::Invalid);}
 int inspectAttempt(const uint8_t*p,uint32_t n,uint64_t next){return linux_?linux_->inspectAttempt(p,n,next):littlefs_?littlefs_->inspectAttempt(p,n,next):int(NativeReplicaCore::Invalid);}
 int inspectImportedAttempt(const uint8_t*p,uint32_t n,uint64_t next){return linux_?linux_->inspectImportedAttempt(p,n,next):littlefs_?littlefs_->inspectImportedAttempt(p,n,next):int(NativeReplicaCore::Invalid);}
 int inspectSdAttempt(const uint8_t*p,uint32_t n,uint64_t next){return linux_?linux_->inspectSdAttempt(p,n,next):littlefs_?littlefs_->inspectSdAttempt(p,n,next):int(NativeReplicaCore::Invalid);}
 private:
 CheckedFreshInitArtifacts*linux_=nullptr;
 CheckedLittleFsFreshInitArtifacts*littlefs_=nullptr;
};
}
