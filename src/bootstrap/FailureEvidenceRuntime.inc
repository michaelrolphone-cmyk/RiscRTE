// App invocations borrow no native memory; every result is copied by value.
bool Runtime::failureEvidenceToken(uint64_t token,bool healthy) const {
  if(currentRuntime!=this || !port_.owner() || !port_.failureEvidence ||
     !token || token!=invocation_->streams_.context() ||
     invocation_!=&mainInvocation_ || !invocation_->defaultRunning_ ||
     !invocation_->entryRunning_ || promotionRunning_)return false;
  if(!healthy)return true;
  return active() && !retained_ && !invocation_->queued_[0] &&
    !foregroundRunning_ && !shellDispatching_ && !yielding_ &&
    !graph_.lifecycleBusy() && !invocation_->streams_.busy() &&
    !invocation_->streams_.retained() && graph_.activationSafe() &&
    !metadataCloseRetained_ && appDataExitSafe() && providerStorageSafe();
}
bool Runtime::failureEvidenceClient(risc_failure_evidence_client_v1* out) {
  if(!out || out->struct_size<sizeof(*out))return false;
  *out={};
  const uint64_t token=invocation_->streams_.context();
  if(!failureEvidenceToken(token,true) || !port_.failureEvidence->read ||
     !port_.failureEvidence->acknowledge)return false;
  *out={1,sizeof(*out),token,
    [](uint64_t id,risc_failure_evidence_v1* record)->int32_t {
      return currentRuntime?currentRuntime->failureEvidenceRead(id,record):RISC_FAILURE_EVIDENCE_DENIED;
    },
    [](uint64_t id,uint32_t boot,uint32_t sequence)->int32_t {
      return currentRuntime?currentRuntime->failureEvidenceAcknowledge(id,boot,sequence):RISC_FAILURE_EVIDENCE_DENIED;
    }};
  return true;
}
int32_t Runtime::failureEvidenceRead(uint64_t token,risc_failure_evidence_v1* out) {
  if(!failureEvidenceToken(token,false) || !port_.failureEvidence->read)return RISC_FAILURE_EVIDENCE_DENIED;
  if(!out || out->struct_size<sizeof(*out))return RISC_FAILURE_EVIDENCE_INVALID;
  return port_.failureEvidence->read(out);
}
int32_t Runtime::failureEvidenceAcknowledge(uint64_t token,uint32_t boot,uint32_t sequence) {
  if(!failureEvidenceToken(token,true) || !port_.failureEvidence->acknowledge)return RISC_FAILURE_EVIDENCE_DENIED;
  return port_.failureEvidence->acknowledge(boot,sequence);
}
void Runtime::failureBreadcrumb(uint32_t phase,const char* application) {
  if(!port_.failureEvidence || !port_.failureEvidence->breadcrumb || retained_)return;
  if(!application)application=invocation_->current_;
  const size_t prefix=strlen(root_);
  if(application && !strncmp(application,root_,prefix) && application[prefix]=='/')application+=prefix+1;
  port_.failureEvidence->breadcrumb(application,invocation_->token,phase,invocation_->role);
}
