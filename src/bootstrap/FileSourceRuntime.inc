// Runtime-owned source bytes are never borrowed from an app image or provider.
namespace {
bool sourceExtension(const char* name,char (&extension)[FileOpenMetadata::TypeBytes]) {
  if(!name)return false;
  const size_t n=strnlen(name,RISC_FILE_OPEN_SOURCE_NAME_MAX);
  if(!n || n>=RISC_FILE_OPEN_SOURCE_NAME_MAX || !utf8(name,n))return false;
  for(size_t i=0;i<n;++i)if(static_cast<unsigned char>(name[i])<32 || name[i]==127 || name[i]=='/' || name[i]=='\\')return false;
  const char* dot=strrchr(name,'.');
  if(!dot || dot==name)return false;
  const size_t length=strlen(dot);
  if(length<2 || length>=sizeof(extension))return false;
  extension[0]='.';
  for(size_t i=1;i<length;++i) {
    char c=dot[i];if(c>='A' && c<='Z')c=char(c-'A'+'a');
    if(!((c>='a' && c<='z') || (c>='0' && c<='9')))return false;
    extension[i]=c;
  }
  extension[length]=0;return true;
}
}
const risc_file_open_source_api_v1* Runtime::fileOpenSourceApi() {
  static const risc_file_open_source_api_v1 api={1,sizeof(api),
    [](const char* n)->uint32_t{return currentRuntime?currentRuntime->fileSourceHandlerCount(n):0;},
    [](const char* n,uint32_t i,t5_file_handler_t* out){if(out)*out={};return currentRuntime && currentRuntime->fileSourceHandlerGet(n,i,out);},
    [](const char* id){return currentRuntime && currentRuntime->fileSourceSupported(id);},
    [](const char* n,uint64_t s,uint64_t* t)->int32_t{if(t)*t=0;return currentRuntime?currentRuntime->fileSourceBegin(n,s,t):RISC_FILE_SOURCE_CONTEXT;},
    [](uint64_t t,const void* b,uint32_t n,uint32_t* a)->int32_t{if(a)*a=0;return currentRuntime?currentRuntime->fileSourceWrite(t,b,n,a):RISC_FILE_SOURCE_CONTEXT;},
    [](uint64_t t)->int32_t{return currentRuntime?currentRuntime->fileSourceFinish(t):RISC_FILE_SOURCE_CONTEXT;},
    [](uint64_t t)->int32_t{return currentRuntime?currentRuntime->fileSourceDiscard(t):RISC_FILE_SOURCE_CONTEXT;},
    [](uint64_t t,const char* id,uint64_t c){return currentRuntime && currentRuntime->fileSourceRequest(t,id,c);},
    [](int32_t* e,uint64_t* c){return currentRuntime && currentRuntime->fileSourceTakeResult(e,c);},
    [](risc_file_open_source_info_v1* o)->int32_t{return currentRuntime?currentRuntime->fileSourceInfo(o):RISC_FILE_SOURCE_CONTEXT;},
    [](uint64_t t,uint64_t o,void* b,uint32_t n,uint32_t* a)->int32_t{if(a)*a=0;return currentRuntime?currentRuntime->fileSourceRead(t,o,b,n,a):RISC_FILE_SOURCE_CONTEXT;}};
  return &api;
}
bool Runtime::fileSourceExitSafe() const {return !port_.fileSource || (port_.fileSource->exitSafe && port_.fileSource->exitSafe(port_.fileSource->context));}
bool Runtime::fileSourceReady() const {
  if(promotionRunning_ || !active() || !invocation_->appPolicy_ || retained_ || !providerStorageSafe() || !fileSourceExitSafe())return false;
  for(const auto& g:invocation_->appGrants_)if(g.live && g.api==fileOpenSourceApi())return true;
  return false;
}
bool Runtime::fileSourceSupported(size_t i) const {
  if(i>=policyCount_ || !policies_[i].enabled)return false;
  for(size_t g=0;g<policies_[i].count;++g)if(policies_[i].grants[g].fileOpenSource)return true;
  return false;
}
bool Runtime::fileSourceSupported(const char* id) const {
  if(!fileSourceReady() || !id || !*id || strnlen(id,T5_FILE_HANDLER_ID_MAX)>=T5_FILE_HANDLER_ID_MAX)return false;
  for(size_t i=0;i<policyCount_;++i)if(!strcmp(policies_[i].id,id))return fileSourceSupported(i);
  return false;
}
uint32_t Runtime::fileSourceHandlerCount(const char* name) const {
  char extension[FileOpenMetadata::TypeBytes];
  if(!fileSourceReady() || !fileHandlers_ || !sourceExtension(name,extension))return 0;
  uint32_t count=0;for(size_t i=0;i<policyCount_;++i)if(policies_[i].enabled && fileOpenMatches(fileHandlers_[i],extension))++count;
  return count;
}
bool Runtime::fileSourceHandlerGet(const char* name,uint32_t index,t5_file_handler_t* out) const {
  if(out)*out={};
  char extension[FileOpenMetadata::TypeBytes];
  if(!out || !fileSourceReady() || !fileHandlers_ || !sourceExtension(name,extension))return false;
  for(size_t i=0;i<policyCount_;++i)if(policies_[i].enabled && fileOpenMatches(fileHandlers_[i],extension)) {
    if(!index){*out=fileHandlers_[i].handler;return true;}--index;
  }
  return false;
}
int32_t Runtime::fileSourceStatus(int32_t rc) {
  if(rc==RISC_FILE_SOURCE_RETAINED || !fileSourceExitSafe()) {
    retained_=true;fail("file source custody retained; restart required");(void)appExitBarrier(false);return RISC_FILE_SOURCE_RETAINED;
  }
  return rc;
}
bool Runtime::fileSourceOwned(uint64_t token) const {
  return token && token==fileSource_.token && fileSource_.ownerInvocation==invocation_->token &&
    fileSource_.owner>=0 && invocation_->appPolicy_==&policies_[fileSource_.owner] &&
    invocation_->fileOpen_.phase==FileOpenState::Phase::Idle;
}
bool Runtime::fileSourceCleanup() {
  if(!fileSource_.token)return true;
  if(retained_ || !fileSourceExitSafe())return fileSourceStatus(RISC_FILE_SOURCE_RETAINED)==0;
  const int32_t rc=fileSourceStatus(port_.fileSource->discard(port_.fileSource->context));
  if(rc) {
    // Lifecycle cannot lose cleanup ownership or start a fresh app while a
    // source remains. Do not retry a failed native cleanup automatically.
    if(!retained_){retained_=true;fail("file source cleanup retained; restart required");(void)appExitBarrier(false);}
    return false;
  }
  fileSource_={};return true;
}
int32_t Runtime::fileSourceBegin(const char* name,uint64_t size,uint64_t* token) {
  if(token)*token=0;
  if(!fileSourceReady() || !invocation_->entryRunning_)return RISC_FILE_SOURCE_CONTEXT;
  char extension[FileOpenMetadata::TypeBytes];
  if(!token || !sourceExtension(name,extension))return RISC_FILE_SOURCE_INVALID;
  if(fileSource_.token || invocation_->queued_[0] || invocation_->fileOpen_.phase!=FileOpenState::Phase::Idle)return RISC_FILE_SOURCE_BUSY;
  // The resident host cannot own a source while a foreground can replace it.
  if(residentEnabled_ && invocation_->role!=RISC_RESIDENT_ROLE_FOREGROUND &&
     !(invocation_==&mainInvocation_ && !invocation_->role && residentLegacyAllowed(invocation_->current_)))return RISC_FILE_SOURCE_CONTEXT;
  if(fileSourceGeneration_==UINT64_MAX)return RISC_FILE_SOURCE_UNAVAILABLE;
  const int32_t rc=fileSourceStatus(port_.fileSource->begin(port_.fileSource->context,size));
  if(rc)return rc;
  fileSource_.token=++fileSourceGeneration_;fileSource_.size=size;fileSource_.ownerInvocation=invocation_->token;
  fileSource_.owner=static_cast<PolicyIndex>(invocation_->appPolicy_-policies_.get());strcpy(fileSource_.name,name);
  *token=fileSource_.token;return RISC_FILE_SOURCE_OK;
}
int32_t Runtime::fileSourceWrite(uint64_t token,const void* bytes,uint32_t length,uint32_t* actual) {
  if(actual)*actual=0;
  if(!fileSourceReady() || !fileSourceOwned(token))return RISC_FILE_SOURCE_CONTEXT;
  if(!actual || !bytes || !length || length>RISC_FILE_OPEN_SOURCE_IO_MAX || fileSource_.sealed || length>fileSource_.size-fileSource_.written)return RISC_FILE_SOURCE_INVALID;
  uint32_t written=0;int32_t rc=fileSourceStatus(port_.fileSource->write(port_.fileSource->context,bytes,length,&written));
  if(!rc && (!written || written>length))rc=RISC_FILE_SOURCE_IO;
  if(rc) {if(rc!=RISC_FILE_SOURCE_BUSY && rc!=RISC_FILE_SOURCE_RETAINED && !fileSourceCleanup())return RISC_FILE_SOURCE_RETAINED;return rc;}
  fileSource_.written+=written;*actual=written;return RISC_FILE_SOURCE_OK;
}
int32_t Runtime::fileSourceFinish(uint64_t token) {
  if(!fileSourceReady() || !fileSourceOwned(token))return RISC_FILE_SOURCE_CONTEXT;
  if(fileSource_.sealed)return RISC_FILE_SOURCE_INVALID;
  if(fileSource_.written!=fileSource_.size)return RISC_FILE_SOURCE_INCOMPLETE;
  const int32_t rc=fileSourceStatus(port_.fileSource->finish(port_.fileSource->context));
  if(!rc)fileSource_.sealed=true;
  else if(rc!=RISC_FILE_SOURCE_BUSY && rc!=RISC_FILE_SOURCE_RETAINED && !fileSourceCleanup())return RISC_FILE_SOURCE_RETAINED;
  return rc;
}
int32_t Runtime::fileSourceDiscard(uint64_t token) {
  if(!fileSourceReady() || !fileSourceOwned(token))return RISC_FILE_SOURCE_CONTEXT;
  return fileSourceCleanup()?RISC_FILE_SOURCE_OK:RISC_FILE_SOURCE_RETAINED;
}
bool Runtime::fileSourceRequest(uint64_t token,const char* id,uint64_t cookie) {
  if(!fileSourceReady() || !invocation_->entryRunning_ || !fileSourceOwned(token) || !fileSource_.sealed || invocation_->queued_[0] ||
     !fileHandlers_ || !id || !*id || strnlen(id,T5_FILE_HANDLER_ID_MAX)>=T5_FILE_HANDLER_ID_MAX ||
     (port_.appExitSafe && !port_.appExitSafe()))return false;
  char extension[FileOpenMetadata::TypeBytes];if(!sourceExtension(fileSource_.name,extension))return false;
  for(size_t i=0;i<policyCount_;++i) {
    if(!fileSourceSupported(i) || strcmp(policies_[i].id,id) || &policies_[i]==invocation_->appPolicy_ || !fileOpenMatches(fileHandlers_[i],extension))continue;
    if(residentEnabled_ && !residentAllowed(policies_[i].elf) && !residentLegacyAllowed(policies_[i].elf))return false;
    invocation_->fileOpen_.caller=fileSource_.owner;invocation_->fileOpen_.receiver=static_cast<PolicyIndex>(i);
    invocation_->fileOpen_.cookie=cookie;invocation_->fileOpen_.sourceToken=token;
    strcpy(invocation_->queued_,policies_[i].elf);invocation_->fileOpen_.phase=FileOpenState::Phase::Requested;return true;
  }
  return false;
}
bool Runtime::fileSourceTakeResult(int32_t* error,uint64_t* cookie) {
  if(!fileSourceReady() || !invocation_->fileOpen_.sourceToken || invocation_->fileOpen_.phase!=FileOpenState::Phase::Result ||
     invocation_->fileOpen_.caller<0 || invocation_->appPolicy_!=&policies_[invocation_->fileOpen_.caller])return false;
  if(error)*error=invocation_->fileOpen_.result;
  if(cookie)*cookie=invocation_->fileOpen_.cookie;
  invocation_->fileOpen_={};return true;
}
int32_t Runtime::fileSourceInfo(risc_file_open_source_info_v1* out) const {
  if(!out || out->struct_size<sizeof(*out))return RISC_FILE_SOURCE_INVALID;
  *out={};out->struct_size=sizeof(*out);
  if(!fileSourceReady() || !fileSource_.token || !fileSource_.sealed || invocation_->fileOpen_.sourceToken!=fileSource_.token ||
     invocation_->fileOpen_.phase!=FileOpenState::Phase::Receiving || invocation_->fileOpen_.receiver<0 ||
     invocation_->appPolicy_!=&policies_[invocation_->fileOpen_.receiver])return RISC_FILE_SOURCE_CONTEXT;
  strcpy(out->name,fileSource_.name);out->token=fileSource_.token;out->size=fileSource_.size;return RISC_FILE_SOURCE_OK;
}
int32_t Runtime::fileSourceRead(uint64_t token,uint64_t offset,void* bytes,uint32_t capacity,uint32_t* actual) {
  if(actual)*actual=0;
  risc_file_open_source_info_v1 info{};info.struct_size=sizeof(info);
  if(fileSourceInfo(&info)!=RISC_FILE_SOURCE_OK || !token || token!=info.token)return RISC_FILE_SOURCE_CONTEXT;
  if(!actual || (!bytes && capacity) || capacity>RISC_FILE_OPEN_SOURCE_IO_MAX || offset>info.size)return RISC_FILE_SOURCE_INVALID;
  // Even a buggy backend cannot publish partial/failed bytes into an app.
  unsigned char copy[RISC_FILE_OPEN_SOURCE_IO_MAX];uint32_t got=0;
  const int32_t rc=fileSourceStatus(port_.fileSource->readAt(port_.fileSource->context,offset,copy,capacity,&got));
  if(rc)return rc;
  if(got>capacity || got>info.size-offset)return RISC_FILE_SOURCE_IO;
  if(got)memcpy(bytes,copy,got);
  *actual=got;return RISC_FILE_SOURCE_OK;
}
