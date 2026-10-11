namespace RiscBoot {
bool Runtime::configureInstallation(JsonVariantConst value){
 if(value.isUnbound())return true;
 if(!value.is<JsonObjectConst>() || !keys(value.as<JsonObjectConst>(),{"volume_instance","grants"}) ||
    !value["grants"].is<JsonArrayConst>() || value["grants"].size()>MaxAppPolicyGrants)return fail("invalid installation envelope");
 int64_t volume=0;if(!integer(value["volume_instance"],1,INT32_MAX,volume))return fail("invalid installation volume");
 bool found=false;for(size_t i=0;i<driverCount_;++i)if(drivers_[i].instance==uint64_t(volume)&&!strcmp(drivers_[i].provides,"storage.volume")&&drivers_[i].api==1)found=true;
 if(!found)return fail("installation volume unavailable");
 // Namespace-owning and privileged native capabilities need an independently
 // durable allocation/delegation protocol. They cannot be implicitly shared.
 for(JsonObjectConst g:value["grants"].as<JsonArrayConst>()){
  char cap[96];int64_t api=0,instance=0;
  if(!keys(g,{"capability","api","instance_id"})||!text(g["capability"],cap,sizeof(cap))||
     !integer(g["api"],1,UINT32_MAX,api)||!integer(g["instance_id"],0,INT32_MAX,instance)||
     !strncmp(cap,"platform.",9)||!strncmp(cap,"runtime.",8)||!strcmp(cap,"hardware.device")||
     !strcmp(cap,"spi.bus")||!strcmp(cap,"storage.key-value")||!strncmp(cap,"storage.app-data",16)||
     !strcmp(cap,"storage.shared-data"))return fail("unsupported installation grant envelope");
 }
 installation_=metadataArray<InstallationState>(1);if(!installation_)return false;
 if(serializeJson(value["grants"],installation_[0].grants,sizeof(installation_[0].grants))>=sizeof(installation_[0].grants)-1)return false;
 installation_[0].volume=uint64_t(volume);
 installation_[0].api={1,sizeof(risc_installation_v1),this,installationAdmit,installationForget,installationCommit,installationCheck};
 return registerPlatform(RISC_INSTALLATION_CAPABILITY,1,Scope::Global,0,&installation_[0].api);
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
 for(JsonObjectConst requirement:manifest["requires"].as<JsonArrayConst>()){
  unsigned matches=0;for(JsonObjectConst allowed:envelope.as<JsonArrayConst>())
   if(requirement["capability"]==allowed["capability"]&&requirement["api"]==allowed["api"]){grants.add(allowed);++matches;}
  if(matches!=1)return RISC_INSTALL_DENIED;
 }
 size_t existing=r->policyCount_;
 for(size_t i=0;i<r->policyCount_;++i)if(!strcmp(r->policies_[i].id,id)){existing=i;break;}
 if(existing<r->policyCount_){
  auto&old=r->policies_[existing];
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
int32_t Runtime::installationTransaction(void*ctx,const risc_install_candidate_v1*items,uint32_t count,risc_install_result_v1*out,bool replace,bool check){
 auto*r=static_cast<Runtime*>(ctx);
 if(!r||r!=currentRuntime||!r->installation_||!r->active()||!r->port_.owner()||r->retained_||
    r->graph_.lifecycleBusy()||count>64||(count&&(!items||!out)))return RISC_INSTALL_DENIED;
 for(uint32_t i=0;i<count;++i){
  if(!items[i].manifest_path||!items[i].manifest_json)return RISC_INSTALL_INVALID;
  JsonDocument a;if(deserializeJson(a,items[i].manifest_json))return RISC_INSTALL_INVALID;
  for(uint32_t j=0;j<i;++j){
   JsonDocument b;if(deserializeJson(b,items[j].manifest_json))return RISC_INSTALL_INVALID;
   if(!strcmp(items[i].manifest_path,items[j].manifest_path)||a["id"]==b["id"])return RISC_INSTALL_INVALID;
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
 for(uint32_t i=0;status==RISC_INSTALL_OK&&i<count;++i){out[i]={};out[i].status=installationAdmit(ctx,items[i].manifest_path,items[i].manifest_json,out[i].launch_path,sizeof(out[i].launch_path));
  if(out[i].status==RISC_INSTALL_BUSY||out[i].status==RISC_INSTALL_FULL){status=out[i].status;break;}
 }
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
