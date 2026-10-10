// Included inside RiscBoot in Runtime.cpp. The unchanged context-free canonical
// table is reachable only through an explicit live file.open@1 app grant.
namespace {
bool fileOpenMetadata(JsonObjectConst manifest,FileOpenMetadata& out) {
  out={};
  const JsonVariantConst types=manifest["supported_file_types"];
  if(!manifest["icon"].isUnbound() && !text(manifest["icon"],out.handler.icon,sizeof(out.handler.icon)))return false;
  if(types.isUnbound())return true;
  if(!types.is<JsonArrayConst>() || types.size()>FileOpenMetadata::MaxTypes)return false;
  for(JsonVariantConst value:types.as<JsonArrayConst>()) {
    char extension[FileOpenMetadata::TypeBytes];
    if(!text(value,extension,sizeof(extension)) || extension[0]!='.' || !extension[1])return false;
    for(size_t i=1;extension[i];++i)if(!((extension[i]>='a' && extension[i]<='z') || (extension[i]>='0' && extension[i]<='9')))return false;
    for(size_t i=0;i<out.count;++i)if(!strcmp(out.types[i],extension))return false;
    strcpy(out.types[out.count++],extension);
  }
  if(!out.count)return true;
  if(!text(manifest["id"],out.handler.app_id,sizeof(out.handler.app_id)))return false;
  if(manifest["display_name"].isUnbound())strcpy(out.handler.display_name,out.handler.app_id);
  else if(!text(manifest["display_name"],out.handler.display_name,sizeof(out.handler.display_name)))return false;
  out.handler.kind=T5_FILE_HANDLER_APP;
  return true;
}
// Preserve the caller's absolute SD data path byte for byte. Never derive an
// executable name from it, normalize away components, or perform filesystem I/O.
bool fileOpenExtension(const char* source,char (&extension)[FileOpenMetadata::TypeBytes]) {
  extension[0]=0;
  if(!source)return false;
  const size_t length=strnlen(source,T5_FILE_OPEN_PATH_MAX);
  if(length<5 || length>=T5_FILE_OPEN_PATH_MAX || strncmp(source,"/sd/",4) || !utf8(source,length))return false;
  const char* component=source+4;
  const char* dot=nullptr;
  for(const char* p=component;;++p) {
    const unsigned char c=static_cast<unsigned char>(*p);
    if(!c || c=='/') {
      const size_t n=size_t(p-component);
      if(!n || (n==1 && component[0]=='.') || (n==2 && component[0]=='.' && component[1]=='.'))return false;
      if(!c)break;
      component=p+1;dot=nullptr;
    }else if(c<32 || c==127 || c=='\\')return false;
    else if(c=='.')dot=p;
  }
  if(!dot || dot==component)return false;
  const size_t n=strlen(dot);
  if(n<2 || n>=sizeof(extension))return false;
  extension[0]='.';
  for(size_t i=1;i<n;++i) {
    char c=dot[i];if(c>='A' && c<='Z')c=char(c-'A'+'a');
    if(!((c>='a' && c<='z') || (c>='0' && c<='9')))return false;
    extension[i]=c;
  }
  extension[n]=0;return true;
}
bool fileOpenMatches(const FileOpenMetadata& handler,const char* extension) {
  for(size_t i=0;i<handler.count;++i)if(!strcmp(handler.types[i],extension))return true;
  return false;
}
}
const t5_file_open_api_v1* Runtime::fileOpenApi() {
  static const t5_file_open_api_v1 api={T5_FILE_OPEN_API_VERSION,sizeof(t5_file_open_api_v1),
    [](){return currentRuntime && currentRuntime->fileOpenReady();},
    [](const char* source)->uint32_t{return currentRuntime?currentRuntime->fileHandlerCount(source):0;},
    [](const char* source,uint32_t index,t5_file_handler_t* out){if(out)*out={};return currentRuntime && currentRuntime->fileHandlerGet(source,index,out);},
    [](const char* source,const char* id,uint64_t cookie){return currentRuntime && currentRuntime->fileOpenRequest(source,id,cookie);},
    [](int32_t* error,uint64_t* cookie){return currentRuntime && currentRuntime->fileOpenTakeResult(error,cookie);},
    [](char* out,size_t capacity){if(out && capacity)*out=0;return currentRuntime && currentRuntime->fileSourcePathGet(out,capacity);}};
  return &api;
}
bool Runtime::fileOpenReady() const {
  if(promotionRunning_ || !active() || !invocation_->appPolicy_ || retained_ || !providerStorageSafe())return false;
  for(const auto& grant:invocation_->appGrants_)if(grant.live && grant.api==fileOpenApi())return true;
  return false;
}
uint32_t Runtime::fileHandlerCount(const char* source) const {
  char extension[FileOpenMetadata::TypeBytes];
  if(!fileOpenReady() || !fileHandlers_ || !fileOpenExtension(source,extension))return 0;
  uint32_t count=0;
  for(size_t i=0;i<policyCount_;++i)if(fileOpenMatches(fileHandlers_[i],extension))++count;
  return count;
}
bool Runtime::fileHandlerGet(const char* source,uint32_t index,t5_file_handler_t* out) const {
  if(out)*out={};
  char extension[FileOpenMetadata::TypeBytes];
  if(!out || !fileOpenReady() || !fileHandlers_ || !fileOpenExtension(source,extension))return false;
  for(size_t i=0;i<policyCount_;++i)if(fileOpenMatches(fileHandlers_[i],extension)) {
    if(!index){*out=fileHandlers_[i].handler;return true;}--index;
  }
  return false;
}
bool Runtime::fileOpenRequest(const char* source,const char* id,uint64_t cookie) {
  char extension[FileOpenMetadata::TypeBytes];
  if(residentEnabled_ && invocation_->role!=RISC_RESIDENT_ROLE_FOREGROUND &&
     !(invocation_==&mainInvocation_ && !invocation_->role && residentLegacyAllowed(invocation_->current_)))return false;
  if(!fileOpenReady() || !invocation_->entryRunning_ || invocation_->queued_[0] || invocation_->fileOpen_.phase!=FileOpenState::Phase::Idle ||
     !fileHandlers_ || !id || !*id || strnlen(id,T5_FILE_HANDLER_ID_MAX)>=T5_FILE_HANDLER_ID_MAX ||
     !fileOpenExtension(source,extension) || (port_.appExitSafe && !port_.appExitSafe()))return false;
  for(size_t i=0;i<policyCount_;++i) {
    if(strcmp(policies_[i].id,id) || &policies_[i]==invocation_->appPolicy_ || !fileOpenMatches(fileHandlers_[i],extension))continue;
    if(residentEnabled_ && !residentAllowed(policies_[i].elf) && !residentLegacyAllowed(policies_[i].elf))return false;
    // These indices refer only to immutable deployment-admitted policy records.
    // The SD path is data, never a launch path. Caller and receiver map afresh.
    invocation_->fileOpen_.caller=static_cast<int8_t>(invocation_->appPolicy_-policies_.get());
    invocation_->fileOpen_.receiver=static_cast<int8_t>(i);
    invocation_->fileOpen_.cookie=cookie;
    strcpy(invocation_->fileOpen_.source,source);
    strcpy(invocation_->queued_,policies_[i].elf);
    invocation_->fileOpen_.phase=FileOpenState::Phase::Requested;
    return true;
  }
  return false;
}
bool Runtime::fileOpenTakeResult(int32_t* error,uint64_t* cookie) {
  if(!fileOpenReady() || invocation_->fileOpen_.phase!=FileOpenState::Phase::Result ||
     invocation_->appPolicy_!=&policies_[invocation_->fileOpen_.caller])return false;
  if(error)*error=invocation_->fileOpen_.result;
  if(cookie)*cookie=invocation_->fileOpen_.cookie;
  invocation_->fileOpen_={};return true;
}
bool Runtime::fileSourcePathGet(char* out,size_t capacity) const {
  if(out && capacity)*out=0;
  if(!out || !capacity || !fileOpenReady() || invocation_->fileOpen_.phase!=FileOpenState::Phase::Receiving ||
     invocation_->appPolicy_!=&policies_[invocation_->fileOpen_.receiver] || strlen(invocation_->fileOpen_.source)>=capacity)return false;
  strcpy(out,invocation_->fileOpen_.source);return true;
}
bool Runtime::fileOpenAfterRun(bool completed) {
  if(invocation_->fileOpen_.phase==FileOpenState::Phase::Requested) {
    if(completed && !strcmp(invocation_->queued_,policies_[invocation_->fileOpen_.receiver].elf))invocation_->fileOpen_.phase=FileOpenState::Phase::Receiving;
    else invocation_->fileOpen_={};
  }else if(invocation_->fileOpen_.phase==FileOpenState::Phase::Receiving) {
    // Only explicit Home can queue during Receiving. runOne has already
    // finalized/unmapped the receiver and revoked every grant successfully.
    if(completed && invocation_->queued_[0] && !strcmp(invocation_->queued_,default_)) {
      invocation_->fileOpen_={};return false;
    }
    invocation_->fileOpen_.result=completed?0:-1; // ESP_OK / ESP_FAIL: invocation, not content parsing.
    memset(invocation_->fileOpen_.source,0,sizeof(invocation_->fileOpen_.source));
    invocation_->fileOpen_.phase=FileOpenState::Phase::Result;
    strcpy(invocation_->current_,policies_[invocation_->fileOpen_.caller].elf);
    invocation_->queued_[0]=0;return true;
  }else if(invocation_->fileOpen_.phase==FileOpenState::Phase::Result) {
    // An unconsumed result cannot leak to another invocation or default app.
    invocation_->fileOpen_={};
  }
  return false;
}
