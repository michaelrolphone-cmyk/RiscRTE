namespace RiscBoot {
RiscStorage::SdFirstKeyValue::Journal Runtime::settingsJournal(){
 const auto*b=port_.keyValue;
 return {this,
  [](void*c,uint32_t ns,void*out,uint32_t cap,uint32_t*size){auto*r=static_cast<Runtime*>(c);const auto&b=*r->port_.keyValue;return b.journalRead(b.context,ns,out,cap,size);},
  [](void*c,uint32_t ns,const void*data,uint32_t size){auto*r=static_cast<Runtime*>(c);const auto&b=*r->port_.keyValue;return b.journalWrite(b.context,ns,data,size);},
  b&&b->get?+[](void*c,uint32_t ns,const char*key,void*out,uint32_t cap,uint32_t*size){auto*r=static_cast<Runtime*>(c);const auto&b=*r->port_.keyValue;return b.get(b.context,ns,key,out,cap,size);}:nullptr,
  b&&b->legacyNamespace?+[](void*c,uint32_t ns,bool*exists){auto*r=static_cast<Runtime*>(c);const auto&b=*r->port_.keyValue;return b.legacyNamespace(b.context,ns,exists);}:nullptr};
}
RiscStorage::ScalableSettings::State* Runtime::scalableSettingsState(uint32_t ns)const{
 if(scalableSettings_&&installation_&&ns>=installation_[0].namespaceFirst&&ns-installation_[0].namespaceFirst<installation_[0].namespaceCount)
  return scalableSettings_.items+ns-installation_[0].namespaceFirst;
 for(size_t i=0;i<sharedSettings_.count;++i)if(sharedSettingsNamespaces_[i]==ns)return sharedSettings_.items+i;
 return nullptr;
}
uint32_t Runtime::settingsNamespace(uint32_t slot)const{
 if(!installation_||!slot)return 0;
 const auto& cfg=installation_[0];
 if(slot<=cfg.namespaceCount)return cfg.namespaceFirst+slot-1;
 const uint32_t index=slot-cfg.namespaceCount-1;
 return index<sharedSettings_.count?sharedSettingsNamespaces_[index]:0;
}
uint32_t Runtime::installationSharedSettingsNamespace(void*c,uint32_t index){
 auto*r=static_cast<Runtime*>(c);
 if(!r||r!=currentRuntime||!r->active()||!r->port_.owner()||r->retained_||index>=r->sharedSettings_.count)return 0;
 return r->sharedSettingsNamespaces_[index];
}
uint32_t Runtime::installationSettingsStatus(void*c,uint32_t slot){
 auto*r=static_cast<Runtime*>(c);
 if(!r||r!=currentRuntime||!r->active()||!r->port_.owner()||r->retained_||(!r->installedSettings_&&!r->scalableSettings_)||!r->settingsNamespace(slot))return UINT32_MAX;
 const auto*state=r->scalableSettingsState(r->settingsNamespace(slot));
 return state?uint32_t(state->status):uint32_t(r->installedSettings_[slot-1].status);
}
int32_t Runtime::installationSettingsRestore(void*c,uint32_t slot,const char*owner,const void*data,uint32_t size,bool missing){
 auto*r=static_cast<Runtime*>(c);if(installationSettingsStatus(c,slot)==UINT32_MAX)return RISC_KEY_VALUE_CONTEXT;
 if(!r->providerStorageSafe()||r->installation_[0].call)return RISC_KEY_VALUE_BUSY;
 const uint32_t ns=r->settingsNamespace(slot);
 if(slot>r->installation_[0].namespaceCount){char expected[32];std::snprintf(expected,sizeof expected,"shared-%08x",unsigned(ns));if(!owner||std::strcmp(owner,expected))return RISC_KEY_VALUE_CONTEXT;}
 if(auto*state=r->scalableSettingsState(ns)){
  const int32_t rc=RiscStorage::ScalableSettings::restore(*state,*r->port_.keyValue->scalableSettings,ns,owner,data,size,missing);
  if(rc!=RISC_KEY_VALUE_OK&&rc!=RISC_KEY_VALUE_BUSY)state->status=RiscStorage::ScalableSettings::Uncertain;
  return rc;
 }
 auto journal=r->settingsJournal();return RiscStorage::SdFirstKeyValue::restore(r->installedSettings_[slot-1],r->installedSettingsWorkspace_[0],journal,r->installation_[0].namespaceFirst+slot-1,owner,data,size,missing);
}
int32_t Runtime::installationSettingsSnapshot(void*c,uint32_t slot,void*out,uint32_t capacity,uint32_t*size,uint64_t*generation){
 if(size)*size=0;
 auto*r=static_cast<Runtime*>(c);if(installationSettingsStatus(c,slot)==UINT32_MAX)return RISC_KEY_VALUE_CONTEXT;
 if(auto*state=r->scalableSettingsState(r->settingsNamespace(slot))){
  uint8_t*bytes=nullptr;uint32_t n=0;int rc=RiscStorage::ScalableSettings::snapshot(*state,r->settingsNamespace(slot),&bytes,&n,generation);if(rc)return rc;
  if(!size||(!out&&capacity)){std::free(bytes);return RISC_KEY_VALUE_INVALID;}*size=n;if(capacity<n){std::free(bytes);return RISC_KEY_VALUE_BUFFER_SMALL;}std::memcpy(out,bytes,n);std::free(bytes);return 0;
 }
 return RiscStorage::SdFirstKeyValue::snapshot(r->installedSettings_[slot-1],r->installation_[0].namespaceFirst+slot-1,out,capacity,size,generation);
}
int32_t Runtime::installationSettingsAck(void*c,uint32_t slot,uint64_t generation){
 auto*r=static_cast<Runtime*>(c);if(installationSettingsStatus(c,slot)==UINT32_MAX)return RISC_KEY_VALUE_CONTEXT;
 if(!r->providerStorageSafe()||r->installation_[0].call)return RISC_KEY_VALUE_BUSY;
 if(auto*state=r->scalableSettingsState(r->settingsNamespace(slot)))return RiscStorage::ScalableSettings::acknowledge(*state,*r->port_.keyValue->scalableSettings,r->settingsNamespace(slot),generation);
 auto journal=r->settingsJournal();return RiscStorage::SdFirstKeyValue::acknowledge(r->installedSettings_[slot-1],r->installedSettingsWorkspace_[0],journal,r->installation_[0].namespaceFirst+slot-1,generation);
}
}
