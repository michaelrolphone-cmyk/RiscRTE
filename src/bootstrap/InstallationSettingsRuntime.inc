namespace RiscBoot {
uint32_t Runtime::installationSharedSettingsNamespace(void*c,uint32_t index){
 auto*r=static_cast<Runtime*>(c);
 if(!r||r!=currentRuntime||!r->active()||!r->port_.owner()||r->retained_||!r->settings_)return 0;
 return r->settings_->sharedNamespace(index);
}
uint32_t Runtime::installationSettingsStatus(void*c,uint32_t slot){
 auto*r=static_cast<Runtime*>(c);
 if(!r||r!=currentRuntime||!r->active()||!r->port_.owner()||r->retained_||!r->settings_)return UINT32_MAX;
 return r->settings_->status(slot);
}
int32_t Runtime::installationSettingsRestore(void*c,uint32_t slot,const char*owner,const void*data,uint32_t size,bool missing){
 auto*r=static_cast<Runtime*>(c);if(installationSettingsStatus(c,slot)==UINT32_MAX)return RISC_KEY_VALUE_CONTEXT;
 if(!r->providerStorageSafe()||r->installation_[0].call)return RISC_KEY_VALUE_BUSY;
 return r->settings_->restore(slot,owner,data,size,missing);
}
int32_t Runtime::installationSettingsSnapshot(void*c,uint32_t slot,void*out,uint32_t capacity,uint32_t*size,uint64_t*generation){
 if(size)*size=0;
 auto*r=static_cast<Runtime*>(c);if(installationSettingsStatus(c,slot)==UINT32_MAX)return RISC_KEY_VALUE_CONTEXT;
 return r->settings_->snapshot(slot,out,capacity,size,generation);
}
int32_t Runtime::installationSettingsAck(void*c,uint32_t slot,uint64_t generation){
 auto*r=static_cast<Runtime*>(c);if(installationSettingsStatus(c,slot)==UINT32_MAX)return RISC_KEY_VALUE_CONTEXT;
 if(!r->providerStorageSafe()||r->installation_[0].call)return RISC_KEY_VALUE_BUSY;
 return r->settings_->acknowledge(slot,generation);
}
}
