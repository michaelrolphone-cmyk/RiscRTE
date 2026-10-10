namespace {
bool cohortSameJson(JsonVariantConst a,JsonVariantConst b){
  // ArduinoJson performs recursive object/array/value comparison without
  // allocating serialized copies. Both documents also pass the strict schema.
  return a==b;
}
struct CohortName {char value[193]{};bool seen=false;};
bool cohortInventory(const char* root,const char* relative,CohortName* names,size_t count,
                     size_t& objects,unsigned depth,bool (*checkpoint)(void*),void* context){
  if(depth>10 || !checkpoint(context))return false;
  char directory[256];
  if(!*relative){if(strlen(root)>=sizeof(directory))return false;strcpy(directory,root);}
  else if(!path(root,relative,directory,sizeof(directory)))return false;
  DIR* dir=opendir(directory);if(!dir)return false;
  bool good=true;
  for(;;){
    errno=0;dirent* item=readdir(dir);if(!item){if(errno)good=false;break;}
    if(!strcmp(item->d_name,".") || !strcmp(item->d_name,".."))continue;
    if(++objects>256 || !checkpoint(context)){good=false;break;}
    char name[193],filename[256];int n=snprintf(name,sizeof(name),"%s%s%s",relative,*relative?"/":"",item->d_name);
    if(n<=0 || size_t(n)>=sizeof(name) || !path(root,name,filename,sizeof(filename))){good=false;break;}
    struct stat info{};
#ifdef ESP_PLATFORM
    const int error=stat(filename,&info); // SPIFFS has no links or special files.
#else
    const int error=lstat(filename,&info);
#endif
    if(error){good=false;break;}
    if(S_ISDIR(info.st_mode)){
      bool prefix=false;for(size_t i=0;i<count;++i)if(!strncmp(names[i].value,name,size_t(n)) && names[i].value[n]=='/')prefix=true;
      if(!prefix || !cohortInventory(root,name,names,count,objects,depth+1,checkpoint,context)){good=false;break;}
    }else if(S_ISREG(info.st_mode)){
      size_t i=0;for(;i<count;++i)if(!strcmp(names[i].value,name))break;
      if(i==count || names[i].seen || info.st_size<=0){good=false;break;}
      names[i].seen=true;
    }else{good=false;break;}
  }
  if(closedir(dir))good=false;
  return good;
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
bool Runtime::validateCohortImpl(Runtime& candidate,const char* root,
                                bool (*admit)(void*,const char*,bool),void* context,
                                const AppDataExportDelegation* delegations,size_t delegationCount,
                                const RiscUpdate::NativeAppDataExportPolicyV1* nativePolicy) const {
  if(&candidate==this || !attempted_ || !port_.owner() || !providerStorageSafe() || !root || !admit || candidate.attempted_ || candidate.metadataCloseRetained_)return false;
  // All metadata opened for this admission, including source comparisons,
  // belongs to the candidate's native session. Keep its close failure visible
  // even when validation stops before candidate.prepare() is reached.
  JsonDocument currentBoot,newBoot,currentBoard,newBoard;
  char filename[256],relative[193];
  if(!path(root_,"boot.json",filename,sizeof(filename)) || !readJson(filename,currentBoot,&candidate.metadataCloseRetained_) ||
     !path(root,"boot.json",filename,sizeof(filename)) || !readJson(filename,newBoot,&candidate.metadataCloseRetained_) ||
     !cohortSameJson(currentBoot["port"],newBoot["port"]))return false;
  if(!text(currentBoot["board"],relative,sizeof(relative)) || !path(root_,relative,filename,sizeof(filename)) || !readJson(filename,currentBoard,&candidate.metadataCloseRetained_) ||
     !text(newBoot["board"],relative,sizeof(relative)) || !path(root,relative,filename,sizeof(filename)) || !readJson(filename,newBoard,&candidate.metadataCloseRetained_) ||
     !cohortSameJson(currentBoard.as<JsonVariantConst>(),newBoard.as<JsonVariantConst>()))return false;
  JsonVariantConst migration=newBoot["cohort_migration"];
  if(!RiscUpdate::validCohortMigration(migration))return false;
  bool migrating=false;
  JsonArrayConst shared=migration["shared_key_value"];
  if(!migration.isUnbound()){
    RiscUpdate::CohortIdentity currentIdentity{},nextIdentity{};
    if(!RiscUpdate::readCohort(root_,currentIdentity,&candidate.metadataCloseRetained_) || !RiscUpdate::readCohort(root,nextIdentity,&candidate.metadataCloseRetained_) ||
       strcmp(currentIdentity.product.source_repo,nextIdentity.product.source_repo) ||
       !RiscUpdate::migrationTarget(migration["to"],nextIdentity))return false;
    migrating=RiscUpdate::migrationTarget(migration["from"],currentIdentity) &&
      eq(migration["from"]["source_revision"],currentIdentity.product.source_revision);
    // A retained source-bound record is inert only for exact self-validation
    // of the already installed target. It never grants authority a second time.
    if(!migrating && !(RiscUpdate::migrationTarget(migration["to"],currentIdentity) &&
       RiscUpdate::sameCohortIdentity(currentIdentity,nextIdentity)))return false;
  }
  uint32_t consumed=0;
  // Reuse the running graph's already scoped native tables with an identical
  // hardware/port declaration. Never bind the staged board into the CPU owner.
  candidate.port_=port_;candidate.port_.bindPlatforms=nullptr;
  if(realtimeBackend_ && !candidate.registerRealtime(realtimeBackend_))return false;
  if(tcpBackend_ && !candidate.registerTcpListener(tcpBackend_))return false;
  if(entropyBackend_ && !candidate.registerEntropy(entropyBackend_))return false;
  for(size_t i=0;i<platformCount_;++i){const auto& p=platforms_[i];
    // Rebuild this Runtime-owned descriptor/leases against the copied native
    // backend; never borrow a source Runtime's mutable generation tables.
    if(!strcmp(p.capability,RISC_PLATFORM_REALTIME_CAPABILITY) || !strcmp(p.capability,RISC_TCP_LISTENER_CAPABILITY) || !strcmp(p.capability,RISC_ENTROPY_CAPABILITY))continue;
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
    if(!RiscUpdate::readCohort(root_,currentIdentity,&candidate.metadataCloseRetained_) ||
       !RiscUpdate::readCohort(root,nextIdentity,&candidate.metadataCloseRetained_) ||
       !matches(nativePolicy->from,currentIdentity) || !matches(nativePolicy->to,nextIdentity))return false;
    delegations=nativePolicy->delegations;delegationCount=nativePolicy->count;
  }
  if(!validateAppDataExportCohort(candidate,delegations,delegationCount))return false;
  auto persistent=[](const AppGrantPolicy& g){return g.keyValue || g.driver==AppDataDriver;};
  auto same=[](const AppGrantPolicy& a,const AppGrantPolicy& b){return a.api==b.api && a.instance==b.instance && !strcmp(a.capability,b.capability);};
  // Keep every existing persisted app namespace attached to the same identity.
  // Removing an owner would lose the only durable anti-reassignment record.
  for(size_t p=0;p<policyCount_;++p)for(size_t g=0;g<policies_[p].count;++g)if(persistent(policies_[p].grants[g])){
    bool found=false;
    for(size_t q=0;q<candidate.policyCount_;++q)if(!strcmp(policies_[p].id,candidate.policies_[q].id))
      for(size_t h=0;h<candidate.policies_[q].count;++h)if(same(policies_[p].grants[g],candidate.policies_[q].grants[h]))found=true;
    if(!found)return false;
  }
  auto oldKvUsed=[&](uint32_t ns){
    for(size_t p=0;p<policyCount_;++p)for(size_t g=0;g<policies_[p].count;++g)if(policies_[p].grants[g].keyValue && policies_[p].grants[g].instance==ns)return true;
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
    bool used=grant.keyValue?oldKvUsed(uint32_t(grant.instance)):oldFilesUsed(uint32_t(grant.instance)),owned=false;
    for(size_t q=0;q<policyCount_;++q)for(size_t h=0;h<policies_[q].count;++h){const auto& previous=policies_[q].grants[h];
      if(!persistent(previous) || strcmp(previous.capability,grant.capability) || previous.instance!=grant.instance)continue;
      used=true;if(same(previous,grant) && !strcmp(policies_[q].id,candidate.policies_[p].id))owned=true;
    }
    if(used && !owned){
      if(!migrating || !grant.keyValue || policyCount_<2)return false;
      const char* id=candidate.policies_[p].id;
      for(size_t q=0;q<policyCount_;++q){
        if(!strcmp(policies_[q].id,id))return false; // Existing-app expansion is never a migration.
        bool universal=false;
        for(size_t h=0;h<policies_[q].count;++h)
          if(policies_[q].grants[h].keyValue && same(policies_[q].grants[h],grant))universal=true;
        if(!universal)return false;
      }
      size_t entry=0;
      for(;entry<shared.size();++entry)if(eq(shared[entry]["application_id"],id) &&
          shared[entry]["api"].as<uint32_t>()==grant.api && shared[entry]["namespace"].as<uint64_t>()==grant.instance)break;
      if(entry==shared.size() || (consumed&(uint32_t(1)<<entry)))return false;
      consumed|=uint32_t(1)<<entry;
    }
  }
  if(migrating && consumed!=((uint32_t(1)<<shared.size())-1))return false;
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
       !add(candidate.policies_[p].elf+prefix) || !admit(context,candidate.policies_[p].elf,false))return false;
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
  size_t objects=0;
  if(!cohortInventory(root,"",names.get(),count,objects,0,[](void* p){auto*r=static_cast<const Runtime*>(p);r->port_.delay(1);return r->port_.owner() && r->providerStorageSafe();},const_cast<Runtime*>(this)))return false;
  for(size_t i=0;i<count;++i)if(!names[i].seen)return false;
  return true;
}
