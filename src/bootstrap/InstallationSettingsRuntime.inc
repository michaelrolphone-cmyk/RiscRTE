namespace RiscBoot {
RiscStorage::SdFirstKeyValue::Journal Runtime::settingsJournal(){
 const auto*b=port_.keyValue;
 return {this,
  [](void*c,uint32_t ns,void*out,uint32_t cap,uint32_t*size){auto*r=static_cast<Runtime*>(c);const auto&b=*r->port_.keyValue;return b.journalRead(b.context,ns,out,cap,size);},
  [](void*c,uint32_t ns,const void*data,uint32_t size){auto*r=static_cast<Runtime*>(c);const auto&b=*r->port_.keyValue;return b.journalWrite(b.context,ns,data,size);},
  b&&b->get?+[](void*c,uint32_t ns,const char*key,void*out,uint32_t cap,uint32_t*size){auto*r=static_cast<Runtime*>(c);const auto&b=*r->port_.keyValue;return b.get(b.context,ns,key,out,cap,size);}:nullptr,
  b&&b->legacyNamespace?+[](void*c,uint32_t ns,bool*exists){auto*r=static_cast<Runtime*>(c);const auto&b=*r->port_.keyValue;return b.legacyNamespace(b.context,ns,exists);}:nullptr};
}
uint32_t Runtime::installationSettingsStatus(void*c,uint32_t slot){
 auto*r=static_cast<Runtime*>(c);
 if(!r||r!=currentRuntime||!r->active()||!r->port_.owner()||r->retained_||(!r->installedSettings_&&!r->scalableSettings_)||!slot||slot>r->installation_[0].namespaceCount)return UINT32_MAX;
 return r->scalableSettings_?uint32_t(r->scalableSettings_[slot-1].status):uint32_t(r->installedSettings_[slot-1].status);
}
int32_t Runtime::installationSettingsRestore(void*c,uint32_t slot,const char*owner,const void*data,uint32_t size,bool missing){
 auto*r=static_cast<Runtime*>(c);if(installationSettingsStatus(c,slot)==UINT32_MAX)return RISC_KEY_VALUE_CONTEXT;
 if(!r->providerStorageSafe()||r->installation_[0].call)return RISC_KEY_VALUE_BUSY;
 if(r->scalableSettings_)return RiscStorage::ScalableSettings::restore(r->scalableSettings_[slot-1],*r->port_.keyValue->scalableSettings,r->installation_[0].namespaceFirst+slot-1,owner,data,size,missing);
 auto journal=r->settingsJournal();return RiscStorage::SdFirstKeyValue::restore(r->installedSettings_[slot-1],r->installedSettingsWorkspace_[0],journal,r->installation_[0].namespaceFirst+slot-1,owner,data,size,missing);
}
int32_t Runtime::installationSettingsSnapshot(void*c,uint32_t slot,void*out,uint32_t capacity,uint32_t*size,uint64_t*generation){
 if(size)*size=0;
 auto*r=static_cast<Runtime*>(c);if(installationSettingsStatus(c,slot)==UINT32_MAX)return RISC_KEY_VALUE_CONTEXT;
 if(r->scalableSettings_){
  uint8_t*bytes=nullptr;uint32_t n=0;int rc=RiscStorage::ScalableSettings::snapshot(r->scalableSettings_[slot-1],r->installation_[0].namespaceFirst+slot-1,&bytes,&n,generation);if(rc)return rc;
  if(!size||(!out&&capacity)){std::free(bytes);return RISC_KEY_VALUE_INVALID;}*size=n;if(capacity<n){std::free(bytes);return RISC_KEY_VALUE_BUFFER_SMALL;}std::memcpy(out,bytes,n);std::free(bytes);return 0;
 }
 return RiscStorage::SdFirstKeyValue::snapshot(r->installedSettings_[slot-1],r->installation_[0].namespaceFirst+slot-1,out,capacity,size,generation);
}
int32_t Runtime::installationSettingsAck(void*c,uint32_t slot,uint64_t generation){
 auto*r=static_cast<Runtime*>(c);if(installationSettingsStatus(c,slot)==UINT32_MAX)return RISC_KEY_VALUE_CONTEXT;
 if(!r->providerStorageSafe()||r->installation_[0].call)return RISC_KEY_VALUE_BUSY;
 if(r->scalableSettings_)return RiscStorage::ScalableSettings::acknowledge(r->scalableSettings_[slot-1],*r->port_.keyValue->scalableSettings,r->installation_[0].namespaceFirst+slot-1,generation);
 auto journal=r->settingsJournal();return RiscStorage::SdFirstKeyValue::acknowledge(r->installedSettings_[slot-1],r->installedSettingsWorkspace_[0],journal,r->installation_[0].namespaceFirst+slot-1,generation);
}
}
