// Serialized, explicitly selected host -> foreground -> host callback lifetime.
bool Runtime::residentPolicy(JsonVariantConst value) {
  if(value.isUnbound())return true;
  if(!value.is<JsonObjectConst>() || !keys(value.as<JsonObjectConst>(),{"api","host","foreground"},{"legacy"}) ||
     !value["api"].is<unsigned>() || value["api"].as<unsigned>()!=1 ||
     !value["foreground"].is<JsonArrayConst>() || !value["foreground"].size() ||
     value["foreground"].size()>=MaxAppPolicies)return fail("invalid resident shell policy");
  char relative[193],selected[256];
  if(!text(value["host"],relative,sizeof(relative)) || !path(root_,relative,selected,sizeof(selected)) ||
     strcmp(selected,invocation_->current_))return fail("resident host must match configured default");
  bool host=false;for(size_t p=0;p<policyCount_;++p)host=host || !strcmp(policies_[p].elf,selected);
  if(!host)return fail("resident host must have admitted app policy");
  for(JsonVariantConst item:value["foreground"].as<JsonArrayConst>()) {
    if(!text(item,relative,sizeof(relative)) || !path(root_,relative,selected,sizeof(selected)) ||
       !strcmp(selected,invocation_->current_))return fail("invalid resident foreground path");
    size_t p=0;for(;p<policyCount_;++p)if(!strcmp(policies_[p].elf,selected))break;
    if(p==policyCount_ || residentForeground_[p])return fail("resident foreground must be uniquely admitted");
    // The native wake store has one durable pending snapshot. Keep the host as
    // its sole resident owner rather than letting a child cancel/replace it.
    for(size_t g=0;g<policies_[p].count;++g)if(policies_[p].grants[g].driver==RetainedWakeDriver)
      return fail("resident foreground cannot own retained-wake state");
    residentForeground_[p]=true;
  }
  const auto legacy=value["legacy"];
  if(!legacy.isUnbound()) {
    if(!legacy.is<JsonArrayConst>() || legacy.size()>=MaxAppPolicies)return fail("invalid resident legacy policy");
    for(JsonVariantConst item:legacy.as<JsonArrayConst>()) {
      if(!text(item,relative,sizeof(relative)) || !path(root_,relative,selected,sizeof(selected)) ||
         !strcmp(selected,invocation_->current_))return fail("invalid resident legacy path");
      size_t p=0;for(;p<policyCount_;++p)if(!strcmp(policies_[p].elf,selected))break;
      if(p==policyCount_ || residentForeground_[p] || residentLegacy_[p])return fail("resident legacy must be uniquely admitted");
      residentLegacy_[p]=true;
    }
  }
  residentEnabled_=true;return true;
}
bool Runtime::residentAllowed(const char* absolute) const {
  for(size_t p=0;p<policyCount_;++p)if(residentForeground_[p] && !strcmp(policies_[p].elf,absolute))return true;
  return false;
}
bool Runtime::residentLegacyAllowed(const char* absolute) const {
  for(size_t p=0;p<policyCount_;++p)if(residentLegacy_[p] && !strcmp(policies_[p].elf,absolute))return true;
  return false;
}
bool Runtime::residentToken(uint64_t token,uint32_t role,bool entry) const {
  return residentEnabled_ && currentRuntime==this && port_.owner() && token &&
    invocation_->streams_.context()==token && invocation_->role==role &&
    (!entry || (active() && invocation_->entryRunning_ && !retained_));
}
bool Runtime::residentClient(risc_resident_client_v1* out) {
  if(!out || out->struct_size<sizeof(*out))return false;
  *out={};
  if(!residentEnabled_ || !active() || !invocation_->entryRunning_ || retained_ ||
     !invocation_->role || !invocation_->streams_.context())return false;
  *out={1,sizeof(*out),invocation_->streams_.context(),invocation_->role,0,
    [](uint64_t token,const risc_resident_callbacks_v1* callbacks)->int32_t {
      return currentRuntime?currentRuntime->registerShell(token,callbacks):RISC_RESIDENT_DENIED;
    },
    [](uint64_t token,const char* path,risc_resident_result_v1* result)->int32_t {
      return currentRuntime?currentRuntime->runForeground(token,path,result):RISC_RESIDENT_DENIED;
    },
    [](uint64_t token,const risc_resident_request_v1* request,risc_resident_reply_v1* reply)->int32_t {
      return currentRuntime?currentRuntime->residentCheckpoint(token,request,reply):RISC_RESIDENT_DENIED;
    },
    [](uint64_t token)->int32_t {return currentRuntime?currentRuntime->residentExit(token):RISC_RESIDENT_DENIED;},
    [](uint64_t token,risc_resident_failure_v1* failure)->bool {return currentRuntime && currentRuntime->residentFailure(token,failure);}};
  return true;
}
int32_t Runtime::registerShell(uint64_t token,const risc_resident_callbacks_v1* callbacks) {
  if(!residentToken(token,RISC_RESIDENT_ROLE_HOST))return RISC_RESIDENT_DENIED;
  if(foregroundRunning_ || shellDispatching_ || graph_.lifecycleBusy() || promotionRunning_)return RISC_RESIDENT_BUSY;
  if(shellToken_ || !callbacks || callbacks->api_version!=1 || callbacks->struct_size<offsetof(risc_resident_callbacks_v1,loading) ||
     !callbacks->dispatch)return RISC_RESIDENT_INVALID;
  if(!appExitBarrier())return RISC_RESIDENT_RETAINED;
  shellCallbacks_={};
  memcpy(&shellCallbacks_,callbacks,offsetof(risc_resident_callbacks_v1,loading));
  if(callbacks->struct_size>=sizeof(*callbacks))shellCallbacks_.loading=callbacks->loading;
  shellToken_=token;return RISC_RESIDENT_OK;
}
bool Runtime::selectInvocation(Invocation& next) {
#if defined(ESP_PLATFORM) || defined(RISC_NATIVE_APP_MEMORY_TEST)
  if(!native_app_memory_select(next.streams_.context())) {
    retained_=true;fail("app allocation context switch retained");(void)appExitBarrier();return false;
  }
#endif
  invocation_=&next;
  if(next.entryRunning_)failureBreadcrumb(RISC_FAILURE_PHASE_MAIN);
  return true;
}
int32_t Runtime::residentLoading(const char* absolute) {
  if(!appExitBarrier())return RISC_RESIDENT_RETAINED;
  if(!shellCallbacks_.loading)return RISC_RESIDENT_OK;
  if(invocation_!=&mainInvocation_ && !selectInvocation(mainInvocation_))return RISC_RESIDENT_RETAINED;
  const size_t prefix=strlen(root_);
  if(strncmp(absolute,root_,prefix) || absolute[prefix]!='/')return RISC_RESIDENT_DENIED;
  const char* relative=absolute+prefix+1;
  if(strnlen(relative,193)>192)return RISC_RESIDENT_DENIED;
  char copied[193];strcpy(copied,relative);
  error_[0]=0;shellDispatching_=true;shellLoading_=true;
  const int32_t status=shellCallbacks_.loading(shellCallbacks_.context,copied);
  shellLoading_=false;shellDispatching_=false;
  if(status!=RISC_RESIDENT_OK && status!=RISC_RESIDENT_BUSY) {
    retained_=true;fail("resident loading cleanup uncertain");
  }
  if(!appExitBarrier())return RISC_RESIDENT_RETAINED;
  return status;
}
void Runtime::recordResidentFailure(uint32_t kind,int32_t status,const char* name,uint64_t token) {
  // Retention keeps the first diagnostic. A later native stack return is not a
  // second failure and cannot erase evidence of the unsafe resource boundary.
  if(lastFailure_.status==RISC_RESIDENT_RETAINED)return;
  lastFailure_={};lastFailure_.struct_size=sizeof(lastFailure_);lastFailure_.kind=kind;
  lastFailure_.status=status;lastFailure_.invocation=token;
  const size_t prefix=strlen(root_);
  if(name && !strncmp(name,root_,prefix) && name[prefix]=='/')name+=prefix+1;
  snprintf(lastFailure_.application,sizeof(lastFailure_.application),"%s",name?name:"");
  snprintf(lastFailure_.detail,sizeof(lastFailure_.detail),"%s",error_);
}
bool Runtime::residentFailure(uint64_t token,risc_resident_failure_v1* out) {
  if(!out || out->struct_size<sizeof(*out) || !residentToken(token,RISC_RESIDENT_ROLE_HOST,false))return false;
  *out=lastFailure_;out->struct_size=sizeof(*out);return out->kind!=RISC_RESIDENT_FAILURE_NONE;
}
int32_t Runtime::runForeground(uint64_t token,const char* relative,risc_resident_result_v1* out) {
  if(!out || out->struct_size<sizeof(*out))return RISC_RESIDENT_INVALID;
  *out={};out->struct_size=sizeof(*out);
  auto refuse=[out](int32_t status){out->status=status;return status;};
  if(!residentToken(token,RISC_RESIDENT_ROLE_HOST) || shellToken_!=token)return refuse(RISC_RESIDENT_DENIED);
  if(foregroundRunning_ || shellDispatching_ || promotionRunning_ || graph_.lifecycleBusy() || yielding_)return refuse(RISC_RESIDENT_BUSY);
  char absolute[256];
  if(mainInvocation_.queued_[0] || mainInvocation_.fileOpen_.phase!=FileOpenState::Phase::Idle)return refuse(RISC_RESIDENT_BUSY);
  const bool resume=!relative;
  if(resume) {
    if(!residentContinuation_[0])return refuse(RISC_RESIDENT_NO_PENDING);
    strcpy(absolute,residentContinuation_);
  }else {
    // A fresh host must first resolve a saved file caller/foreground request.
    if(residentContinuation_[0])return refuse(RISC_RESIDENT_BUSY);
    if(!path(root_,relative,absolute,sizeof(absolute)) ||
       (!residentAllowed(absolute) && !residentLegacyAllowed(absolute)))return refuse(RISC_RESIDENT_DENIED);
  }
  if(!appExitBarrier()) {out->status=RISC_RESIDENT_RETAINED;out->failure=lastFailure_;return out->status;}
  if(residentLegacyAllowed(absolute)) {
    const int32_t status=residentLoading(absolute);
    if(status!=RISC_RESIDENT_OK) {
      if(status==RISC_RESIDENT_BUSY){residentContinuation_[0]=0;residentTransferFileOpen_={};}
      if(status<0)out->failure=lastFailure_;
      return refuse(status);
    }
    strcpy(mainInvocation_.queued_,absolute);
    if(!resume)residentTransferFileOpen_={};
    residentContinuation_[0]=0;
    return refuse(RISC_RESIDENT_HANDOFF);
  }
  auto allocationFailure=[&]()->int32_t {
    fail("resident foreground context allocation failed");
    recordResidentFailure(RISC_RESIDENT_FAILURE_ALLOCATION,RISC_RESIDENT_FAILED,absolute,0);
    out->status=RISC_RESIDENT_FAILED;out->failure=lastFailure_;
    if(shellCallbacks_.failed) {
      error_[0]=0; // A new callback failure must not inherit a recoverable error.
      shellDispatching_=true;shellCallbacks_.failed(shellCallbacks_.context,&out->failure);shellDispatching_=false;
      if(!appExitBarrier()){out->status=RISC_RESIDENT_RETAINED;out->failure=lastFailure_;}
    }
    if(!retained_)error_[0]=0;
    return out->status;
  };
  if(!foreground_) {
    foreground_.reset(new(std::nothrow) Invocation(graph_,{this,streamBindingValid,streamRetain,streamYield}));
    if(!foreground_)return allocationFailure();
    if(mainInvocation_.installedFiles_) {
      foreground_->installedFiles_.reset(new(std::nothrow) InstalledFiles());
      if(!foreground_->installedFiles_ || !foreground_->installedFiles_->cloneConfiguration(*mainInvocation_.installedFiles_)) {
        foreground_.reset();return allocationFailure();
      }
    }
  }
  foregroundRunning_=true;foregroundExit_=false;
  invocation_=foreground_.get();strcpy(invocation_->current_,absolute);
  invocation_->fileOpen_=resume?residentTransferFileOpen_:FileOpenState{};
  residentContinuation_[0]=0;residentTransferFileOpen_={};
  int32_t status=RISC_RESIDENT_OK;
  for(;;) {
    auto* child=foreground_.get();
    status=residentLoading(child->current_);
    if(status!=RISC_RESIDENT_OK){invocation_=child;break;}
    if(residentLegacyAllowed(child->current_)) {
      strcpy(mainInvocation_.queued_,child->current_);residentTransferFileOpen_=child->fileOpen_;
      status=RISC_RESIDENT_HANDOFF;invocation_=child;break;
    }
    invocation_=child;
    invocation_->queued_[0]=0;
    const bool completed=runOne(invocation_->current_);
    out->invocation=invocation_->token;
    if(retained_) {
      status=RISC_RESIDENT_RETAINED;
      recordResidentFailure(RISC_RESIDENT_FAILURE_RETAINED,status,invocation_->current_,out->invocation);break;
    }
    if(!completed) {
      status=invocation_->status==RISC_RESIDENT_INCOMPATIBLE?RISC_RESIDENT_INCOMPATIBLE:RISC_RESIDENT_FAILED;
      recordResidentFailure(invocation_->failureKind?invocation_->failureKind:uint32_t(RISC_RESIDENT_FAILURE_LOAD),status,invocation_->current_,out->invocation);
    }
    const bool caller=fileOpenAfterRun(completed);
    if(!caller && (!completed || !invocation_->queued_[0] || !strcmp(invocation_->queued_,default_)))break;
    const char* next=caller?invocation_->current_:invocation_->queued_;
    if(!residentAllowed(next) && !residentLegacyAllowed(next)){status=RISC_RESIDENT_DENIED;break;}
    if(!caller)strcpy(invocation_->current_,next);
    error_[0]=0;
  }
  invocation_->fileOpen_={};
  if(retained_)invocation_=&mainInvocation_;
  else if(!selectInvocation(mainInvocation_)) {
    status=RISC_RESIDENT_RETAINED;recordResidentFailure(RISC_RESIDENT_FAILURE_RETAINED,status,absolute,out->invocation);
    invocation_=&mainInvocation_;
  }
  foregroundRunning_=false;foregroundExit_=false;
  out->status=status;if(status<0)out->failure=lastFailure_;
  if(!retained_ && status<0 && shellCallbacks_.failed) {
    error_[0]=0;
    shellDispatching_=true;shellCallbacks_.failed(shellCallbacks_.context,&out->failure);shellDispatching_=false;
    if(!appExitBarrier()){out->status=status=RISC_RESIDENT_RETAINED;out->failure=lastFailure_;}
  }
  if(!retained_)error_[0]=0;
  return status;
}
int32_t Runtime::residentExit(uint64_t token) {
  if(!residentToken(token,RISC_RESIDENT_ROLE_HOST) || shellToken_!=token || !shellDispatching_ || shellLoading_ || !foregroundRunning_)return RISC_RESIDENT_DENIED;
  foregroundExit_=true;return RISC_RESIDENT_OK;
}
int32_t Runtime::residentCheckpoint(uint64_t token,const risc_resident_request_v1* request,risc_resident_reply_v1* reply) {
  if(!residentToken(token,RISC_RESIDENT_ROLE_FOREGROUND) || !foregroundRunning_ || !shellToken_)return RISC_RESIDENT_DENIED;
  if(shellDispatching_ || yielding_ || promotionRunning_ || graph_.lifecycleBusy() || mainInvocation_.streams_.busy())return RISC_RESIDENT_BUSY;
  constexpr uint32_t pollFlags=RISC_RESIDENT_POLL_ACTIVITY|RISC_RESIDENT_POLL_INHIBIT_IDLE|RISC_RESIDENT_POLL_INHIBIT_POLICY;
  if(!request || request->struct_size!=sizeof(*request) || !request->reason || request->reserved ||
     (request->flags&~pollFlags) || (request->flags && request->reason!=RISC_RESIDENT_CHECKPOINT_POLL) ||
     !reply || reply->struct_size<sizeof(*reply))return RISC_RESIDENT_INVALID;
  if(foregroundExit_ || invocation_->queued_[0]){invocation_->residentPolicyPending_=false;return RISC_RESIDENT_EXIT;}
  if(request->reason==RISC_RESIDENT_CHECKPOINT_POLICY && !invocation_->residentPolicyPending_)return RISC_RESIDENT_INVALID;
  if(!appExitBarrier())return RISC_RESIDENT_RETAINED;
  const auto copied=*request;auto* child=invocation_;
  risc_resident_reply_v1 result{sizeof(result),0};
  if(!selectInvocation(mainInvocation_))return RISC_RESIDENT_RETAINED;
  shellDispatching_=true;
  int32_t status=shellCallbacks_.dispatch(shellCallbacks_.context,&copied,&result);
  shellDispatching_=false;
  if(status==RISC_RESIDENT_RETAINED ||
     (status!=RISC_RESIDENT_OK && status!=RISC_RESIDENT_BUSY && status!=RISC_RESIDENT_EXIT) ||
     result.struct_size!=sizeof(result) || (result.flags&~(RISC_RESIDENT_REPLY_REDRAW|RISC_RESIDENT_REPLY_CONFIGURATION_CHANGED|RISC_RESIDENT_REPLY_POLICY_REQUEST)) ||
     ((result.flags&RISC_RESIDENT_REPLY_POLICY_REQUEST) &&
      (copied.reason!=RISC_RESIDENT_CHECKPOINT_POLL || (copied.flags&RISC_RESIDENT_POLL_INHIBIT_POLICY) ||
       status!=RISC_RESIDENT_OK || foregroundExit_)) ||
     (status==RISC_RESIDENT_BUSY && (result.flags || foregroundExit_))) {
    retained_=true;fail("resident dispatch cleanup uncertain");
  }
  if(!appExitBarrier()) {
    recordResidentFailure(RISC_RESIDENT_FAILURE_RETAINED,RISC_RESIDENT_RETAINED,child->current_,token);
    invocation_=child;return RISC_RESIDENT_RETAINED;
  }
  if(!selectInvocation(*child)) {invocation_=child;return RISC_RESIDENT_RETAINED;}
  if(foregroundExit_ || status==RISC_RESIDENT_EXIT)child->residentPolicyPending_=false;
  else if(status==RISC_RESIDENT_OK) {
    if(copied.reason==RISC_RESIDENT_CHECKPOINT_POLICY ||
       (copied.reason==RISC_RESIDENT_CHECKPOINT_POLL && (copied.flags&RISC_RESIDENT_POLL_INHIBIT_POLICY)))child->residentPolicyPending_=false;
    else if(result.flags&RISC_RESIDENT_REPLY_POLICY_REQUEST)child->residentPolicyPending_=true;
  }
  *reply=result;
  return foregroundExit_ || status==RISC_RESIDENT_EXIT ? RISC_RESIDENT_EXIT : status;
}
