#pragma once
#include "SettingsService.h"
#include "KeyValueBackend.h"
#include "MetadataArray.h"
#include <cstdio>
namespace RiscBoot {
// Compatibility composition for existing native writers. Concrete checkpoint
// selection, state and allocation belong here, never in Runtime policy code.
class LegacySettingsSession final : public SettingsSession {
 public:
  explicit LegacySettingsSession(const KeyValueBackend& backend):backend_(backend){}
  uint32_t maxBlobSize() const override {return backend_.get&&backend_.put?backend_.maxBlobSize:0;}
  bool configure(uint32_t first,uint32_t count,bool checkpoints,const uint32_t* shared,uint32_t sharedCount) override {
    if(configured_ || sharedCount>128 || (sharedCount&&!shared))return false;
    configured_=true;
    if(!checkpoints)return !sharedCount;
    if(!first||!count||count>128||!backend_.journalRead||!backend_.journalWrite||!backend_.legacyNamespace)return false;
    if(sharedCount&&!backend_.scalableSettings)return false;
    if(backend_.scalableSettings){if(!scalable_.create(count))return false;}
    else {installed_=metadataArray<RiscStorage::SdFirstKeyValue::State>(count);workspace_=metadataArray<RiscStorage::SdFirstKeyValue::Workspace>(1);if(!installed_||!workspace_)return false;}
    if(sharedCount){
      sharedNamespaces_=metadataArray<uint32_t>(sharedCount);
      if(!sharedNamespaces_||!shared_.create(sharedCount))return false;
      for(uint32_t i=0;i<sharedCount;++i)sharedNamespaces_[i]=shared[i];
    }
    first_=first;count_=count;return true;
  }
  uint32_t checkpointCount() const override {return count_;}
  uint32_t format() const override {return scalable_?2u:1u;}
  uint32_t sharedCount() const override {return uint32_t(shared_.count);}
  uint32_t sharedNamespace(uint32_t i) const override {return i<shared_.count?sharedNamespaces_[i]:0;}
  uint32_t status(uint32_t slot) const override {
    const auto ns=nameSpace(slot);if(!ns)return UINT32_MAX;
    if(const auto* s=scalable(ns))return uint32_t(s->status);
    return uint32_t(installed_[slot-1].status);
  }
  int32_t get(uint32_t ns,const char* key,void* out,uint32_t cap,uint32_t* size,uint32_t limit,bool provider) override {
    if(!provider&&installed(ns)&&installed_[ns-first_].status!=RiscStorage::SdFirstKeyValue::LegacyNative){
      auto j=journal();return RiscStorage::SdFirstKeyValue::get(installed_[ns-first_],workspace_[0],j,ns,key,out,cap,size);
    }
    auto* state=scalable(ns);
    const uint32_t capacity=cap<limit?cap:limit;
    return state?RiscStorage::ScalableSettings::get(*state,key,out,capacity,size):backend_.get(backend_.context,ns,key,out,capacity,size);
  }

  int32_t put(uint32_t ns,const char* key,const void* data,uint32_t size,bool provider) override {
    int32_t rc;
    if(auto* s=scalable(ns))rc=RiscStorage::ScalableSettings::put(*s,*backend_.scalableSettings,ns,key,data,size);
    else if(!provider&&installed(ns)&&installed_[ns-first_].status!=RiscStorage::SdFirstKeyValue::LegacyNative){auto j=journal();rc=RiscStorage::SdFirstKeyValue::put(installed_[ns-first_],workspace_[0],j,ns,key,data,size);}
    else rc=backend_.put(backend_.context,ns,key,data,size);
    return rc==RISC_KEY_VALUE_OK||rc==RISC_KEY_VALUE_BUSY?rc:RISC_KEY_VALUE_IO;
  }
  int32_t restore(uint32_t slot,const char* owner,const void* data,uint32_t size,bool missing) override {
    const auto ns=nameSpace(slot);if(!ns)return RISC_KEY_VALUE_CONTEXT;
    if(slot>count_){char expected[32];std::snprintf(expected,sizeof expected,"shared-%08x",unsigned(ns));if(!owner||std::strcmp(owner,expected))return RISC_KEY_VALUE_CONTEXT;}
    if(auto* s=scalable(ns)){
      const int32_t rc=RiscStorage::ScalableSettings::restore(*s,*backend_.scalableSettings,ns,owner,data,size,missing);
      if(rc!=RISC_KEY_VALUE_OK&&rc!=RISC_KEY_VALUE_BUSY)s->status=RiscStorage::ScalableSettings::Uncertain;
      return rc;
    }
    auto j=journal();return RiscStorage::SdFirstKeyValue::restore(installed_[slot-1],workspace_[0],j,ns,owner,data,size,missing);
  }
  int32_t snapshot(uint32_t slot,void* out,uint32_t cap,uint32_t* size,uint64_t* generation) override {
    if(size)*size=0;
    const auto ns=nameSpace(slot);if(!ns)return RISC_KEY_VALUE_CONTEXT;
    if(auto* s=scalable(ns)){
      uint8_t* bytes=nullptr;uint32_t n=0;const int32_t rc=RiscStorage::ScalableSettings::snapshot(*s,ns,&bytes,&n,generation);if(rc)return rc;
      if(!size||(!out&&cap)){std::free(bytes);return RISC_KEY_VALUE_INVALID;}
      *size=n;if(cap<n){std::free(bytes);return RISC_KEY_VALUE_BUFFER_SMALL;}
      std::memcpy(out,bytes,n);std::free(bytes);return RISC_KEY_VALUE_OK;
    }
    return RiscStorage::SdFirstKeyValue::snapshot(installed_[slot-1],ns,out,cap,size,generation);
  }
  int32_t acknowledge(uint32_t slot,uint64_t generation) override {
    const auto ns=nameSpace(slot);if(!ns)return RISC_KEY_VALUE_CONTEXT;
    if(auto* s=scalable(ns))return RiscStorage::ScalableSettings::acknowledge(*s,*backend_.scalableSettings,ns,generation);
    auto j=journal();return RiscStorage::SdFirstKeyValue::acknowledge(installed_[slot-1],workspace_[0],j,ns,generation);
  }
  bool launchReady(uint32_t ns,bool* pending) const override {
    if(const auto* s=scalable(ns)){
      if(s->status==RiscStorage::ScalableSettings::Clean||s->status==RiscStorage::ScalableSettings::Dirty)return true;
      if(pending)*pending=s->status==RiscStorage::ScalableSettings::Cold||s->status==RiscStorage::ScalableSettings::Restoring;
      return false;
    }
    if(installed(ns)){const auto s=installed_[ns-first_].status;return s!=RiscStorage::SdFirstKeyValue::Cold&&s!=RiscStorage::SdFirstKeyValue::Uncertain;}
    return true;
  }
 private:
  const KeyValueBackend& backend_;
  uint32_t first_=0,count_=0;bool configured_=false;
  MetadataArray<RiscStorage::SdFirstKeyValue::State> installed_;
  MetadataArray<RiscStorage::SdFirstKeyValue::Workspace> workspace_;
  RiscStorage::ScalableSettings::Collection scalable_,shared_;
  MetadataArray<uint32_t> sharedNamespaces_;
  bool installed(uint32_t ns) const {return installed_&&ns>=first_&&ns-first_<count_;}
  uint32_t nameSpace(uint32_t slot) const {if(!slot)return 0;if(slot<=count_)return first_+slot-1;return sharedNamespace(slot-count_-1);}
  RiscStorage::ScalableSettings::State* scalable(uint32_t ns) const {
    if(scalable_&&ns>=first_&&ns-first_<count_)return scalable_.items+ns-first_;
    for(size_t i=0;i<shared_.count;++i)if(sharedNamespaces_[i]==ns)return shared_.items+i;
    return nullptr;
  }
  RiscStorage::SdFirstKeyValue::Journal journal() const {return {backend_.context,backend_.journalRead,backend_.journalWrite,backend_.get,backend_.legacyNamespace};}
};
inline std::unique_ptr<SettingsSession> openLegacySettings(const KeyValueBackend& backend){
  return std::unique_ptr<SettingsSession>(new(std::nothrow) LegacySettingsSession(backend));
}
}
