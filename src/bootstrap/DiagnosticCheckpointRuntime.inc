// The app supplies only bounded text; identity and authority come from Runtime.
bool Runtime::diagnosticCheckpointToken(uint64_t token) const {
  return currentRuntime==this && port_.diagnosticCheckpoint && token &&
    token==invocation_->streams_.context() && active() && !retained_ &&
    !invocation_->streams_.retained() && !invocation_->queued_[0] &&
    !promotionRunning_ && !yielding_ && !graph_.lifecycleBusy();
}
bool Runtime::diagnosticCheckpointClient(risc_diagnostic_checkpoint_client_v1* out) {
  if(!out || out->struct_size<sizeof(*out))return false;
  *out={};
  const uint64_t token=invocation_->streams_.context();
  if(!diagnosticCheckpointToken(token))return false;
  *out={RISC_DIAGNOSTIC_CHECKPOINT_API_V1,sizeof(*out),token,
    [](uint64_t id,const char* text,uint32_t length)->int32_t {
      return currentRuntime?currentRuntime->diagnosticCheckpointWrite(id,text,length):RISC_DIAGNOSTIC_CHECKPOINT_DENIED;
    }};
  return true;
}
int32_t Runtime::diagnosticCheckpointWrite(uint64_t token,const char* text,uint32_t length) {
  if(!diagnosticCheckpointToken(token))return RISC_DIAGNOSTIC_CHECKPOINT_DENIED;
  if(!text || !length)return RISC_DIAGNOSTIC_CHECKPOINT_INVALID;
  const char* application=invocation_->current_;
  const size_t prefix=strlen(root_);
  if(!strncmp(application,root_,prefix) && application[prefix]=='/')application+=prefix+1;
  const size_t size=strnlen(application,193);
  if(!size || size>192)return RISC_DIAGNOSTIC_CHECKPOINT_DENIED;
  return port_.diagnosticCheckpoint(application,token,text,length);
}
