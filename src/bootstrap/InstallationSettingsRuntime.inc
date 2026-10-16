namespace RiscBoot {
uint32_t Runtime::installationSharedSettingsNamespace(void*c,uint32_t index){
 auto*r=static_cast<Runtime*>(c);
 if(!r||r!=currentRuntime||!r->active()||!r->port_.owner()||r->retained_||!r->settingsCallSafe())return 0;
 const uint32_t ns=r->settings_->sharedNamespace(index);
 return r->settingsCallSafe()?ns:0;
}
uint32_t Runtime::installationSettingsStatus(void*c,uint32_t slot){
 auto*r=static_cast<Runtime*>(c);
 if(!r||r!=currentRuntime||!r->active()||!r->port_.owner()||r->retained_||!r->settingsCallSafe())return UINT32_MAX;
 const uint32_t status=r->settings_->status(slot);
 return r->settingsCallSafe()?status:UINT32_MAX;
}
int32_t Runtime::installationSettingsRestore(void*c,uint32_t slot,const char*owner,const void*data,uint32_t size,bool missing){
 auto*r=static_cast<Runtime*>(c);if(installationSettingsStatus(c,slot)==UINT32_MAX)return RISC_KEY_VALUE_CONTEXT;
 if(!r->providerStorageSafe()||r->installation_[0].call)return RISC_KEY_VALUE_BUSY;
 const int32_t rc=r->settings_->restore(slot,owner,data,size,missing);
 return r->settingsCallSafe()?rc:RISC_KEY_VALUE_CONTEXT;
}
int32_t Runtime::installationSettingsSnapshot(void*c,uint32_t slot,void*out,uint32_t capacity,uint32_t*size,uint64_t*generation){
 if(size)*size=0;
 auto*r=static_cast<Runtime*>(c);if(installationSettingsStatus(c,slot)==UINT32_MAX)return RISC_KEY_VALUE_CONTEXT;
 const int32_t rc=r->settings_->snapshot(slot,out,capacity,size,generation);
 return r->settingsCallSafe()?rc:RISC_KEY_VALUE_CONTEXT;
}
int32_t Runtime::installationSettingsAck(void*c,uint32_t slot,uint64_t generation){
 auto*r=static_cast<Runtime*>(c);if(installationSettingsStatus(c,slot)==UINT32_MAX)return RISC_KEY_VALUE_CONTEXT;
 if(!r->providerStorageSafe()||r->installation_[0].call)return RISC_KEY_VALUE_BUSY;
 const int32_t rc=r->settings_->acknowledge(slot,generation);
 return r->settingsCallSafe()?rc:RISC_KEY_VALUE_CONTEXT;
}
}
