namespace RiscBoot {
bool Runtime::configureInstallation(JsonVariantConst value){
 if(value.isUnbound())return true;
 if(!value.is<JsonObjectConst>() || !keys(value.as<JsonObjectConst>(),{"volume_instance","grants"},{"namespace_range","provider_grants","settings_sd"}) ||
    !value["grants"].is<JsonArrayConst>() || value["grants"].size()>MaxAppPolicyGrants)return fail("invalid installation envelope");
 int64_t volume=0;if(!integer(value["volume_instance"],1,INT32_MAX,volume))return fail("invalid installation volume");
 bool found=false;for(size_t i=0;i<driverCount_;++i)if(drivers_[i].instance==uint64_t(volume)&&!strcmp(drivers_[i].provides,"storage.volume")&&drivers_[i].api==1)found=true;
 if(!found)return fail("installation volume unavailable");
 int64_t first=0,count=0;
 if(!value["namespace_range"].isUnbound()){
  auto range=value["namespace_range"].as<JsonObjectConst>();
  if(!keys(range,{"first","count"})||!integer(range["first"],1,INT32_MAX,first)||
     !integer(range["count"],1,128,count)||first+count-1>INT32_MAX)return fail("invalid installation namespace range");
 }
 // Private namespaces are templates with instance zero. Only native code maps
 // a durably assigned slot into the boot-reserved range. Shared/provider-bound
 // storage remains explicit authority outside this installation envelope.
 for(JsonObjectConst g:value["grants"].as<JsonArrayConst>()){
  char cap[96];int64_t api=0,instance=0;
  if(!keys(g,{"capability","api","instance_id"})||!text(g["capability"],cap,sizeof(cap))||
     !integer(g["api"],1,UINT32_MAX,api)||!integer(g["instance_id"],0,INT32_MAX,instance)||
     !strncmp(cap,"platform.",9)||(!strncmp(cap,"runtime.",8)&&strcmp(cap,RISC_REALTIME_CAPABILITY))||!strcmp(cap,"hardware.device")||
     !strcmp(cap,"spi.bus")||(!strncmp(cap,"storage.app-data",16)&&strcmp(cap,"storage.app-data"))||
     !strcmp(cap,"storage.shared-data"))return fail("unsupported installation grant envelope");
  if((!strcmp(cap,"storage.key-value")||!strcmp(cap,"storage.app-data"))&&(!count||instance))return fail("private installation namespace template required");
 }
 installation_=metadataArray<InstallationState>(1);if(!installation_)return false;
 if(serializeJson(value["grants"],installation_[0].grants,sizeof(installation_[0].grants))>=sizeof(installation_[0].grants)-1)return false;
 if(!value["provider_grants"].isUnbound()){
  auto allowed=value["provider_grants"];
  if(!allowed.is<JsonArrayConst>()||allowed.size()>32)return fail("invalid provider installation envelope");
  for(JsonObjectConst g:allowed.as<JsonArrayConst>()){
   char cap[96];int64_t api=0,instance=0;
   if(!keys(g,{"capability","api","instance_id"})||!text(g["capability"],cap,sizeof(cap))||
      !integer(g["api"],1,UINT32_MAX,api)||!integer(g["instance_id"],0,INT32_MAX,instance)||
      !strcmp(cap,RISC_INSTALLATION_CAPABILITY)||!strcmp(cap,RISC_BOUND_KEY_VALUE_CAPABILITY)||
      !strcmp(cap,RISC_BOUND_APP_DATA_CAPABILITY))return fail("unsupported provider installation grant");
  }
  if(serializeJson(allowed,installation_[0].providerGrants,sizeof(installation_[0].providerGrants))>=sizeof(installation_[0].providerGrants)-1)return false;
  installation_[0].providersEnabled=true;
 }
 if(!value["settings_sd"].isUnbound()){
  if(!value["settings_sd"].is<bool>())return fail("invalid installed settings selection");
  if(value["settings_sd"].as<bool>()){
   if(!count||!port_.keyValue||!port_.keyValue->journalRead||!port_.keyValue->journalWrite||!port_.keyValue->legacyNamespace)return fail("installed settings journal unavailable");
   for(JsonObjectConst grant:value["grants"].as<JsonArrayConst>())if(grant["capability"]=="storage.key-value"&&grant["api"]!=1)return fail("SD private settings require KV API 1");
   installedSettings_=metadataArray<RiscStorage::SdFirstKeyValue::State>(size_t(count));
   installedSettingsWorkspace_=metadataArray<RiscStorage::SdFirstKeyValue::Workspace>(1);
   if(!installedSettings_||!installedSettingsWorkspace_)return false;
  }
 }
 installation_[0].volume=uint64_t(volume);installation_[0].namespaceFirst=uint32_t(first);installation_[0].namespaceCount=uint32_t(count);
 installation_[0].api={1,sizeof(risc_installation_v1),this,installationAdmit,installationForget,installationCommit,installationCheck,uint32_t(count),installationNamespaced,installationProviders,installationAvailable,uint32_t(installation_[0].providersEnabled?MaxDrivers-bootDriverCount_:0),uint32_t(installedSettings_?count:0),installationSettingsStatus,installationSettingsRestore,installationSettingsSnapshot,installationSettingsAck};
 return registerPlatform(RISC_INSTALLATION_CAPABILITY,1,Scope::Global,0,&installation_[0].api);
}
bool Runtime::installationNamespaceRangeSafe()const{
 if(!installation_||!installation_[0].namespaceCount)return true;
 const auto&s=installation_[0];auto overlaps=[&](uint64_t n){return n>=s.namespaceFirst&&n<uint64_t(s.namespaceFirst)+s.namespaceCount;};
 for(size_t p=0;p<policyCount_;++p)for(size_t g=0;g<policies_[p].count;++g){const auto&grant=policies_[p].grants[g];
  if((grant.keyValue||grant.driver==AppDataDriver||grant.driver==SharedDataDriver)&&overlaps(grant.instance))return false;
 }
 for(const auto&provider:providerStorage_){
  for(size_t i=0;i<provider.count;++i)if(overlaps(provider.keys[i].nameSpace))return false;
  for(size_t i=0;i<provider.fileCount;++i)if(overlaps(provider.files[i].nameSpace))return false;
 }
 return true;
}
bool Runtime::installationMutable(size_t i)const{
 if(i>=policyCount_)return false;
 const auto*p=&policies_[i];
 return (!mainInvocation_.active_ || mainInvocation_.appPolicy_!=p) && (!foreground_ || foreground_->appPolicy_!=p || !foreground_->active_);
}
int32_t Runtime::installationAdmit(void*ctx,const char*relative,const char*json,char*out,uint32_t cap){
 auto*r=static_cast<Runtime*>(ctx);
 if(!r||r!=currentRuntime||!r->installation_||!r->active()||r->retained_||!r->port_.owner()||
    r->graph_.lifecycleBusy()||!r->providerStorageSafe()||!relative||!json||!out||cap<2)return RISC_INSTALL_DENIED;
 if(r->installation_[0].call)return RISC_INSTALL_BUSY;
 if(strnlen(relative,193)>192||strnlen(json,4097)>4096)return RISC_INSTALL_INVALID;
 // Exact managed-directory manifest only; grants and volume never come from SD.
 const char*slash=strrchr(relative,'/');
 if(strncmp(relative,"Apps/",5)||!slash||slash<=relative+5||strcmp(slash,"/manifest.json")||
    !InstalledFiles::valid(relative,false))return RISC_INSTALL_INVALID;
 JsonDocument manifest,envelope,policy;
 if(deserializeJson(manifest,json)||deserializeJson(envelope,r->installation_[0].grants))return RISC_INSTALL_INVALID;
 char id[96],artifact[128];if(!text(manifest["id"],id,sizeof(id))||!text(manifest["file_name"],artifact,sizeof(artifact)))return RISC_INSTALL_INVALID;
 const auto mode=manifest["runtime_lifecycle"];
 if(!mode.isUnbound()&&!eq(mode,"foreground")&&!eq(mode,"legacy"))return RISC_INSTALL_INVALID;
 std::string image(relative,size_t(slash-relative)+1);image+=artifact;
 if(image.size()>=cap||image.size()>=128)return RISC_INSTALL_INVALID;
 policy["manifest"]=relative;policy["image_source"]["volume_instance"]=r->installation_[0].volume;
 policy["image_source"]["path"]=image;auto grants=policy["grants"].to<JsonArray>();
 if(!manifest["requires"].is<JsonArrayConst>())return RISC_INSTALL_INVALID;
 // Immutable boot reservations preserve a legacy identity's exact grants,
 // including multiple shared/private KV namespaces and named shared files.
 // They are never launchable and never confer authority to another identity.
 const AppPolicy* authority=nullptr;
 for(size_t i=0;i<r->bootPolicyCount_;++i)if(r->policies_[i].catalogAuthority&&!strcmp(r->policies_[i].id,id))authority=&r->policies_[i];
 if(authority){
  if(authority->sourceDriver<0 || r->drivers_[authority->sourceDriver].instance!=r->installation_[0].volume)return RISC_INSTALL_DENIED;
  // A migrated identity remains pinned to its original managed directory.
  const char* oldSlash=strrchr(authority->sourcePath,'/');
  if(!oldSlash || size_t(oldSlash-authority->sourcePath)!=size_t(slash-relative)+1 ||
     strncmp(authority->sourcePath+1,relative,size_t(slash-relative)))return RISC_INSTALL_DENIED;
 }
 for(JsonObjectConst requirement:manifest["requires"].as<JsonArrayConst>()){
  unsigned matches=0;
  if(authority){
   for(size_t g=0;g<authority->count;++g){const auto& allowed=authority->grants[g];
    if(requirement["capability"]!=allowed.capability || requirement["api"]!=allowed.api)continue;
    auto grant=grants.add<JsonObject>();grant["capability"]=allowed.capability;grant["api"]=allowed.api;grant["instance_id"]=allowed.instance;
    if(allowed.driver==SharedDataDriver)grant["file"]=allowed.sharedFile;
    ++matches;
   }
   if(!matches)return RISC_INSTALL_DENIED;
  }else{
   for(JsonObjectConst allowed:envelope.as<JsonArrayConst>())
    if(requirement["capability"]==allowed["capability"]&&requirement["api"]==allowed["api"]){
     auto grant=grants.add<JsonObject>();grant.set(allowed);
     if(eq(allowed["capability"],"storage.key-value")||eq(allowed["capability"],"storage.app-data")){
      const auto&s=r->installation_[0];if(!s.namespaceSlot||s.namespaceSlot>s.namespaceCount)return RISC_INSTALL_DENIED;
      grant["instance_id"]=s.namespaceFirst+s.namespaceSlot-1;
     }
     ++matches;
    }
   if(!matches && r->installation_[0].providersEnabled){
    for(size_t d=r->bootDriverCount_;d<r->driverCount_;++d){const auto& provider=r->drivers_[d];
     if(requirement["capability"]!=provider.provides || requirement["api"]!=provider.api)continue;
     auto grant=grants.add<JsonObject>();grant["capability"]=provider.provides;grant["api"]=provider.api;grant["instance_id"]=provider.instance;++matches;
    }
   }
   if(matches!=1)return RISC_INSTALL_DENIED;
  }
 }
 const uint32_t namespaceSlot=r->installation_[0].namespaceSlot;
 for(size_t i=r->bootPolicyCount_;namespaceSlot&&i<r->policyCount_;++i)
  if(r->installedMetadata_[i].namespaceSlot==namespaceSlot&&strcmp(r->policies_[i].id,id))return RISC_INSTALL_DENIED;
 size_t existing=r->policyCount_;
 for(size_t i=0;i<r->policyCount_;++i)if(!r->policies_[i].catalogAuthority&&!strcmp(r->policies_[i].id,id)){existing=i;break;}
 if(existing<r->policyCount_){
  auto&old=r->policies_[existing];
  if(existing>=r->bootPolicyCount_&&r->installedMetadata_[existing].namespaceSlot&&r->installedMetadata_[existing].namespaceSlot!=namespaceSlot)return RISC_INSTALL_DENIED;
  // A discovered identity cannot redirect or override a bootstrap entry.
  if(existing<r->bootPolicyCount_){
   if(old.sourceDriver<0||strcmp(old.sourcePath+1,image.c_str()))return RISC_INSTALL_DENIED;
   const char*launch=old.elf+strlen(r->root_)+1;if(strlen(launch)>=cap)return RISC_INSTALL_INVALID;
   strcpy(out,launch);return RISC_INSTALL_OK;
  }
  if(!strcmp(old.sourcePath+1,image.c_str()) && !strcmp(r->installedMetadata_[existing].manifest,json) && old.enabled){
   const char*launch=old.elf+strlen(r->root_)+1;if(strlen(launch)>=cap)return RISC_INSTALL_INVALID;
   strcpy(out,launch);return RISC_INSTALL_OK;
  }
  if(!r->installationMutable(existing))return RISC_INSTALL_BUSY;
 }
 // Stable retired slots can be reused only after both invocation owners have
 // left them. Replacement validates in-place with exact rollback; no extra
 // capacity is consumed merely to update an already installed identity.
 size_t destination=existing;
 if(existing==r->policyCount_)for(size_t i=r->bootPolicyCount_;i<r->policyCount_;++i)
  if(!r->policies_[i].enabled&&r->installationMutable(i)){destination=i;break;}
 if(destination>=r->policyCapacity_)return RISC_INSTALL_FULL;
 std::string encoded;serializeJson(policy,encoded);r->installation_[0].call=true;
 const bool ok=r->admitInstalledAppAt(encoded.c_str(),json,destination);r->installation_[0].call=false;
 if(!ok)return RISC_INSTALL_INVALID;
 r->installedMetadata_[destination].namespaceSlot=namespaceSlot;
 r->residentForeground_[destination]=eq(mode,"foreground");r->residentLegacy_[destination]=!r->residentForeground_[destination];
 strcpy(out,image.c_str());return RISC_INSTALL_OK;
}
int32_t Runtime::installationForget(void*ctx,const char*relative,bool checkOnly){
 auto*r=static_cast<Runtime*>(ctx);
 if(!r||r!=currentRuntime||!r->installation_||!r->active()||!r->port_.owner()||r->retained_||
    r->graph_.lifecycleBusy()||!relative||strnlen(relative,193)>192)return RISC_INSTALL_DENIED;
 char absolute[256];if(!path(r->root_,relative,absolute,sizeof(absolute)))return RISC_INSTALL_INVALID;
 for(size_t i=r->bootPolicyCount_;i<r->policyCount_;++i)if(!strcmp(r->policies_[i].elf,absolute)){
  if(!r->installationMutable(i))return RISC_INSTALL_BUSY;
  if(!checkOnly)r->policies_[i].enabled=false;
  return RISC_INSTALL_OK;
 }
 return RISC_INSTALL_DENIED;
}
}

namespace RiscBoot {
int32_t Runtime::installationCommit(void*ctx,const risc_install_candidate_v1*items,uint32_t count,risc_install_result_v1*out,bool replace){
 return installationTransaction(ctx,items,count,out,replace,false);
}
int32_t Runtime::installationCheck(void*ctx,const risc_install_candidate_v1*items,uint32_t count,risc_install_result_v1*out,bool replace){
 return installationTransaction(ctx,items,count,out,replace,true);
}
int32_t Runtime::installationNamespaced(void*ctx,const risc_install_candidate_v1*items,const uint32_t*slots,uint32_t count,risc_install_result_v1*out,bool replace,bool check){
 if(count&&!slots)return RISC_INSTALL_INVALID;
 return installationTransaction(ctx,items,count,out,replace,check,slots);
}
int32_t Runtime::installationTransaction(void*ctx,const risc_install_candidate_v1*items,uint32_t count,risc_install_result_v1*out,bool replace,bool check,const uint32_t*slots){
 auto*r=static_cast<Runtime*>(ctx);
 if(!r||r!=currentRuntime||!r->installation_||!r->active()||!r->port_.owner()||r->retained_||
    r->graph_.lifecycleBusy()||count>64||(count&&(!items||!out)))return RISC_INSTALL_DENIED;
 for(uint32_t i=0;i<count;++i){
  if(!items[i].manifest_path||!items[i].manifest_json||(slots&&slots[i]>r->installation_[0].namespaceCount))return RISC_INSTALL_INVALID;
  JsonDocument a;if(deserializeJson(a,items[i].manifest_json))return RISC_INSTALL_INVALID;
  for(uint32_t j=0;j<i;++j){
   JsonDocument b;if(deserializeJson(b,items[j].manifest_json))return RISC_INSTALL_INVALID;
   if((slots&&slots[i]&&slots[i]==slots[j])||!strcmp(items[i].manifest_path,items[j].manifest_path)||a["id"]==b["id"])return RISC_INSTALL_INVALID;
  }
 }
 auto policies=metadataArray<AppPolicy>(r->policyCapacity_);
 auto metadata=metadataArray<InstalledAppMetadata>(r->policyCapacity_);
 auto handlers=metadataArray<FileOpenMetadata>(r->policyCapacity_);
 if(!policies||!metadata||!handlers)return RISC_INSTALL_FULL;
 const size_t before=r->policyCount_;bool foreground[MaxAppPolicies],legacy[MaxAppPolicies];
 for(size_t i=0;i<r->policyCapacity_;++i){policies[i]=r->policies_[i];metadata[i]=r->installedMetadata_[i];handlers[i]=r->fileHandlers_[i];foreground[i]=r->residentForeground_[i];legacy[i]=r->residentLegacy_[i];}
 int32_t status=RISC_INSTALL_OK;
 // Retire absent identities inside the saved transaction before admitting new
 // ones, so a full catalog can atomically replace an old app with a new app.
 if(replace)for(size_t i=r->bootPolicyCount_;i<r->policyCount_;++i){
  bool found=false;for(uint32_t j=0;j<count;++j){JsonDocument m;deserializeJson(m,items[j].manifest_json);found=found||eq(m["id"],r->policies_[i].id);}
  if(!found&&r->policies_[i].enabled){
   if(!r->installationMutable(i)){status=RISC_INSTALL_BUSY;break;}
   r->policies_[i].enabled=false;
  }
 }
 for(uint32_t i=0;status==RISC_INSTALL_OK&&i<count;++i){r->installation_[0].namespaceSlot=slots?slots[i]:0;out[i]={};out[i].status=installationAdmit(ctx,items[i].manifest_path,items[i].manifest_json,out[i].launch_path,sizeof(out[i].launch_path));
  if(out[i].status==RISC_INSTALL_BUSY||out[i].status==RISC_INSTALL_FULL){status=out[i].status;break;}
 }
 r->installation_[0].namespaceSlot=0;
 if(status==RISC_INSTALL_OK&&replace)for(size_t i=r->bootPolicyCount_;i<r->policyCount_;++i){
  bool found=false;const char*path=r->policies_[i].elf+strlen(r->root_)+1;
  for(uint32_t j=0;j<count;++j)found=found||(out[j].status==RISC_INSTALL_OK&&!strcmp(path,out[j].launch_path));
  if(!found&&r->policies_[i].enabled){if(!r->installationMutable(i)){status=RISC_INSTALL_BUSY;break;}r->policies_[i].enabled=false;}
 }
 if(check||status!=RISC_INSTALL_OK){
  for(size_t i=0;i<r->policyCapacity_;++i){r->policies_[i]=policies[i];r->installedMetadata_[i]=metadata[i];r->fileHandlers_[i]=handlers[i];r->residentForeground_[i]=foreground[i];r->residentLegacy_[i]=legacy[i];}
  r->policyCount_=before;if(status!=RISC_INSTALL_OK)for(uint32_t i=0;i<count;++i){out[i]={};out[i].status=status;}
 }
 return status;
}
}

#include "InstallationProvidersRuntime.inc"

#include "InstallationSettingsRuntime.inc"
