namespace {
bool cohortSameJson(JsonVariantConst a,JsonVariantConst b){
  // ArduinoJson performs recursive object/array/value comparison without
  // allocating serialized copies. Both documents also pass the strict schema.
  return a==b;
}
struct CohortName {char value[193]{};bool seen=false;};
bool cohortInventory(RuntimeFileView source,const char* root,CohortName* names,size_t count,
                     bool (*checkpoint)(void*),void* context){
  // Keep one provider cursor at a time. The existing depth-ten admission must
  // not consume eleven native handles (providers may have fewer available).
  struct Pending {uint16_t name,prefix;uint8_t depth;};
  auto pending=metadataArray<Pending>(256);if(!pending)return false;
  size_t head=0,tail=1,objects=0;pending[0]={0,0,0};
  while(head<tail){
    const auto current=pending[head++];if(current.depth>10 || !checkpoint(context))return false;
    char relative[193]{},directory[256];
    if(current.prefix){memcpy(relative,names[current.name].value,current.prefix);relative[current.prefix]=0;}
    if(!*relative){if(strlen(root)>=sizeof(directory))return false;strcpy(directory,root);}
    else if(!path(root,relative,directory,sizeof(directory)))return false;
    uint32_t dir=0;if(source.dirOpen(directory,&dir)!=RISC_FILES_OK)return false;
    bool good=true;
    for(;;){
      risc_storage_files_stat_v1 item{};item.struct_size=sizeof(item);char entryName[193]{};uint32_t required=0;
      const int32_t rc=source.directoryNext(dir,&item,entryName,sizeof(entryName),&required);
      if(rc==RISC_FILES_END)break;
      if(rc!=RISC_FILES_OK){good=false;break;}
      if(!strcmp(entryName,".") || !strcmp(entryName,".."))continue;
      if(++objects>256 || !checkpoint(context)){good=false;break;}
      char name[193],filename[256];int n=snprintf(name,sizeof(name),"%s%s%s",relative,*relative?"/":"",entryName);
      if(n<=0 || size_t(n)>=sizeof(name) || !path(root,name,filename,sizeof(filename))){good=false;break;}
      if(item.flags&RISC_FILES_DIRECTORY){
        size_t i=0;for(;i<count;++i)if(!strncmp(names[i].value,name,size_t(n)) && names[i].value[n]=='/')break;
        if(i==count || tail>=256 || current.depth>=10){good=false;break;}
        pending[tail++]={uint16_t(i),uint16_t(n),uint8_t(current.depth+1)};
      }else{
        size_t i=0;for(;i<count;++i)if(!strcmp(names[i].value,name))break;
        if(i==count || names[i].seen || !item.size){good=false;break;}
        names[i].seen=true;
      }
    }
    if(source.dirClose(dir)!=RISC_FILES_OK)good=false;
    if(!good || source.retained())return false;
  }
  return true;
}
}
bool Runtime::validateCohort(Runtime& candidate,const char* root,
                            bool (*admit)(void*,const char*,bool),void* context,
                            const AppDataExportDelegation* delegations,size_t delegationCount) const {
  return validateCohortImpl(candidate,root,admit,context,delegations,delegationCount,nullptr);
}
bool Runtime::validateNativeCohort(Runtime& candidate,const char* root,
                                  bool (*admit)(void*,const char*,bool),void* context,
                                  const RiscUpdate::NativeAppDataExportPolicyV1& policy) const {
  return validateCohortImpl(candidate,root,admit,context,nullptr,0,&policy);
}
bool Runtime::validateNativeCohort(Runtime& candidate,const char* root,
                                  bool (*admit)(void*,const char*,bool),void* context,
                                  const RiscUpdate::NativeAppDataExportPolicyV2& policy) const {
  return validateCohortImpl(candidate,root,admit,context,nullptr,0,nullptr,&policy);
}
bool Runtime::validateCohortImpl(Runtime& candidate,const char* root,
                                bool (*admit)(void*,const char*,bool),void* context,
                                const AppDataExportDelegation* delegations,size_t delegationCount,
                                const RiscUpdate::NativeAppDataExportPolicyV1* nativePolicy,
                                const RiscUpdate::NativeAppDataExportPolicyV2* nativeV2) const {
  if(&candidate==this || !attempted_ || !port_.owner() || !providerStorageSafe() || !root || !admit || candidate.attempted_ || candidate.metadataCloseRetained())return false;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  // Staged authority comes only from this trusted source Runtime. A preseeded
  // candidate must not mix its own endpoints or writer identities with ours.
  if(candidate.atomicBindingCount_)return false;
#endif
  if(candidate.port_.files && candidate.port_.files!=port_.files)return false;
  candidate.port_.files=port_.files;
  if(!metadataSource(root).bound())return false;
  // All metadata opened for this admission, including source comparisons,
  // belongs to the candidate's native session. Keep its close failure visible
  // even when validation stops before candidate.prepare() is reached.
  JsonDocument currentBoot,newBoot,currentBoard,newBoard;
  char filename[256],relative[193];
  if(!path(root_,"boot.json",filename,sizeof(filename)) || !readJson(metadataSource(root_),filename,currentBoot,&candidate.metadataCloseRetained_) ||
     !path(root,"boot.json",filename,sizeof(filename)) || !readJson(metadataSource(root),filename,newBoot,&candidate.metadataCloseRetained_) ||
     !cohortSameJson(currentBoot["port"],newBoot["port"]))return false;
  if(!text(currentBoot["board"],relative,sizeof(relative)) || !path(root_,relative,filename,sizeof(filename)) || !readJson(metadataSource(root_),filename,currentBoard,&candidate.metadataCloseRetained_) ||
     !text(newBoot["board"],relative,sizeof(relative)) || !path(root,relative,filename,sizeof(filename)) || !readJson(metadataSource(root),filename,newBoard,&candidate.metadataCloseRetained_) ||
     !cohortSameJson(currentBoard.as<JsonVariantConst>(),newBoard.as<JsonVariantConst>()))return false;
  JsonVariantConst migration=newBoot["cohort_migration"];
  if(!RiscUpdate::validCohortMigration(migration))return false;
  bool migrating=false;
  JsonArrayConst shared=migration["shared_key_value"];
  if(!migration.isUnbound()){
    RiscUpdate::CohortIdentity currentIdentity{},nextIdentity{};
    if(!RiscUpdate::readCohort(metadataSource(),root_,currentIdentity,&candidate.metadataCloseRetained_) || !RiscUpdate::readCohort(metadataSource(root),root,nextIdentity,&candidate.metadataCloseRetained_) ||
       strcmp(currentIdentity.product.source_repo,nextIdentity.product.source_repo) ||
       !RiscUpdate::migrationTarget(migration["to"],nextIdentity))return false;
    migrating=RiscUpdate::migrationTarget(migration["from"],currentIdentity) &&
      eq(migration["from"]["source_revision"],currentIdentity.product.source_revision);
    // A retained source-bound record is inert only for exact self-validation
    // of the already installed target. It never grants authority a second time.
    if(!migrating && !(RiscUpdate::migrationTarget(migration["to"],currentIdentity) &&
       RiscUpdate::sameCohortIdentity(currentIdentity,nextIdentity)))return false;
  }
  // Dynamic namespace identities live in the protected catalog ledger. A
  // preserving firmware update must keep its native slot-to-namespace mapping;
  // removing the range would permit a later unrelated owner to reclaim it.
  if(installation_&&installation_[0].namespaceCount&&
     !cohortSameJson(currentBoot["installation"]["namespace_range"],newBoot["installation"]["namespace_range"]))return false;
  if(installation_&&installation_[0].providersEnabled&&
     !cohortSameJson(currentBoot["installation"]["provider_grants"],newBoot["installation"]["provider_grants"]))return false;
  if((installedSettings_||scalableSettings_)&&newBoot["installation"]["settings_sd"]!=true)return false;
  // Once shared namespaces are checkpoint-backed, preserving updates may not
  // silently detach them and revive stale native originals.
  if(sharedSettings_.count&&!cohortSameJson(currentBoot["installation"]["shared_settings"],newBoot["installation"]["shared_settings"]))return false;
  bool consumed[RiscLimits::Apps]{};
  // Reuse the running graph's already scoped native tables with an identical
  // hardware/port declaration. Never bind the staged board into the CPU owner.
  candidate.port_=port_;candidate.port_.bindPlatforms=nullptr;
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
  // Re-register exact permanent native endpoints and RAM-only availability
  // callbacks. The registration guards reject pending/retained custody. Never
  // rebind the native lifecycle or copy active grants/invocation generations.
  for(size_t i=0;i<atomicBindingCount_;++i){const auto& b=atomicBindings_[i];
    if(!b.endpoint || !(b.app[0]
        ?candidate.registerAtomicKeyValue(b.app,b.nameSpace,*b.endpoint)
        :candidate.registerAtomicKeyValueReadThrough(b.nameSpace,*b.endpoint)))return false;
    if(b.availability.snapshot &&
       !candidate.registerKeyValueAvailability(b.nameSpace,b.availability))return false;
  }
#endif
  if(realtimeBackend_ && !candidate.registerRealtime(realtimeBackend_))return false;
  if(tcpBackend_ && !candidate.registerTcpListener(tcpBackend_))return false;
  if(entropyBackend_ && !candidate.registerEntropy(entropyBackend_))return false;
  for(size_t i=0;i<platformCount_;++i){const auto& p=platforms_[i];
    // Rebuild this Runtime-owned descriptor/leases against the copied native
    // backend; never borrow a source Runtime's mutable generation tables.
    if(!strcmp(p.capability,RISC_PLATFORM_REALTIME_CAPABILITY) || !strcmp(p.capability,RISC_TCP_LISTENER_CAPABILITY) || !strcmp(p.capability,RISC_ENTROPY_CAPABILITY) || !strcmp(p.capability,RISC_INSTALLATION_CAPABILITY))continue;
    if(!candidate.registerPlatform(p.capability,p.api,p.scope,p.id,p.table))return false;
  }
  if(!candidate.prepare(root))return false;
  // A native-first bridge and later preserving cohorts do not consume an
  // unused delegation, even while the selected native image remains installed.
  // First introduction alone can select this source-bound authority.
  if(nativePolicy && needsAppDataExportDelegation(candidate)) {
    if(!nativePolicy->delegations || !nativePolicy->count || nativePolicy->count>MaxAppPolicies)return false;
    auto matches=[](const RiscUpdate::NativeAppDataExportCohortV1& selected,
                    const RiscUpdate::CohortIdentity& actual) {
      auto exact=[](const char* value,const char* expected,size_t bound) {
        return value && strnlen(value,bound)<bound && !strcmp(value,expected);
      };
      return exact(selected.product,actual.product.product,sizeof(actual.product.product)) &&
        exact(selected.version,actual.product.version,sizeof(actual.product.version)) &&
        exact(selected.sourceRepo,actual.product.source_repo,sizeof(actual.product.source_repo)) &&
        exact(selected.sourceRevision,actual.product.source_revision,sizeof(actual.product.source_revision)) &&
        exact(selected.runtimeVersion,actual.runtimeVersion,sizeof(actual.runtimeVersion));
    };
    RiscUpdate::CohortIdentity currentIdentity{},nextIdentity{};
    if(!RiscUpdate::readCohort(metadataSource(),root_,currentIdentity,&candidate.metadataCloseRetained_) ||
       !RiscUpdate::readCohort(metadataSource(root),root,nextIdentity,&candidate.metadataCloseRetained_) ||
       !matches(nativePolicy->from,currentIdentity) || !matches(nativePolicy->to,nextIdentity))return false;
    delegations=nativePolicy->delegations;delegationCount=nativePolicy->count;
  }
  bool exactExportMigration=false;
  bool nativeKvConsumed[MaxAppPolicies]{};
  if(nativeV2 && nativeV2->migrationId &&
     !cohortSameJson(currentBoot["app_capabilities"],newBoot["app_capabilities"])) {
    if(nativeV2->schemaVersion!=2 || !*nativeV2->migrationId ||
       strnlen(nativeV2->migrationId,96)>=96 || !nativeV2->before || !nativeV2->after ||
       nativeV2->sharedKeyValueCount>MaxAppPolicies ||
       (nativeV2->sharedKeyValueCount && !nativeV2->sharedKeyValue))return false;
    auto matches=[](const RiscUpdate::NativeAppDataExportCohortV1& selected,
                    const RiscUpdate::CohortIdentity& actual) {
      auto exact=[](const char* value,const char* expected,size_t bound) {
        return value && strnlen(value,bound)<bound && !strcmp(value,expected);
      };
      return exact(selected.product,actual.product.product,sizeof(actual.product.product)) &&
        exact(selected.version,actual.product.version,sizeof(actual.product.version)) &&
        exact(selected.sourceRepo,actual.product.source_repo,sizeof(actual.product.source_repo)) &&
        exact(selected.sourceRevision,actual.product.source_revision,sizeof(actual.product.source_revision)) &&
        exact(selected.runtimeVersion,actual.runtimeVersion,sizeof(actual.runtimeVersion));
    };
    RiscUpdate::CohortIdentity sourceIdentity{},targetIdentity{};
    if(!RiscUpdate::readCohort(metadataSource(),root_,sourceIdentity,&candidate.metadataCloseRetained_) ||
       !RiscUpdate::readCohort(metadataSource(root),root,targetIdentity,&candidate.metadataCloseRetained_))return false;
    if(matches(nativeV2->from,sourceIdentity) && matches(nativeV2->to,targetIdentity)) {
    JsonDocument before,after;
    if(strnlen(nativeV2->before,65536)>=65536 || strnlen(nativeV2->after,65536)>=65536 ||
       deserializeJson(before,nativeV2->before)!=DeserializationError::Ok ||
       deserializeJson(after,nativeV2->after)!=DeserializationError::Ok ||
       !before.is<JsonArray>() || !after.is<JsonArray>() ||
       !cohortSameJson(before.as<JsonVariantConst>(),currentBoot["app_capabilities"]) ||
       !cohortSameJson(after.as<JsonVariantConst>(),newBoot["app_capabilities"]))return false;
    exactExportMigration=true;
    }
  }
  if(!validateAppDataExportCohort(candidate,delegations,delegationCount,exactExportMigration))return false;
#if RISC_RUNTIME_STORAGE_FILES
  // A preserving update cannot silently retarget an already-authorized logical
  // file namespace. Presentation labels may change; its provider identity may not.
  for(size_t p=0;p<policyCount_;++p)for(size_t g=0;g<policies_[p].count;++g){
    const auto& before=policies_[p].grants[g];if(before.driver!=StorageFilesDriver)continue;
    bool found=false;
    for(size_t q=0;q<candidate.policyCount_;++q)if(!strcmp(policies_[p].id,candidate.policies_[q].id))
      for(size_t h=0;h<candidate.policies_[q].count;++h){const auto& after=candidate.policies_[q].grants[h];
        if(after.driver!=StorageFilesDriver || before.instance!=after.instance || before.api!=after.api)continue;
        const auto& a=drivers_[before.platform];const auto& b=candidate.drivers_[after.platform];
        if(a.instance!=b.instance || a.api!=b.api || strcmp(a.id,b.id) || strcmp(a.provides,b.provides))return false;
        found=true;
      }
    if(!found)return false;
  }
#endif
  auto persistent=[](const AppGrantPolicy& g){return keyValuePolicy(g) || g.driver==AppDataDriver;};
  auto same=[](const AppGrantPolicy& a,const AppGrantPolicy& b){
#if RISC_RUNTIME_PREFERENCE_STORE
    // A facade preserves the existing opaque namespace for the same app ID.
    // No new owner, key migration, or backend authority is inferred here.
    if((a.driver==PreferenceStoreDriver || b.driver==PreferenceStoreDriver) && keyValuePolicy(a) && keyValuePolicy(b))return a.instance==b.instance;
#endif
    return a.api==b.api && a.instance==b.instance && !strcmp(a.capability,b.capability);
  };
  // Keep every existing persisted app namespace attached to the same identity.
  // Removing an owner would lose the only durable anti-reassignment record.
  for(size_t p=0;p<policyCount_;++p)for(size_t g=0;g<policies_[p].count;++g)if(persistent(policies_[p].grants[g])){
    if(p>=bootPolicyCount_&&installedMetadata_&&installedMetadata_[p].namespaceSlot&&
       installation_&&installation_[0].namespaceCount)continue;
    bool found=false;
    for(size_t q=0;q<candidate.policyCount_;++q)if(!strcmp(policies_[p].id,candidate.policies_[q].id))
      for(size_t h=0;h<candidate.policies_[q].count;++h)if(same(policies_[p].grants[g],candidate.policies_[q].grants[h]))found=true;
    if(!found)return false;
  }
  auto oldKvUsed=[&](uint32_t ns){
    for(size_t p=0;p<policyCount_;++p)for(size_t g=0;g<policies_[p].count;++g)if(keyValuePolicy(policies_[p].grants[g]) && policies_[p].grants[g].instance==ns)return true;
    for(size_t p=0;p<driverCount_;++p)for(size_t g=0;g<providerStorage_[p].count;++g)if(providerStorage_[p].keys[g].nameSpace==ns)return true;
    return false;
  };
  auto oldFilesUsed=[&](uint32_t ns){
    for(size_t p=0;p<policyCount_;++p)for(size_t g=0;g<policies_[p].count;++g)
      if(policies_[p].grants[g].driver==AppDataDriver && policies_[p].grants[g].instance==ns)return true;
    for(size_t p=0;p<driverCount_;++p)for(size_t g=0;g<providerStorage_[p].fileCount;++g)
      if(providerStorage_[p].files[g].nameSpace==ns)return true;
    return false;
  };
  for(size_t p=0;p<candidate.policyCount_;++p)for(size_t g=0;g<candidate.policies_[p].count;++g){
    const auto& grant=candidate.policies_[p].grants[g];if(!persistent(grant))continue;
    bool used=keyValuePolicy(grant)?oldKvUsed(uint32_t(grant.instance)):oldFilesUsed(uint32_t(grant.instance)),owned=false;
    for(size_t q=0;q<policyCount_;++q)for(size_t h=0;h<policies_[q].count;++h){const auto& previous=policies_[q].grants[h];
      if(!persistent(previous) || (!(keyValuePolicy(previous) && keyValuePolicy(grant)) && strcmp(previous.capability,grant.capability)) || previous.instance!=grant.instance)continue;
      used=true;if(same(previous,grant) && !strcmp(policies_[q].id,candidate.policies_[p].id))owned=true;
    }
    if(used && !owned){
      if(exactExportMigration && keyValuePolicy(grant)) {
        size_t selected=nativeV2->sharedKeyValueCount;
        for(size_t i=0;i<nativeV2->sharedKeyValueCount;++i) {
          const auto& entry=nativeV2->sharedKeyValue[i];
          if(!entry.consumer || strnlen(entry.consumer,96)>=96)return false;
          if(strcmp(entry.consumer,candidate.policies_[p].id) || entry.api!=grant.api || entry.nameSpace!=grant.instance)continue;
          if(selected!=nativeV2->sharedKeyValueCount || nativeKvConsumed[i] ||
             !entry.previousOwners || !entry.previousOwnerCount || entry.previousOwnerCount>MaxAppPolicies)return false;
          bool owners[MaxAppPolicies]{};size_t ownersFound=0;
          for(size_t q=0;q<policyCount_;++q) {
            bool owns=false;
            for(size_t h=0;h<policies_[q].count;++h)
              owns=owns || (keyValuePolicy(policies_[q].grants[h]) && same(policies_[q].grants[h],grant));
            if(!owns)continue;
            size_t match=entry.previousOwnerCount;
            for(size_t j=0;j<entry.previousOwnerCount;++j) {
              if(!entry.previousOwners[j] || strnlen(entry.previousOwners[j],96)>=96)return false;
              if(!strcmp(entry.previousOwners[j],policies_[q].id)) {
                if(match!=entry.previousOwnerCount || owners[j])return false;
                match=j;
              }
            }
            if(match==entry.previousOwnerCount)return false;
            owners[match]=true;++ownersFound;
          }
          if(ownersFound!=entry.previousOwnerCount)return false;
          selected=i;
        }
        if(selected==nativeV2->sharedKeyValueCount)return false;
        nativeKvConsumed[selected]=true;continue;
      }
      if(!migrating || !keyValuePolicy(grant) || policyCount_<2)return false;
      const char* id=candidate.policies_[p].id;
      for(size_t q=0;q<policyCount_;++q){
        if(!strcmp(policies_[q].id,id))return false; // Existing-app expansion is never a migration.
        bool universal=false;
        for(size_t h=0;h<policies_[q].count;++h)
          if(keyValuePolicy(policies_[q].grants[h]) && same(policies_[q].grants[h],grant))universal=true;
        if(!universal)return false;
      }
      size_t entry=0;
      for(;entry<shared.size();++entry)if(eq(shared[entry]["application_id"],id) &&
          shared[entry]["api"].as<uint32_t>()==grant.api && shared[entry]["namespace"].as<uint64_t>()==grant.instance)break;
      if(entry==shared.size() || consumed[entry])return false;
      consumed[entry]=true;
    }
  }
  if(exactExportMigration)for(size_t i=0;i<nativeV2->sharedKeyValueCount;++i)if(!nativeKvConsumed[i])return false;
  if(migrating)for(size_t entry=0;entry<shared.size();++entry)if(!consumed[entry])return false;
  for(size_t p=0;p<driverCount_;++p)for(size_t k=0;k<providerStorage_[p].count;++k){
    const auto& key=providerStorage_[p].keys[k];bool found=false;
    for(size_t q=0;q<candidate.driverCount_;++q)if(!strcmp(drivers_[p].id,candidate.drivers_[q].id) && drivers_[p].instance==candidate.drivers_[q].instance)
      for(size_t h=0;h<candidate.providerStorage_[q].count;++h){const auto& next=candidate.providerStorage_[q].keys[h];
        if(!strcmp(key.key,next.key) && key.nameSpace==next.nameSpace && key.writable==next.writable)found=true;
      }
    if(!found)return false;
  }
  for(size_t p=0;p<candidate.driverCount_;++p)for(size_t k=0;k<candidate.providerStorage_[p].count;++k){
    const auto& key=candidate.providerStorage_[p].keys[k];if(!oldKvUsed(key.nameSpace))continue;
    bool owned=false;
    for(size_t q=0;q<driverCount_;++q)if(!strcmp(candidate.drivers_[p].id,drivers_[q].id) && candidate.drivers_[p].instance==drivers_[q].instance)
      for(size_t h=0;h<providerStorage_[q].count;++h)if(providerStorage_[q].keys[h].nameSpace==key.nameSpace)owned=true;
    if(!owned)return false;
  }
  // Exact file grants carry persistent identity just like app namespaces and
  // provider keys. Fresh deployments may explicitly share app/provider access;
  // a staged update cannot borrow an already owned namespace for a new principal.
  for(size_t p=0;p<driverCount_;++p)for(size_t f=0;f<providerStorage_[p].fileCount;++f){
    const auto& file=providerStorage_[p].files[f];bool found=false;
    for(size_t q=0;q<candidate.driverCount_;++q)
      if(!strcmp(drivers_[p].id,candidate.drivers_[q].id) && drivers_[p].instance==candidate.drivers_[q].instance)
        for(size_t h=0;h<candidate.providerStorage_[q].fileCount;++h){const auto& next=candidate.providerStorage_[q].files[h];
          if(!strcmp(file.name,next.name) && file.nameSpace==next.nameSpace && file.writable==next.writable)found=true;
        }
    if(!found)return false;
  }
  for(size_t p=0;p<candidate.driverCount_;++p)for(size_t f=0;f<candidate.providerStorage_[p].fileCount;++f){
    const auto& file=candidate.providerStorage_[p].files[f];if(!oldFilesUsed(file.nameSpace))continue;
    bool owned=false;
    for(size_t q=0;q<driverCount_;++q)
      if(!strcmp(candidate.drivers_[p].id,drivers_[q].id) && candidate.drivers_[p].instance==drivers_[q].instance)
        for(size_t h=0;h<providerStorage_[q].fileCount;++h)
          if(providerStorage_[q].files[h].nameSpace==file.nameSpace)owned=true;
    if(!owned)return false;
  }
  constexpr size_t maximum=2*MaxAppPolicies+2*MaxDrivers+3;
  auto names=metadataArray<CohortName>(maximum);if(!names)return false;size_t count=0;
  auto add=[&](const char* name){
    char checked[256];if(!path(root,name,checked,sizeof(checked)))return false;
    for(size_t i=0;i<count;++i)if(!strcmp(names[i].value,name))return false;
    if(count==maximum)return false;
    strcpy(names[count++].value,name);return true;
  };
  if(!add("boot.json") || !add("cohort.json") || !add(relative))return false;
  const size_t prefix=strlen(root)+1;bool defaultFound=false;
  for(size_t p=0;p<candidate.policyCount_;++p){
    if(!text(newBoot["app_capabilities"][p]["manifest"],relative,sizeof(relative)) || !add(relative) ||
       (candidate.policies_[p].sourceDriver<0 && (!add(candidate.policies_[p].elf+prefix) || !admit(context,candidate.policies_[p].elf,false))))return false;
    if(!strcmp(candidate.policies_[p].elf,candidate.default_))defaultFound=true;
  }
  if(!defaultFound)return false;
  for(size_t d=0;d<candidate.driverCount_;++d){
    // Hardware instances may intentionally share one package's exact files.
    bool earlier=false;for(size_t e=0;e<d;++e)if(!strcmp(candidate.drivers_[d].elf,candidate.drivers_[e].elf))earlier=true;
    if(earlier)continue;
    if(!text(newBoot["drivers"][d]["manifest"],relative,sizeof(relative)) || !add(relative) ||
       !add(candidate.drivers_[d].elf+prefix) || !admit(context,candidate.drivers_[d].elf,true))return false;
  }
  if(!cohortInventory(metadataSource(root),root,names.get(),count,[](void* p){auto*r=static_cast<const Runtime*>(p);r->port_.delay(1);return r->port_.owner() && r->providerStorageSafe();},const_cast<Runtime*>(this)))return false;
  for(size_t i=0;i<count;++i)if(!names[i].seen)return false;
  return true;
}
