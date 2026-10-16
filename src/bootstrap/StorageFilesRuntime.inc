// Generic file namespaces are provisioned separately from physical providers.
bool Runtime::storageFilesExitSafe(bool exiting) const {
  if(exiting && storageFilesCalling_)return false;
  const auto safe=[exiting](const Invocation& i){
    for(const auto& g:i.appGrants_)if(g.storageFilesAdapter &&
      (g.storageFilesAdapter->retained() || (exiting && !g.storageFilesAdapter->exitSafe())))return false;
    return true;
  };
  return safe(mainInvocation_) && (!foreground_ || safe(*foreground_));
}
Runtime::AppGrant* Runtime::storageFilesContext(void* context,bool catalog) {
  const auto owner=atomicKeyValueOwner.load(std::memory_order_acquire);if(!owner || !owner())return nullptr;
  auto* r=currentRuntime;
  if(!r || !context || r->storageFilesCalling_ || !r->active() || r->retained_ || r->promotionRunning_ || r->graph_.lifecycleBusy() || !r->invocation_->entryRunning_)return nullptr;
  if(!catalog && !r->providerStorageSafe()){(void)r->appExitBarrier(false);return nullptr;}
  for(auto& g:r->invocation_->appGrants_)if(g.live && g.invocation==r->invocation_->streams_.context() &&
    (catalog?(g.api==&g.storageFilesCatalog && g.storageFilesCatalog.context==context):
      (g.api==&g.storageFiles && g.storageFiles.context==context && g.storageFilesAdapter)))return &g;
  return nullptr;
}
int32_t Runtime::storageFilesResult(int32_t rc) {
  if(rc==RISC_FILES_RETAINED){retained_=true;fail("file namespace custody retained; restart required");(void)appExitBarrier(false);}
  return rc;
}
risc_storage_files_v1 Runtime::storageFilesTable(void* context) {
#define FILES_CALL(method,...) \
  auto* g=storageFilesContext(c);if(!g)return RISC_FILES_CONTEXT; \
  auto* r=currentRuntime;r->storageFilesCalling_=true; \
  struct Call {Runtime* r;~Call(){r->storageFilesCalling_=false;}} call{r}; \
  return r->storageFilesResult(g->storageFilesAdapter->method(__VA_ARGS__))
  return {1,sizeof(risc_storage_files_v1),context,
    [](void* c,const char* path,uint32_t flags,uint32_t* out)->int32_t{if(out)*out=0;FILES_CALL(open,path,flags,out);},
    [](void* c,uint32_t token)->int32_t{FILES_CALL(close,token);},
    [](void* c,uint32_t token,void* out,uint32_t size,uint32_t* n)->int32_t{if(n)*n=0;FILES_CALL(read,token,out,size,n);},
    [](void* c,uint32_t token,const void* data,uint32_t size,uint32_t* n)->int32_t{if(n)*n=0;FILES_CALL(write,token,data,size,n);},
    [](void* c,uint32_t token,uint64_t at)->int32_t{FILES_CALL(seek,token,at);},
    [](void* c,uint32_t token,uint64_t* size,uint64_t* at)->int32_t{FILES_CALL(info,token,size,at);},
    [](void* c,uint32_t token)->int32_t{FILES_CALL(sync,token);},
    [](void* c,uint32_t token,uint64_t size)->int32_t{FILES_CALL(truncate,token,size);},
    [](void* c,const char* path,risc_storage_files_stat_v1* out)->int32_t{FILES_CALL(stat,path,out);},
    [](void* c,const char* path)->int32_t{FILES_CALL(mkdir,path);},
    [](void* c,const char* from,const char* to)->int32_t{FILES_CALL(rename,from,to);},
    [](void* c,const char* path)->int32_t{FILES_CALL(remove,path);},
    [](void* c,const char* path,uint32_t* out)->int32_t{if(out)*out=0;FILES_CALL(dirOpen,path,out);},
    [](void* c,uint32_t token,risc_storage_files_entry_v1* out)->int32_t{FILES_CALL(dirNext,token,out);},
    [](void* c,uint32_t token)->int32_t{FILES_CALL(dirClose,token);},
    [](void* c,uint32_t token)->int32_t{FILES_CALL(dirRewind,token);},
    [](void* c,uint32_t token,uint64_t* out)->int32_t{FILES_CALL(dirTell,token,out);},
    [](void* c,uint32_t token,uint64_t at)->int32_t{FILES_CALL(dirSeek,token,at);}};
#undef FILES_CALL
}
int32_t Runtime::storageFilesSnapshot(void* context,risc_storage_files_catalog_snapshot_v1* out) {
  if(!storageFilesContext(context,true))return RISC_FILES_CONTEXT;
  if(!out || out->struct_size<sizeof(*out))return RISC_FILES_INVALID;
  const auto* policy=currentRuntime->invocation_->appPolicy_;uint32_t count=0;
  for(size_t i=0;i<policy->count;++i)if(policy->grants[i].driver==StorageFilesDriver)++count;
  *out={sizeof(*out),count,uint64_t(reinterpret_cast<uintptr_t>(context))};return RISC_FILES_OK;
}
int32_t Runtime::storageFilesGet(void* context,uint64_t revision,uint32_t index,risc_storage_files_namespace_v1* out) {
  if(!storageFilesContext(context,true))return RISC_FILES_CONTEXT;
  if(!out || out->struct_size<sizeof(*out) || !revision)return RISC_FILES_INVALID;
  if(revision!=uint64_t(reinterpret_cast<uintptr_t>(context)))return RISC_FILES_STALE;
  const auto* policy=currentRuntime->invocation_->appPolicy_;uint32_t at=0;
  for(size_t i=0;i<policy->count;++i){const auto& p=policy->grants[i];if(p.driver!=StorageFilesDriver)continue;
    if(at++!=index)continue;
    risc_storage_files_namespace_v1 value{};value.struct_size=sizeof(value);value.namespace_id=p.instance;
    static_assert(sizeof(value.label)>=sizeof(p.sharedFile),"File namespace label bound");
    memcpy(value.label,p.sharedFile,sizeof(p.sharedFile));*out=value;return RISC_FILES_OK;
  }
  return RISC_FILES_NOT_FOUND;
}
