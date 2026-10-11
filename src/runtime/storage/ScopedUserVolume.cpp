#include "ScopedUserVolume.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace RiscStorage {
namespace {
size_t length(const char*s,size_t maximum){size_t n=0;while(n<maximum && s[n])++n;return n;}
// This early guard also catches ASCII case aliases on FAT. The admitted
// provider remains responsible for its own alias-aware exclusive rename.
bool sameOrDescendant(const char*source,const char*destination){
 for(;*source && *destination;++source,++destination){
  const auto lower=[](unsigned char c){return c>='A' && c<='Z'?c+'a'-'A':c;};
  if(lower(*source)!=lower(*destination))return false;
 }
 return !*source && (!*destination || *destination=='/');
}
}
ScopedUserVolume* ScopedUserVolume::slots_[SlotMax]{};
uintptr_t ScopedUserVolume::nextContext_=1;
uint32_t ScopedUserVolume::nextStage_=1;
uint32_t ScopedUserVolume::nextHandle_=1;
ScopedUserVolume::~ScopedUserVolume(){for(auto&slot:slots_)if(slot==this)slot=nullptr;}
bool ScopedUserVolume::copy(char*out,size_t n,const char*s){if(!out || !n || !s || strlen(s)>=n)return false;memcpy(out,s,strlen(s)+1);return true;}
bool ScopedUserVolume::reserved(const char*p){return p && p[0]=='~' && (p[1]=='r' || p[1]=='R');}
bool ScopedUserVolume::valid(const char*p,bool root){
 if(!p || *p!='/' || length(p,PathMax+1)>PathMax || (root && !p[1]))return false;
 if(!p[1])return true;
 const char*part=p+1;
 for(const char*q=part;;++q){const unsigned char c=*q;
  if(c && (c<32 || c==127 || c=='\\' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|'))return false;
  if(c=='/' || !c){const size_t n=size_t(q-part);
   if(!n || n>=RISC_STORAGE_VOLUME_NAME_MAX || part[n-1]=='.' || part[n-1]==' ' || reserved(part))return false;
   if(!c)return true;
   part=q+1;
  }
 }
}
bool ScopedUserVolume::fail(const char*error){(void)copy(error_,sizeof(error_),error);return false;}
bool ScopedUserVolume::retain(const char*error){retained_=true;return fail(error);}
bool ScopedUserVolume::path(const char*p,char*out,bool allowRoot){
 if(!valid(p) || (!allowRoot && !p[1]))return fail("Invalid user-volume path");
 if(snprintf(out,RISC_STORAGE_VOLUME_PATH_MAX,"%s%s",root_,p[1]?p:"")>=int(RISC_STORAGE_VOLUME_PATH_MAX))return fail("User-volume path too long");
 return true;
}
bool ScopedUserVolume::configure(const risc_storage_volume_api_v1*api,const char*root,const char*label){
 if(configured_ || active_ || retained_ || !hooks_.owner || !hooks_.safe || !hooks_.owner(hooks_.context) || !valid(root,true) || !label || length(label,sizeof(label_))>=sizeof(label_))return false;
 const auto*ext=risc_storage_volume_extension(api);
 if(!ext || !api->refresh || !api->ready || !api->stat || !api->dir_open || !api->dir_next || !api->file_open_read || !api->file_read || !api->file_open_write || !api->file_write || !api->file_close || !api->remove || !api->last_error || !ext->dir_close_checked || !ext->handle_error || !ext->file_sync || !ext->file_info || !ext->rename)return false;
 copy(root_,sizeof(root_),root);copy(label_,sizeof(label_),label);upstream_=ext;
 extendedSupported_=ext->mkdir!=nullptr;configured_=true;return true;
}
ScopedUserVolume* ScopedUserVolume::resolve(void*context){
 if(!context)return nullptr;
 for(auto*slot:slots_)if(slot && slot->active_ && slot->context_==reinterpret_cast<uintptr_t>(context))return slot;
 return nullptr;
}
bool ScopedUserVolume::enter(){
 if(!hooks_.owner || !hooks_.owner(hooks_.context))return false;
 if(!active_ || retained_ || busy_)return false;
 if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
 busy_=true;error_[0]=0;return true;
}
bool ScopedUserVolume::leave(){
 if(!retained_ && !hooks_.safe(hooks_.context))retain("Storage custody lost. Restart needed.");
 busy_=false;return !retained_;
}
bool ScopedUserVolume::providerError(const char*fallback){
 char message[sizeof(error_)]{};
 // Do not query an already-retained provider, even for a diagnostic string.
 if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
 if(upstream_->base.last_error(upstream_->base.context,message,sizeof(message)) && message[0] && memchr(message,0,sizeof(message)))return fail(message);
 return fail(fallback);
}
bool ScopedUserVolume::begin(risc_storage_volume_api_v1*out){
 if(!out || !configured_ || active_ || retained_ || busy_ || !hooks_.owner(hooks_.context) || !nextContext_)return false;
 ScopedUserVolume**slot=nullptr;for(auto&s:slots_)if(!s){slot=&s;break;}if(!slot)return false;
 context_=nextContext_++;active_=true;*slot=this;
 if(!call([&]{uint64_t size=0;bool directory=false;
  if(!upstream_->base.ready(upstream_->base.context))return providerError("User volume unavailable");
  if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
  return (upstream_->base.stat(upstream_->base.context,root_,&size,&directory) && directory) || providerError("User root unavailable");
 })){
  if(!retained_){active_=false;*slot=nullptr;context_=0;}return false;
 }
 risc_storage_volume_api_v1 table{1,sizeof(risc_storage_volume_api_v1),reinterpret_cast<void*>(context_),
 [](void*c){auto*s=resolve(c);return s && s->refresh();},
 [](void*c){auto*s=resolve(c);return s && s->ready();},
 [](void*c,char*out,size_t n){auto*s=resolve(c);return s && s->call([&]{return copy(out,n,s->label_);});},
 [](void*c,const char*p,uint64_t*n,bool*d){auto*s=resolve(c);return s && s->stat(p,n,d);},
 [](void*c,const char*p)->uint32_t{auto*s=resolve(c);return s?s->dirOpen(p):0;},
 [](void*c,uint32_t h,risc_storage_dirent_v1*out){auto*s=resolve(c);return s && s->dirNext(h,out);},
 [](void*c,uint32_t h){auto*s=resolve(c);if(s)s->dirClose(h);},
 [](void*c,const char*p,uint64_t*n)->uint32_t{auto*s=resolve(c);return s?s->fileOpenRead(p,n):0;},
 [](void*c,uint32_t h,void*out,size_t n)->size_t{auto*s=resolve(c);return s?s->fileRead(h,out,n):0;},
 [](void*c,const char*p)->uint32_t{auto*s=resolve(c);return s?s->fileOpenWrite(p):0;},
 [](void*c,uint32_t h,const void*bytes,size_t n)->size_t{auto*s=resolve(c);return s?s->fileWrite(h,bytes,n):0;},
 [](void*c,uint32_t h,bool commit){auto*s=resolve(c);return s && s->fileClose(h,commit);},
 [](void*c,const char*p){auto*s=resolve(c);return s && s->remove(p);},
 [](void*c,char*out,size_t n){auto*s=resolve(c);return s && s->hooks_.owner(s->hooks_.context) && copy(out,n,s->error_);}};
 *out=table;return true;
}
bool ScopedUserVolume::beginExtended(risc_storage_volume_api_v1_ext*out){
 // Reject unavailable optional operations before beginning a session or I/O.
 // Availability was copied during configuration. Do not dereference a borrowed
 // provider table here: begin() first establishes current owner and custody.
 if(!out || !configured_ || !extendedSupported_)return false;
 risc_storage_volume_api_v1_ext table{};
 if(!begin(&table.base))return false;
 table.base.struct_size=sizeof(table);
 table.dir_close_checked=[](void*c,uint32_t h){auto*s=resolve(c);return s && s->dirCloseChecked(h);};
 table.handle_error=[](void*c,uint32_t h,bool d)->uint32_t{auto*s=resolve(c);return s?s->handleError(h,d):1u;};
 table.mkdir=[](void*c,const char*p){auto*s=resolve(c);return s && s->mkdir(p);};
 table.rename=[](void*c,const char*a,const char*b){auto*s=resolve(c);return s && s->rename(a,b);};
 *out=table;return true;
}
bool ScopedUserVolume::ready(){return call([&]{return upstream_->base.ready(upstream_->base.context) || providerError("User volume unavailable");});}
bool ScopedUserVolume::refresh(){return call([&]{
 if(file_ || read_ || dir_)return fail("Close user-volume handles before refresh");
 uint64_t size=0;bool directory=false;
 if(!upstream_->base.refresh(upstream_->base.context))return providerError("User volume unavailable");
 if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
 if(!upstream_->base.ready(upstream_->base.context))return providerError("User volume unavailable");
 if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
 return (upstream_->base.stat(upstream_->base.context,root_,&size,&directory) && directory) || providerError("User root unavailable");
});}
bool ScopedUserVolume::stat(const char*p,uint64_t*size,bool*directory){
 if(!size || !directory)return false;
 uint64_t n=0;bool d=false;const bool ok=call([&]{char full[RISC_STORAGE_VOLUME_PATH_MAX];return path(p,full) && (upstream_->base.stat(upstream_->base.context,full,&n,&d) || providerError("User file unavailable"));});
 if(ok){*size=n;*directory=d;}return ok;
}
uint32_t ScopedUserVolume::dirOpen(const char*p){
 uint32_t result=0;const bool ok=call([&]{char full[RISC_STORAGE_VOLUME_PATH_MAX];
 if(dir_ || !nextHandle_)return fail("User folder already open or handles exhausted");
 if(!path(p,full))return false;
 upstreamDir_=upstream_->base.dir_open(upstream_->base.context,full);if(!upstreamDir_)return providerError("Could not open user folder");
 dirFailed_=false;result=dir_=nextHandle_++;return true;
 });return ok?result:0;
}
bool ScopedUserVolume::dirNext(uint32_t h,risc_storage_dirent_v1*out){
 if(!out)return false;
 risc_storage_dirent_v1 entry{};const bool ok=call([&]{
 if(!h || h!=dir_ || dirFailed_)return fail("User folder handle expired or failed");
 for(size_t i=0;i<SkipMax;++i){entry={};
  const bool next=upstream_->base.dir_next(upstream_->base.context,upstreamDir_,&entry);
  if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
  const auto status=upstream_->handle_error(upstream_->base.context,upstreamDir_,true);
  if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
  if(status){dirFailed_=true;return providerError("User folder read failed");}
  if(!next)return false;
  if(!entry.name[0] || !memchr(entry.name,0,sizeof(entry.name))){dirFailed_=true;return fail("Invalid upstream file name");}
  if(!strcmp(entry.name,".") || !strcmp(entry.name,"..") || reserved(entry.name))continue;
  char check[RISC_STORAGE_VOLUME_NAME_MAX+1];check[0]='/';
  memcpy(check+1,entry.name,strlen(entry.name)+1);
  if(entry.is_directory>1 || strchr(entry.name,'/') || !valid(check)){dirFailed_=true;return fail("Invalid upstream file name");}
  return true;
 }
 dirFailed_=true;return fail("Too many reserved staging entries");
 });if(ok)*out=entry;return ok;
}
bool ScopedUserVolume::closeDir(){
 if(!dir_)return true;
 if(!upstream_->dir_close_checked(upstream_->base.context,upstreamDir_))return retain("Folder close uncertain. Restart needed.");
 dir_=upstreamDir_=0;dirFailed_=false;return true;
}
bool ScopedUserVolume::dirCloseChecked(uint32_t h){return call([&]{if(!h || h!=dir_)return fail("User folder handle expired");return closeDir();});}
void ScopedUserVolume::dirClose(uint32_t h){(void)dirCloseChecked(h);}
uint32_t ScopedUserVolume::handleError(uint32_t h,bool directory){
 // A diagnostic read must not clear the preceding operation's error. Never
 // delegate this callback: upstream tokens and retained providers stay private.
 if(!hooks_.owner(hooks_.context) || !active_ || retained_ || busy_)return 1u;
 if(!hooks_.safe(hooks_.context)){retain("Storage custody lost. Restart needed.");return 1u;}
 if(!h)return 1u;
 if(directory)return h==dir_ && !dirFailed_?0u:1u;
 if(h==read_)return readFailed_?1u:0u;
 return h==file_ && !fileFailed_?0u:1u;
}
uint32_t ScopedUserVolume::fileOpenRead(const char*p,uint64_t*size){
 if(!size)return 0;
 uint32_t result=0;const bool ok=call([&]{char full[RISC_STORAGE_VOLUME_PATH_MAX];
 if(read_ || !nextHandle_)return fail("User reader already open or handles exhausted");
 if(!path(p,full,false))return false;
 uint64_t n=0;upstreamRead_=upstream_->base.file_open_read(upstream_->base.context,full,&n);
 if(!upstreamRead_)return providerError("Could not open user file");
 readSize_=n;readOffset_=0;readFailed_=false;result=read_=nextHandle_++;return true;
 });if(ok)*size=readSize_;return ok?result:0;
}
uint32_t ScopedUserVolume::fileOpenWrite(const char*p){
 uint32_t result=0;const bool ok=call([&]{
 if(file_ || !nextHandle_ || !nextStage_ || nextStage_>0xffffffu)return fail("User writer already open or handles exhausted");
 if(!path(p,destination_,false))return false;
 uint64_t n=0;bool d=false;
 if(upstream_->base.stat(upstream_->base.context,destination_,&n,&d))return fail("Destination already exists");
 if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
 // Exclusive rename is authoritative at commit. A failed stat cannot grant
 // overwrite permission; no destination is ever removed by this adapter.
 const char*slash=strrchr(destination_,'/');const size_t prefix=size_t(slash-destination_)+1;
 memcpy(stage_,destination_,prefix);
 snprintf(stage_+prefix,sizeof(stage_)-prefix,"~R%06X.TMP",unsigned(nextStage_++));
 upstreamFile_=upstream_->base.file_open_write(upstream_->base.context,stage_);
 if(!upstreamFile_)return providerError("Could not create user staging file");
 size_=offset_=0;writing_=true;fileFailed_=false;result=file_=nextHandle_++;return true;
 });return ok?result:0;
}
size_t ScopedUserVolume::fileRead(uint32_t h,void*out,size_t n){
 if(!out && n)return 0;
 unsigned char bytes[IoMax];size_t got=0;const bool ok=call([&]{
 if(!h || h!=read_ || readFailed_)return fail("User read handle expired or failed");
 if(!n)return true;
 uint64_t size=0,position=0;
 if(!upstream_->file_info(upstream_->base.context,upstreamRead_,&size,&position) || size!=readSize_ || position!=readOffset_){readFailed_=true;return providerError("User file changed. Reopen it.");}
 if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
 const size_t wanted=size_t(std::min<uint64_t>(std::min(n,IoMax),readSize_-readOffset_));if(!wanted)return true;
 got=upstream_->base.file_read(upstream_->base.context,upstreamRead_,bytes,wanted);
 if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
 if(got!=wanted || upstream_->handle_error(upstream_->base.context,upstreamRead_,false)){readFailed_=true;return providerError("User file read failed");}
 readOffset_+=got;return true;
 });if(ok && got)memcpy(out,bytes,got);return ok?got:0;
}
size_t ScopedUserVolume::fileWrite(uint32_t h,const void*bytes,size_t n){
 if(!bytes && n)return 0;
 const size_t wanted=std::min(n,IoMax);unsigned char snapshot[IoMax];
 size_t written=0;const bool ok=call([&]{
 if(!h || h!=file_ || !writing_ || fileFailed_)return fail("User write handle expired or failed");
 if(!wanted)return true;
 memcpy(snapshot,bytes,wanted);
 if(offset_>std::numeric_limits<uint64_t>::max()-wanted){fileFailed_=true;return fail("User file too large");}
 written=upstream_->base.file_write(upstream_->base.context,upstreamFile_,snapshot,wanted);
 if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
 if(written!=wanted || upstream_->handle_error(upstream_->base.context,upstreamFile_,false)){fileFailed_=true;return providerError("User file write failed");}
 offset_+=written;return true;
 });return ok?written:0;
}
bool ScopedUserVolume::closeFile(bool commit){
 if(!file_)return true;
 const bool requested=commit;
 if(writing_ && commit && !fileFailed_){
  if(!upstream_->file_sync(upstream_->base.context,upstreamFile_))fileFailed_=true;
  if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
 }
 if(fileFailed_)commit=false;
 if(!upstream_->base.file_close(upstream_->base.context,upstreamFile_,!writing_ || commit))return retain("File close or rollback uncertain. Restart needed.");
 if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
 if(writing_ && commit && !upstream_->rename(upstream_->base.context,stage_,destination_))return retain("File publication uncertain. Restart needed.");
 const bool failed=fileFailed_;file_=upstreamFile_=0;writing_=fileFailed_=false;size_=offset_=0;stage_[0]=destination_[0]=0;
 // A false commit result must not let the owner unload while a v1 consumer
 // still holds this handle as unconfirmed. The stage is already rolled back;
 // retain without another provider call rather than turn a retry into a false
 // success or a permanently stale handle in an otherwise unloadable session.
 return (!requested || !failed) || retain("Publication failed; rollback complete. Restart needed.");
}
bool ScopedUserVolume::closeRead(){
 if(!read_)return true;
 if(!upstream_->base.file_close(upstream_->base.context,upstreamRead_,true))return retain("File close uncertain. Restart needed.");
 if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
 read_=upstreamRead_=0;readSize_=readOffset_=0;readFailed_=false;
 // A prior read error is not a close failure. No publication occurs for a
 // reader, and the consumer already saw the read error. Report checked cleanup.
 return true;
}
bool ScopedUserVolume::fileClose(uint32_t h,bool commit){return call([&]{
 if(h && h==read_)return closeRead();
 if(!h || h!=file_)return fail("User file handle expired");
 return closeFile(commit);
});}
bool ScopedUserVolume::remove(const char*p){return call([&]{char full[RISC_STORAGE_VOLUME_PATH_MAX];
 if(file_ || read_ || dir_)return fail("Close user-volume handles before remove");
 return path(p,full,false) && (upstream_->base.remove(upstream_->base.context,full) || providerError("Could not remove user file"));
});}
bool ScopedUserVolume::mkdir(const char*p){return call([&]{
 if(file_ || read_ || dir_)return fail("Close user-volume handles before mkdir");
 char full[RISC_STORAGE_VOLUME_PATH_MAX];
 if(!upstream_->mkdir || !path(p,full,false))return false;
 uint64_t size=0;bool directory=false;
 const bool exists=upstream_->base.stat(upstream_->base.context,full,&size,&directory);
 if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
 if(exists)return fail("Destination already exists");
 // The provider is authoritative on races. Its bool result cannot establish
 // whether a failed mutation had a side effect, so never retry or undo it.
 return upstream_->mkdir(upstream_->base.context,full) || retain("Folder creation uncertain. Restart needed.");
});}
bool ScopedUserVolume::rename(const char*source,const char*destination){return call([&]{
 if(file_ || read_ || dir_)return fail("Close user-volume handles before rename");
 char from[RISC_STORAGE_VOLUME_PATH_MAX],to[RISC_STORAGE_VOLUME_PATH_MAX];
 // Copy both untrusted inputs before the first provider callback.
 if(!path(source,from,false) || !path(destination,to,false))return false;
 if(sameOrDescendant(from,to))return fail("Destination is the source or its descendant");
 uint64_t size=0;bool directory=false;
 if(!upstream_->base.stat(upstream_->base.context,from,&size,&directory))return providerError("Source unavailable");
 if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
 const bool exists=upstream_->base.stat(upstream_->base.context,to,&size,&directory);
 if(!hooks_.safe(hooks_.context))return retain("Storage custody lost. Restart needed.");
 if(exists)return fail("Destination already exists");
 return upstream_->rename(upstream_->base.context,from,to) || retain("Rename uncertain. Restart needed.");
});}
bool ScopedUserVolume::end(){
 if(!hooks_.owner || !hooks_.owner(hooks_.context) || retained_ || busy_)return false;
 if(!active_)return true;
 if(!call([&]{return closeRead() && closeFile(false) && closeDir();}))return false;
 active_=false;context_=0;for(auto&slot:slots_)if(slot==this)slot=nullptr;return true;
}
}
