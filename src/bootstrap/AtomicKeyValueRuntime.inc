// Included only by the explicitly selected Runtime.cpp broker.
bool Runtime::registerAtomicKeyValue(const char* app,uint32_t ns,RiscExperimental::NativeAtomicKeyValue& endpoint) {
  if(!port_.owner || !port_.owner() || (attempted_ && !registrationOpen_) || !app ||
     strnlen(app,96)>=96 || !RuntimePackages::safeId(app) || !ns || ns>INT32_MAX ||
     endpoint.namespaceId()!=ns || !endpoint.exitSafe() || atomicBindingCount_==8)return false;
  for(size_t i=0;i<atomicBindingCount_;++i) {
    const auto& b=atomicBindings_[i];
    if((b.nameSpace==ns && (!strcmp(b.app,app) || b.endpoint!=&endpoint)) ||
       (b.endpoint==&endpoint && b.nameSpace!=ns))return false;
  }
  auto& b=atomicBindings_[atomicBindingCount_++];strcpy(b.app,app);b.nameSpace=ns;b.endpoint=&endpoint;
  return true;
}
bool Runtime::registerAtomicKeyValueReadThrough(uint32_t ns,RiscExperimental::NativeAtomicKeyValue& endpoint) {
  if(!port_.owner || !port_.owner() || (attempted_ && !registrationOpen_) || !ns || ns>INT32_MAX ||
     endpoint.namespaceId()!=ns || !endpoint.exitSafe() || atomicBindingCount_==8)return false;
  for(size_t i=0;i<atomicBindingCount_;++i) {
    const auto& b=atomicBindings_[i];
    if((b.nameSpace==ns && (!b.app[0] || b.endpoint!=&endpoint)) ||
       (b.endpoint==&endpoint && b.nameSpace!=ns))return false;
  }
  auto& b=atomicBindings_[atomicBindingCount_++];b.nameSpace=ns;b.endpoint=&endpoint;return true;
}
bool Runtime::registerKeyValueAvailability(uint32_t ns,KeyValueAvailability status) {
  if(!port_.owner || !port_.owner() || (attempted_ && !registrationOpen_) || !status.snapshot)return false;
  for(size_t i=0;i<atomicBindingCount_;++i) {
    auto& b=atomicBindings_[i];
    if(!b.app[0] && b.nameSpace==ns && !b.availability.snapshot){b.availability=status;return true;}
  }
  return false;
}
const Runtime::KeyValueAvailability* Runtime::keyValueAvailability(uint32_t ns) const {
  for(size_t i=0;i<atomicBindingCount_;++i) {
    const auto& b=atomicBindings_[i];
    if(!b.app[0] && b.nameSpace==ns && b.availability.snapshot)return &b.availability;
  }
  return nullptr;
}
bool Runtime::keyValueAuthoritative(uint32_t ns) const {
  const auto* a=keyValueAvailability(ns);if(!a)return true;
  risc_key_value_availability_state_v1 value{sizeof(value)};
  return a->snapshot(a->context,ns,&value)==RISC_KEY_VALUE_AVAILABILITY_OK &&
    value.namespace_id==ns && value.availability==RISC_KEY_VALUE_AVAILABLE;
}
bool Runtime::keyValueRetentionObserved() const {return retained_;}
bool Runtime::keyValueReconciliationSafe(uint32_t ns) const {
  if(!port_.owner || !port_.owner() || !ns || retained_ || promotionRunning_ || graph_.lifecycleBusy())return false;
#if RISC_NATIVE_SETTINGS_LIFECYCLE
  if(currentRuntime==this && !nativeCheckpointRunning_)return false;
#else
  if(currentRuntime==this)return false;
#endif
  const auto live=[ns](const Invocation&i){for(const auto&g:i.appGrants_){if(g.live &&
    (g.keyValueNamespace==ns || (g.atomicEndpoint && g.atomicEndpoint->namespaceId()==ns)))return true;}return false;};
  if(live(mainInvocation_) || (foreground_ && live(*foreground_)))return false;
  for(const auto& p:providerStorage_)if(p.live)for(size_t i=0;i<p.count;++i)if(p.keys[i].nameSpace==ns)return false;
  return true;
}
int32_t Runtime::keyValueAvailabilitySnapshot(void* context,risc_key_value_availability_state_v1* out) {
  const auto owner=atomicKeyValueOwner.load(std::memory_order_acquire);
  if(!owner || !owner())return RISC_KEY_VALUE_AVAILABILITY_CONTEXT;
  auto* r=currentRuntime;
  if(!r || !context || !r->active() || r->retained_ || r->promotionRunning_ || r->graph_.lifecycleBusy() || !r->invocation_->entryRunning_)return RISC_KEY_VALUE_AVAILABILITY_CONTEXT;
  if(!out || out->struct_size<sizeof(*out))return RISC_KEY_VALUE_AVAILABILITY_INVALID;
  for(const auto& g:r->invocation_->appGrants_)if(g.live && g.invocation==r->invocation_->streams_.context() &&
      g.api==&g.availability && g.availability.context==context && g.availabilityNamespace){
    const auto* a=r->keyValueAvailability(g.availabilityNamespace);
    if(a)return a->snapshot(a->context,g.availabilityNamespace,out);
  }
  return RISC_KEY_VALUE_AVAILABILITY_CONTEXT;
}
RiscExperimental::NativeAtomicKeyValue* Runtime::atomicReadThrough(uint32_t ns) const {
  for(size_t i=0;i<atomicBindingCount_;++i)
    if(!atomicBindings_[i].app[0] && atomicBindings_[i].nameSpace==ns)return atomicBindings_[i].endpoint;
  return nullptr;
}
int32_t Runtime::atomicLegacyGet(uint32_t ns,const char* key,void* out,uint32_t capacity,uint32_t* outSize,uint32_t limit) {
  auto* endpoint=atomicReadThrough(ns);if(!endpoint)return RISC_KEY_VALUE_CONTEXT;
  if(endpoint->retained()){(void)atomicResult(RISC_ATOMIC_RETAINED);return RISC_KEY_VALUE_IO;}
  if(!keyValueAuthoritative(ns))return RISC_KEY_VALUE_NOT_FOUND;
  uint8_t bytes[RISC_KEY_VALUE_V2_BLOB_MAX];RiscRuntime::ScopedBufferWipe wipe(bytes);uint32_t size=0;
  risc_key_value_atomic_state_v1 state{sizeof(state)};
  int32_t rc=endpoint->state(&state);
  if(rc==RISC_ATOMIC_OK)rc=endpoint->get(state.revision,key,bytes,limit,&size);
  if(rc==RISC_ATOMIC_RETAINED)(void)atomicResult(rc);
  if(rc==RISC_ATOMIC_NOT_FOUND)return RISC_KEY_VALUE_NOT_FOUND;
  if(rc==RISC_ATOMIC_BUSY || rc==RISC_ATOMIC_STALE)return RISC_KEY_VALUE_BUSY;
  // Oversized values are IO under the old per-version contract, not size probes.
  if(rc!=RISC_ATOMIC_OK || !size || size>limit)return RISC_KEY_VALUE_IO;
  if(capacity<size){*outSize=size;return RISC_KEY_VALUE_BUFFER_SMALL;}
  memcpy(out,bytes,size);*outSize=size;return RISC_KEY_VALUE_OK;
}
Runtime::AtomicBinding* Runtime::atomicBinding(const char* app,uint32_t ns) {
  for(size_t i=0;i<atomicBindingCount_;++i)
    if(atomicBindings_[i].nameSpace==ns && !strcmp(atomicBindings_[i].app,app))return &atomicBindings_[i];
  return nullptr;
}
bool Runtime::atomicExitSafe(bool exiting) const {
  const auto safe=[exiting](const Invocation& invocation){
    // Retention revokes live without surrendering endpoint/operation custody.
    for(const auto& g:invocation.appGrants_)if(g.atomicEndpoint &&
      (g.atomicEndpoint->retained() || (exiting && (g.atomicOperation || !g.atomicEndpoint->exitSafe()))))return false;
    return true;
  };
  for(size_t i=0;i<atomicBindingCount_;++i)
    if(!atomicBindings_[i].app[0] && atomicBindings_[i].endpoint->retained())return false;
  return safe(mainInvocation_) && (!foreground_ || safe(*foreground_));
}
Runtime::AppGrant* Runtime::atomicContext(void* context) {
  const auto owner=atomicKeyValueOwner.load(std::memory_order_acquire);
  if(!owner || !owner())return nullptr;
  auto* r=currentRuntime;
  if(!r || !context || !r->active() || r->retained_ || r->promotionRunning_ || r->graph_.lifecycleBusy() || !r->invocation_->entryRunning_)return nullptr;
  for(auto& g:r->invocation_->appGrants_)
    if(g.live && g.invocation==r->invocation_->streams_.context() && g.api==&g.atomic &&
       g.atomic.context==context && g.atomicEndpoint)return &g;
  return nullptr;
}
int32_t Runtime::atomicResult(int32_t result) {
  if(result==RISC_ATOMIC_RETAINED) {
    retained_=true;fail("atomic key-value custody retained; restart required");(void)appExitBarrier(false);
  }
  return result;
}
int32_t Runtime::atomicState(void* c,risc_key_value_atomic_state_v1* out) {
  auto* g=atomicContext(c);if(!g || !out || out->struct_size<sizeof(*out))return RISC_ATOMIC_INVALID;
  if(g->atomicEndpoint->retained())return currentRuntime->atomicResult(RISC_ATOMIC_RETAINED);
  if(!currentRuntime->keyValueAuthoritative(g->atomicEndpoint->namespaceId()))return RISC_ATOMIC_BUSY;
  return currentRuntime->atomicResult(g->atomicEndpoint->state(out));
}
int32_t Runtime::atomicGet(void* c,uint64_t revision,const char* key,void* out,uint32_t cap,uint32_t* size) {
  if(size)*size=0;
  auto* g=atomicContext(c);if(!g || !size || !revision || !key || (!out&&cap))return RISC_ATOMIC_INVALID;
  if(g->atomicEndpoint->retained())return currentRuntime->atomicResult(RISC_ATOMIC_RETAINED);
  if(!currentRuntime->keyValueAuthoritative(g->atomicEndpoint->namespaceId())){if(size)*size=0;return RISC_ATOMIC_BUSY;}
  return currentRuntime->atomicResult(g->atomicEndpoint->get(revision,key,out,cap,size));
}
int32_t Runtime::atomicSubmit(void* c,uint64_t revision,const risc_key_value_atomic_mutation_v1* changes,uint32_t count,const void* intent,uint32_t size,uint64_t* token) {
  auto* g=atomicContext(c);if(token)*token=0;if(!g)return RISC_ATOMIC_INVALID;
  if(g->atomicEndpoint->retained())return currentRuntime->atomicResult(RISC_ATOMIC_RETAINED);
  if(g->atomicOperation || !currentRuntime->keyValueAuthoritative(g->atomicEndpoint->namespaceId()))return RISC_ATOMIC_BUSY;
  const int32_t result=g->atomicEndpoint->submit(revision,changes,count,intent,size,token);
  if(result==RISC_ATOMIC_OK)g->atomicOperation=*token;
  return currentRuntime->atomicResult(result);
}
int32_t Runtime::atomicCollect(void* c,uint64_t token,risc_key_value_atomic_receipt_v1* out) {
  auto* g=atomicContext(c);if(!g || !token || token!=g->atomicOperation)return RISC_ATOMIC_INVALID;
  const int32_t result=g->atomicEndpoint->collect(token,out);
  if(result==RISC_ATOMIC_OK)g->atomicOperation=0;
  return currentRuntime->atomicResult(result);
}
int32_t Runtime::atomicCancel(void* c,uint64_t token) {
  auto* g=atomicContext(c);if(!g || !token || token!=g->atomicOperation)return RISC_ATOMIC_INVALID;
  return currentRuntime->atomicResult(g->atomicEndpoint->cancel(token));
}
