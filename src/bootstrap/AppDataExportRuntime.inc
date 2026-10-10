// Explicit boot-owned projections over the same native app-data backend. These
// records carry authority only for the consumer; durable namespace ownership is
// still exclusively the owner's storage.app-data grant.
bool Runtime::configureAppDataExports(JsonVariantConst selections) {
  for(size_t p=0;p<policyCount_;++p) {
    auto& policy=policies_[p];bool granted=false;
    for(size_t g=0;g<policy.count;++g)granted=granted || policy.grants[g].driver==AppDataExportDriver;
    const auto mapping=selections[p]["app_data_export"];
    if(mapping.isUnbound()) {if(granted)return fail("app-data export map missing");continue;}
    if(!granted || !mapping.is<JsonObjectConst>() ||
       !keys(mapping.as<JsonObjectConst>(),{"label","files"}) ||
       !text(mapping["label"],policy.exportLabel,sizeof(policy.exportLabel)) ||
       !mapping["files"].is<JsonArrayConst>() || !mapping["files"].size() ||
       mapping["files"].size()>RiscStorage::AppDataExport::EntryMax)
      return fail("invalid app-data export map");
    const auto files=mapping["files"].as<JsonArrayConst>();
    exportEntries_[p]=metadataArray<RiscStorage::AppDataExport::Entry>(files.size());
    policy.exports=exportEntries_[p].get();
    if(!policy.exports)return fail("app-data export map allocation failed");
    for(JsonObjectConst item:files) {
      auto& entry=exportEntries_[p][policy.exportCount];int64_t nameSpace=0;
      if(!keys(item,{"owner","namespace","name","path","access"}) ||
         !text(item["owner"],entry.owner,sizeof(entry.owner)) ||
         !integer(item["namespace"],1,INT32_MAX,nameSpace) ||
         !text(item["name"],entry.name,sizeof(entry.name)) ||
         !text(item["path"],entry.path,sizeof(entry.path)) ||
         (!eq(item["access"],"read") && !eq(item["access"],"read-write")))
        return fail("invalid app-data export entry");
      entry.nameSpace=uint32_t(nameSpace);entry.writable=eq(item["access"],"read-write");
      bool owned=false;
      for(size_t o=0;o<policyCount_;++o)if(!strcmp(entry.owner,policies_[o].id))
        for(size_t g=0;g<policies_[o].count;++g) {
          const auto& grant=policies_[o].grants[g];
          owned=owned || (grant.driver==AppDataDriver && grant.api==RISC_APP_DATA_API_V1 && grant.instance==entry.nameSpace);
        }
      if(!owned)return fail("app-data export owner/namespace mismatch");
      ++policy.exportCount;
    }
    if(!RiscStorage::AppDataExport::validEntries(policy.exports,policy.exportCount))
      return fail("app-data export paths/files ambiguous or invalid");
  }
  return true;
}
bool Runtime::appDataExportExitSafe() const {
  return (!mainInvocation_.appDataExport_ || mainInvocation_.appDataExport_->exitSafe()) &&
    (!foreground_ || !foreground_->appDataExport_ || foreground_->appDataExport_->exitSafe());
}
bool Runtime::appDataExportOwner(void* context) {
  Runtime* r=currentRuntime;
  if(!r || r->retained_ || !r->port_.owner() || context!=r->invocation_ ||
     r->invocation_->streams_.busy() ||
     (!r->invocation_->active_ && !r->invocation_->appDataExportEnding_))return false;
  return r->invocation_->appPolicy_ && r->invocation_->appPolicy_->exportCount;
}
bool Runtime::appDataExportSafe(void* context) {
  Runtime* r=currentRuntime;
  return r && context==r->invocation_ && !r->retained_ && r->providerStorageSafe();
}
bool Runtime::acquireAppDataExport() {
  if(!providerStorageSafe() || !invocation_->appPolicy_ || !invocation_->appPolicy_->exportCount ||
     (invocation_->appDataExport_ && invocation_->appDataExport_->active()))return false;
  // Reconstruct only a clean inactive broker. One-shot immutable core maps must
  // never leak from one invocation policy into a later handoff.
  if(invocation_->appDataExport_ && !invocation_->appDataExport_->retained())invocation_->appDataExport_.reset();
  if(!invocation_->appDataExport_) {
    auto memory=metadataArray<uint8_t>(sizeof(RiscStorage::AppDataExport));
    if(!memory)return false;
    invocation_->appDataExport_.reset(new(memory.release()) RiscStorage::AppDataExport(
      {invocation_,appDataExportOwner,appDataExportSafe,
       [](size_t size)->void*{return metadataArray<uint8_t>(size).release();},std::free}));
  }
  const auto& policy=*invocation_->appPolicy_;
  return invocation_->appDataExport_->configure(port_.appData,policy.exports,policy.exportCount,policy.exportLabel) &&
    invocation_->appDataExport_->begin(&invocation_->appDataExportTable_);
}
bool Runtime::endAppDataExport() {
  invocation_->appDataExportEnding_=true;
  const bool ended=invocation_->appDataExport_->end();
  invocation_->appDataExportEnding_=false;
  if(ended)invocation_->appDataExport_.reset();
  return ended;
}
bool Runtime::needsAppDataExportDelegation(const Runtime& candidate) const {
  for(size_t q=0;q<candidate.policyCount_;++q)if(candidate.policies_[q].exportCount) {
    bool existing=false;
    for(size_t p=0;p<policyCount_;++p)
      existing=existing || (policies_[p].exportCount && !strcmp(policies_[p].id,candidate.policies_[q].id));
    if(!existing)return true;
  }
  return false;
}
bool Runtime::validateAppDataExportCohort(const Runtime& candidate,
                                         const AppDataExportDelegation* delegations,size_t count) const {
  if(count>MaxAppPolicies || (count && !delegations))return false;
  auto sameEntry=[](const RiscStorage::AppDataExport::Entry& a,const RiscStorage::AppDataExport::Entry& b) {
    return a.nameSpace==b.nameSpace && a.writable==b.writable && !strcmp(a.owner,b.owner) &&
      !strcmp(a.name,b.name) && !strcmp(a.path,b.path);
  };
  auto sameMap=[&](const AppPolicy& policy,const char* label,const RiscStorage::AppDataExport::Entry* entries,size_t size) {
    if(!label || strnlen(label,sizeof(policy.exportLabel))>=sizeof(policy.exportLabel) ||
       strcmp(policy.exportLabel,label) || policy.exportCount!=size ||
       !RiscStorage::AppDataExport::validEntries(entries,size))return false;
    for(size_t i=0;i<size;++i) {
      bool found=false;for(size_t j=0;j<size;++j)found=found || sameEntry(policy.exports[i],entries[j]);
      if(!found)return false;
    }
    return true;
  };
  // Export admission cannot make room by silently dropping any existing
  // consumer grant. Persisted owner grants are independently preserved by the
  // ordinary cohort checks; unrelated apps retain their established behavior.
  for(size_t q=0;q<candidate.policyCount_;++q)if(candidate.policies_[q].exportCount) {
    const auto& next=candidate.policies_[q];
    for(size_t p=0;p<policyCount_;++p)if(!strcmp(policies_[p].id,next.id))
      for(size_t g=0;g<policies_[p].count;++g) {
        const auto& old=policies_[p].grants[g];bool found=false;
        for(size_t h=0;h<next.count;++h) {
          const auto& grant=next.grants[h];
          found=found || (old.api==grant.api && old.instance==grant.instance && !strcmp(old.capability,grant.capability));
        }
        if(!found)return false;
      }
  }
  // Existing authority is immutable, including reductions and label changes.
  // The optional native input can admit only a first map, never rewrite one.
  for(size_t p=0;p<policyCount_;++p)if(policies_[p].exportCount) {
    bool found=false;
    for(size_t q=0;q<candidate.policyCount_;++q)if(!strcmp(policies_[p].id,candidate.policies_[q].id))
      found=sameMap(candidate.policies_[q],policies_[p].exportLabel,policies_[p].exports,policies_[p].exportCount);
    if(!found)return false;
  }
  bool consumed[MaxAppPolicies]{};
  for(size_t q=0;q<candidate.policyCount_;++q) {
    const auto& next=candidate.policies_[q];if(!next.exportCount)continue;
    bool existing=false;
    for(size_t p=0;p<policyCount_;++p)existing=existing || (policies_[p].exportCount && !strcmp(policies_[p].id,next.id));
    if(existing)continue;
    // The selected map delegates only the export capability, not unrelated
    // new consumer privileges bundled into the same update. Existing consumer
    // grants were preserved above; require the other direction as well.
    for(size_t g=0;g<next.count;++g)if(next.grants[g].driver!=AppDataExportDriver) {
      const auto& grant=next.grants[g];bool found=false;
      for(size_t p=0;p<policyCount_;++p)if(!strcmp(policies_[p].id,next.id))
        for(size_t h=0;h<policies_[p].count;++h) {
          const auto& old=policies_[p].grants[h];
          found=found || (old.api==grant.api && old.instance==grant.instance && !strcmp(old.capability,grant.capability));
        }
      if(!found)return false;
    }
    size_t selected=count;
    for(size_t i=0;i<count;++i) {
      const auto& delegation=delegations[i];
      if(!delegation.consumer || strnlen(delegation.consumer,sizeof(next.id))>=sizeof(next.id))return false;
      if(strcmp(delegation.consumer,next.id))continue;
      if(selected!=count || consumed[i] || !sameMap(next,delegation.label,delegation.entries,delegation.count))return false;
      selected=i;
    }
    if(selected==count)return false;
    consumed[selected]=true;
  }
  for(size_t i=0;i<count;++i)if(!consumed[i])return false;
  return true;
}
