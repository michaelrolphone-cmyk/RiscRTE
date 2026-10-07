Runtime* Runtime::retainedWakeContext(void* context){
 auto* r=currentRuntime;
 if(!r || !context || !r->active() || !r->entryRunning_ || r->retained_ ||
    r->retainedWakeContext_!=context || !r->port_.retainedWake || !r->appPolicy_ || !r->appDataExitSafe() || !r->providerStorageSafe())return nullptr;
 for(const auto& g:r->appGrants_)if(g.live && g.api==&r->retainedWakeTable_)return r;
 return nullptr;
}
bool Runtime::retainedWakeIdentity(RiscRetainedWake::Identity& out) const{
 if(!appPolicy_ || !retainedCohort_[0])return false;
 out={};strcpy(out.app,appPolicy_->id);strcpy(out.version,appPolicy_->version);strcpy(out.cohort,retainedCohort_);return true;
}
int32_t Runtime::retainedWakeRead(void* context,uint32_t type,uint32_t schema,risc_retained_wake_record_v1* out,uint32_t* cause){
 auto* r=retainedWakeContext(context);if(!r)return RISC_RETAINED_WAKE_CONTEXT;
 if(!type || !schema || !out || out->struct_size!=sizeof(*out) || !cause)return RISC_RETAINED_WAKE_INVALID;
 RiscRetainedWake::Identity id{};if(!r->retainedWakeIdentity(id))return RISC_RETAINED_WAKE_CONTEXT;
 return r->port_.retainedWake->read(id,type,schema,*out,*cause);
}
int32_t Runtime::retainedWakeStage(void* context,const risc_retained_wake_record_v1* record){
 auto* r=retainedWakeContext(context);if(!r)return RISC_RETAINED_WAKE_CONTEXT;
 if(!record || !RiscRetainedWake::valid(*record))return RISC_RETAINED_WAKE_INVALID;
 RiscRetainedWake::Identity id{};if(!r->retainedWakeIdentity(id))return RISC_RETAINED_WAKE_CONTEXT;
 r->port_.retainedWake->stage(id,*record);return RISC_RETAINED_WAKE_OK;
}
int32_t Runtime::retainedWakeClear(void* context){
 auto* r=retainedWakeContext(context);if(!r)return RISC_RETAINED_WAKE_CONTEXT;
 RiscRetainedWake::Identity id{};if(!r->retainedWakeIdentity(id))return RISC_RETAINED_WAKE_CONTEXT;
 r->port_.retainedWake->clear(id);return RISC_RETAINED_WAKE_OK;
}
