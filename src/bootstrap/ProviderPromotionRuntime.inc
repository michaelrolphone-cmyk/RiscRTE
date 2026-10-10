bool Runtime::promotionSafe() const {
  return !retained_ && !metadataCloseRetained_ && !invocation_->queued_[0] &&
    appDataExitSafe() && (!invocation_->installedFiles_ || !invocation_->installedFiles_->retained()) &&
    (!port_.appExitSafe || port_.appExitSafe()) && graph_.activationSafe();
}
int32_t Runtime::promoteProviders(void* context) {
  auto* r=currentRuntime;
  if(!r || !context || !r->active() || !r->invocation_->defaultRunning_ || !r->invocation_->entryRunning_ ||
     !r->invocation_->appPolicy_ || context!=r->invocation_->promotionContext_)return RISC_PROVIDER_PROMOTION_CONTEXT;
  bool live=false;
  for(const auto& grant:r->invocation_->appGrants_)if(grant.live && grant.api==&r->invocation_->promotionTable_)live=true;
  if(!live)return RISC_PROVIDER_PROMOTION_CONTEXT;
  if(r->promotionRunning_)return RISC_PROVIDER_PROMOTION_BUSY;
  if(!r->promotionSafe())return RISC_PROVIDER_PROMOTION_CONTEXT;
  if(!r->demandActivation_)return RISC_PROVIDER_PROMOTION_ALREADY_READY;
  r->promotionRunning_=true;
  size_t active=0;
  for(size_t i=0;i<r->driverCount_;++i) {
    const auto& d=r->drivers_[i];
    if(r->demandRetention_ && !r->graph_.activeFrom(d.id,d.provides,d.api,d.instance))continue;
    ++active;
    if(r->grants_[i].slot)continue;
    auto grant=r->graph_.acquireFrom(d.id,d.provides,d.api,d.instance);
    if(grant.slot)r->grants_[i]=grant;
    // start/cleanup can expose native uncertainty even without a graph grant.
    if(!r->promotionSafe()) {
      r->retained_=true;r->invocation_->active_=false;r->promotionRunning_=false;
      r->fail("provider promotion retained; restart required");
      if(r->residentEnabled_)(void)r->appExitBarrier();
      return RISC_PROVIDER_PROMOTION_RETAINED;
    }
    if(!grant.slot) {
      r->promotionRunning_=false;r->fail(r->graph_.lastError());
      return RISC_PROVIDER_PROMOTION_FAILED;
    }
  }
  r->demandActivation_=false;r->promotionRunning_=false;
  if(r->demandRetention_) {
    RISC_STAGE_LOG("providers retention armed active=%u deferred=%u",unsigned(active),unsigned(r->driverCount_-active));
  }
  return RISC_PROVIDER_PROMOTION_OK;
}
