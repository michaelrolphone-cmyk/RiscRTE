namespace RiscBoot {
bool Runtime::installationAvailable(void* context,const char* capability,uint32_t api,uint64_t instance){
 auto*r=static_cast<Runtime*>(context);
 if(!r||r!=currentRuntime||!r->installation_||!r->active()||r->retained_||!r->port_.owner()||
    !capability||strnlen(capability,96)>=96||!api)return false;
 unsigned matches=0;
 for(size_t i=0;i<r->driverCount_;++i){const auto&d=r->drivers_[i];
  if(d.api==api&&!strcmp(d.provides,capability)&&(!instance||d.instance==instance))++matches;
 }
 for(size_t i=0;i<r->platformCount_;++i){const auto&p=r->platforms_[i];
  if(p.api==api&&!strcmp(p.capability,capability)&&
     (p.scope==Scope::Global||p.id==instance))++matches;
 }
 return matches==1;
}
int32_t Runtime::installationProviders(void* context,const risc_install_provider_v1* input,uint32_t count,bool check){
 return installationProviderTransaction(context,input,nullptr,count,check);
}
int32_t Runtime::installationProvidersMedia(void*context,const risc_install_provider_v1*input,const uint32_t*media,uint32_t count,bool check){
 if(count&&!media)return RISC_INSTALL_INVALID;
 return installationProviderTransaction(context,input,media,count,check);
}
int32_t Runtime::installationProviderTransaction(void*context,const risc_install_provider_v1*input,const uint32_t*media,uint32_t count,bool check){
 auto*r=static_cast<Runtime*>(context);
 if(!r||r!=currentRuntime||!r->installation_||!r->installation_[0].providersEnabled||
    !r->active()||!r->port_.owner()||r->retained_)return RISC_INSTALL_DENIED;
 if(r->installation_[0].call||r->graph_.lifecycleBusy()||!r->providerStorageSafe())return RISC_INSTALL_BUSY;
 if(count>MaxDrivers-r->bootDriverCount_||(count&&!input))return RISC_INSTALL_FULL;
 if(!count)return RISC_INSTALL_OK;
 auto saved=metadataArray<Driver>(MaxDrivers);
 using StorageSlot=std::aligned_storage_t<sizeof(ProviderStorage),alignof(ProviderStorage)>;
 auto storageMemory=metadataArray<StorageSlot>(MaxDrivers);
 auto specs=metadataArray<RuntimeProviders::SpecV2>(count);
 if(!saved||!storageMemory||!specs)return RISC_INSTALL_FULL;
 auto*storage=reinterpret_cast<ProviderStorage*>(storageMemory.get());
 for(size_t i=0;i<MaxDrivers;++i){new(&storage[i]) ProviderStorage;saved[i]=r->drivers_[i];}
 const size_t before=r->driverCount_;size_t slots[MaxDrivers]{},changed=0;
 bool touched[MaxDrivers]{};
 struct Rollback {
  Runtime*r;Driver*drivers;ProviderStorage*storage;bool*touched;size_t before;bool committed=false;
  ~Rollback(){
   if(!committed){for(size_t i=0;i<MaxDrivers;++i){r->drivers_[i]=drivers[i];if(touched[i])r->providerStorage_[i]=std::move(storage[i]);}r->driverCount_=before;}
   for(size_t i=0;i<MaxDrivers;++i)storage[i].~ProviderStorage();
   r->installation_[0].call=false;
  }
 } rollback{r,saved.get(),storage,touched,before};
 r->installation_[0].call=true;
 JsonDocument permissions;if(deserializeJson(permissions,r->installation_[0].providerGrants))return RISC_INSTALL_INVALID;
 for(uint32_t n=0;n<count;++n){
  if(media&&(media[n]>1||(media[n]==1&&!r->installation_[0].internalEnabled)))return RISC_INSTALL_DENIED;
  const auto&record=input[n];
  if(!record.manifest_json||strnlen(record.manifest_json,4097)>4096||!record.image_path||
     strnlen(record.image_path,193)>192||record.instance>INT32_MAX)return RISC_INSTALL_INVALID;
  JsonDocument document;if(deserializeJson(document,record.manifest_json))return RISC_INSTALL_INVALID;
  char id[96];if(!text(document["id"],id,sizeof(id)))return RISC_INSTALL_INVALID;
  for(const char*p=id;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'||*p=='.'))return RISC_INSTALL_INVALID;
  if(strstr(id,"..")||!strcmp(id,"."))return RISC_INSTALL_INVALID;
  for(uint32_t j=0;j<n;++j){JsonDocument previous;if(deserializeJson(previous,input[j].manifest_json))return RISC_INSTALL_INVALID;
   if(previous["id"]==id&&input[j].instance==record.instance)return RISC_INSTALL_INVALID;
  }
  char prefix[120];const int prefixBytes=snprintf(prefix,sizeof(prefix),"/Drivers/%s/image-",id);
  if(prefixBytes<=0||size_t(prefixBytes)>=sizeof(prefix)||strncmp(record.image_path,prefix,size_t(prefixBytes))||
     strlen(record.image_path)!=size_t(prefixBytes)+68||strcmp(record.image_path+prefixBytes+64,".elf"))return RISC_INSTALL_INVALID;
  for(unsigned i=0;i<64;++i){char c=record.image_path[prefixBytes+i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return RISC_INSTALL_INVALID;}
  size_t slot=r->driverCount_;
  for(size_t i=0;i<r->driverCount_;++i)if(!strcmp(r->drivers_[i].id,id)&&r->drivers_[i].instance==record.instance){slot=i;break;}
  if(slot<r->bootDriverCount_)return RISC_INSTALL_DENIED;
  if(slot==MaxDrivers)return RISC_INSTALL_FULL;
  auto&d=r->drivers_[slot];d={};d.instance=record.instance;
  if(snprintf(d.elf,sizeof(d.elf),"%s/installed-provider/%s/manifest.json",r->root_,id)>=int(sizeof(d.elf))||
     !r->manifest(document.as<JsonObjectConst>(),d))return RISC_INSTALL_INVALID;
  if(!strncmp(d.provides,"platform.",9)||!strncmp(d.provides,"runtime.",8)||
     !strncmp(d.provides,"storage.app-data",16)||!strcmp(d.provides,"storage.key-value")||!strcmp(d.provides,"spi.bus"))return RISC_INSTALL_DENIED;
  strcpy(d.sourcePath,record.image_path);d.sourceOwner=r;d.sourceVolume=media&&media[n]==1?r->installation_[0].internalVolume:r->installation_[0].volume;
  bool same=slot<before&&saved[slot].sourceVolume==d.sourceVolume&&!strcmp(saved[slot].version,d.version)&&!strcmp(saved[slot].sourcePath,d.sourcePath)&&
    !strcmp(saved[slot].provides,d.provides)&&saved[slot].api==d.api&&saved[slot].count==d.count;
  for(size_t j=0;same&&j<d.count;++j)same=saved[slot].requirements[j].api==d.requirements[j].api&&!strcmp(saved[slot].names[j],d.names[j]);
  if(same){d=saved[slot];continue;}
  if(slot<before){
   if(!r->graph_.metadataMutable(slot))return RISC_INSTALL_BUSY;
   uint32_t oldVersion[3],newVersion[3];
   if(strcmp(saved[slot].provides,d.provides)||saved[slot].api!=d.api||
      !RiscUpdate::parseVersion(saved[slot].version,oldVersion)||!RiscUpdate::parseVersion(d.version,newVersion)||
      RiscUpdate::compareVersion(newVersion,oldVersion)<=0)return RISC_INSTALL_DENIED;
  }
  storage[slot]=std::move(r->providerStorage_[slot]);r->providerStorage_[slot]={};touched[slot]=true;
  if(slot==r->driverCount_)++r->driverCount_;
  slots[changed++]=slot;
 }
 if(!r->validateGraph())return RISC_INSTALL_INVALID;
 // Resolved dependencies are allowed by the selected native envelope, or by
 // another independently admitted dynamic provider. No manifest supplies grants.
 for(size_t i=0;i<changed;++i){const auto&d=r->drivers_[slots[i]];
  for(size_t j=0;j<d.count;++j){const auto&need=d.requirements[j];uint64_t wanted=0;bool allowed=false;
   if(!strcmp(need.capability,"hardware.device"))wanted=d.instance;
   else if(need.providerId){wanted=need.providerInstance;
    for(size_t k=r->bootDriverCount_;k<r->driverCount_;++k)if(!strcmp(r->drivers_[k].id,need.providerId)&&r->drivers_[k].instance==wanted)allowed=true;
   }else for(size_t k=0;k<r->platformCount_;++k){const auto&p=r->platforms_[k];
    if(p.api==need.api&&!strcmp(p.capability,need.capability)&&
       (p.scope==Scope::Global||(p.scope==Scope::Device&&p.id==d.instance)||(p.scope==Scope::Bus&&p.id==r->board_.deviceBus(d.instance))))wanted=p.id;
   }
   for(JsonObjectConst grant:permissions.as<JsonArrayConst>())if(grant["capability"]==need.capability&&grant["api"]==need.api&&grant["instance_id"]==wanted)allowed=true;
   if(!allowed)return RISC_INSTALL_DENIED;
  }
  auto& spec=specs[i];spec={d.id,d.elf,d.provides,d.api,d.requirements,d.count};
  spec.hardware=d.instance?&r->board_.device(d.instance)->hardware:nullptr;
  spec.openImage=openDriverImage;spec.imageContext=&r->drivers_[slots[i]];
  spec.imageProvider=r->drivers_[d.sourceDriver].id;spec.imageInstance=d.sourceVolume;
  auto&owned=r->providerStorage_[slots[i]];owned.owner=r;
  spec.lease={&owned,beginProvider,revokeProvider,providerResourcesSafe};
 }
 if(!r->graph_.publishOrdinaryBatch(specs.get(),slots,changed,check))return RISC_INSTALL_BUSY;
 if(!check)rollback.committed=true;
 return RISC_INSTALL_OK;
}
}
