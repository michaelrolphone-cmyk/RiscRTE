namespace {
bool providerFileName(const char* name){
 if(!name || !*name)return false;
 auto alnum=[](unsigned char c){return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9');};
 if(!alnum(*name))return false;
 for(size_t i=0;i<=RISC_APP_DATA_NAME_MAX;++i){
  const char c=name[i];if(!c)return true;
  if(i==RISC_APP_DATA_NAME_MAX || (!alnum(c) && c!='.' && c!='_' && c!='-'))return false;
 }
 return false;
}
}
bool Runtime::providerFilePolicy(JsonObjectConst selection,ProviderStorage& storage){
 const auto value=selection["app_data"];
 if(value.isUnbound())return true;
 if(!value.is<JsonArrayConst>() || !value.size() || value.size()>RISC_BOUND_APP_DATA_BINDINGS_MAX)
  return fail("invalid provider app-data map");
 storage.files=metadataArray<ProviderFile>(value.size());
 if(!storage.files)return fail("provider app-data allocation failed");
 for(JsonObjectConst item:value.as<JsonArrayConst>()){
  auto& entry=storage.files[storage.fileCount];int64_t nameSpace=0;
  if(!keys(item,{"name","namespace","access"}) || !text(item["name"],entry.name,sizeof(entry.name)) ||
     !providerFileName(entry.name) || !integer(item["namespace"],1,INT32_MAX,nameSpace) ||
     (!eq(item["access"],"read") && !eq(item["access"],"read-write")))return fail("invalid provider app-data entry");
  for(size_t i=0;i<storage.fileCount;++i)if(!strcmp(entry.name,storage.files[i].name))return fail("duplicate provider app-data name");
  entry.nameSpace=uint32_t(nameSpace);entry.writable=eq(item["access"],"read-write");++storage.fileCount;
 }
 storage.owner=this;
 storage.fileTable={RISC_BOUND_APP_DATA_API_V1,sizeof(risc_bound_app_data_v1),nullptr,
                    boundAppDataStat,boundAppDataRead,boundAppDataReplace};
 return true;
}
bool Runtime::providerFileSafe(void* context){
 const auto* storage=static_cast<const ProviderStorage*>(context);
 return storage && storage->owner && !storage->owner->providerFileRetained_;
}
int32_t Runtime::providerFileResult(int32_t result){
 if(result==RISC_APP_DATA_RETAINED){
  // Sticky even when a backend's reported safety later changes. No callbacks,
  // stream cleanup, or mapped-image disposal are allowed after this result.
  providerFileRetained_=true;
  (void)appExitBarrier();
 }
 return result;
}
Runtime::ProviderStorage* Runtime::providerFileContext(void* context,int32_t& result){
 result=RISC_APP_DATA_CONTEXT;
 auto* r=currentRuntime;
 if(!r || !context || !r->port_.owner() || !r->port_.appData)return nullptr;
 ProviderStorage* matched=nullptr;
 for(auto& storage:r->providerStorage_)
  if(storage.owner==r && storage.live && storage.fileCount && storage.fileTable.context==context)matched=&storage;
 if(!matched || r->retained_)return nullptr;
 if(!r->appDataExitSafe()){result=r->providerFileResult(RISC_APP_DATA_RETAINED);return nullptr;}
 if(!r->providerStorageSafe()){r->revokeProviders();return nullptr;}
 return matched;
}
int32_t Runtime::boundAppDataStat(void* context,const char* name,uint32_t* size,uint64_t* revision){
 if(size)*size=0;
 if(revision)*revision=0;
 int32_t result;const auto* storage=providerFileContext(context,result);if(!storage)return result;
 if(!size || !revision || !providerFileName(name))return RISC_APP_DATA_INVALID;
 for(size_t i=0;i<storage->fileCount;++i)if(!strcmp(name,storage->files[i].name)){
  auto* r=storage->owner;const auto& b=*r->port_.appData;
  return r->providerFileResult(b.stat(b.context,storage->files[i].nameSpace,name,size,revision));
 }
 return RISC_APP_DATA_CONTEXT;
}
int32_t Runtime::boundAppDataRead(void* context,const char* name,uint64_t expected,void* bytes,uint32_t capacity,uint32_t* size,uint64_t* revision){
 if(size)*size=0;
 if(revision)*revision=0;
 int32_t result;const auto* storage=providerFileContext(context,result);if(!storage)return result;
 if(!size || !revision || (!bytes && capacity) || !providerFileName(name))return RISC_APP_DATA_INVALID;
 for(size_t i=0;i<storage->fileCount;++i)if(!strcmp(name,storage->files[i].name)){
  auto* r=storage->owner;const auto& b=*r->port_.appData;
  return r->providerFileResult(b.read(b.context,storage->files[i].nameSpace,name,expected,bytes,capacity,size,revision));
 }
 return RISC_APP_DATA_CONTEXT;
}
int32_t Runtime::boundAppDataReplace(void* context,const char* name,uint64_t expected,const void* bytes,uint32_t size){
 int32_t result;const auto* storage=providerFileContext(context,result);if(!storage)return result;
 if((!bytes && size) || size>RISC_APP_DATA_FILE_MAX || !providerFileName(name))return RISC_APP_DATA_INVALID;
 for(size_t i=0;i<storage->fileCount;++i)if(!strcmp(name,storage->files[i].name) && storage->files[i].writable){
  auto* r=storage->owner;const auto& b=*r->port_.appData;
  return r->providerFileResult(b.replace(b.context,storage->files[i].nameSpace,name,expected,bytes,size));
 }
 return RISC_APP_DATA_CONTEXT;
}
