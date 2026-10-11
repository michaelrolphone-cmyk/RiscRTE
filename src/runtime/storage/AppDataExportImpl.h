#pragma once
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <climits>
namespace RiscStorage {
inline AppDataExport* AppDataExport::slots_[SlotMax]{};
inline uintptr_t AppDataExport::nextContext_=0;
inline uint32_t AppDataExport::nextHandle_=0;
inline AppDataExport::~AppDataExport(){
 // Destruction never performs I/O or retries uncertainty. Runtime must retain
 // this object on failure; unregister anyway so stale callbacks cannot UAF.
 for(auto& slot:slots_)if(slot==this)slot=nullptr;
 if(!retained_ && !context_){dropRead();dropWrite();}
}
inline bool AppDataExport::validPath(const char* p,bool root){
 if(!p || p[0]!='/' || strnlen(p,PathMax+1)>PathMax)return false;
 if(!p[1])return root;
 size_t segment=0;
 for(size_t i=1;p[i];++i){unsigned char c=p[i];
  if(c=='/'){if(!segment || !p[i+1])return false;segment=0;continue;}
  bool alnum=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');
  if((!segment && !alnum) || (!alnum&&c!='.'&&c!='_'&&c!='-') || ++segment>=RISC_STORAGE_VOLUME_NAME_MAX)return false;
 }
 return segment!=0;
}
inline bool AppDataExport::validEntries(const Entry* entries,size_t count){
 if(!entries || !count || count>EntryMax)return false;
 for(size_t i=0;i<count;++i){const auto&e=entries[i];
  if(!e.owner[0] || strnlen(e.owner,sizeof(e.owner))>=sizeof(e.owner) || !e.nameSpace || e.nameSpace>INT32_MAX || !validPath(e.path,false))return false;
  size_t n=strnlen(e.name,sizeof(e.name));if(!n || n>RISC_APP_DATA_NAME_MAX)return false;
  for(size_t k=0;k<n;++k){unsigned char c=e.name[k];bool a=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');if((!k&&!a)||(!a&&c!='.'&&c!='_'&&c!='-'))return false;}
  for(size_t j=0;j<i;++j){const auto&p=entries[j];size_t a=strlen(e.path),b=strlen(p.path);
   if(!strcmp(e.path,p.path) || (a>b&&!strncmp(e.path,p.path,b)&&e.path[b]=='/') || (b>a&&!strncmp(p.path,e.path,a)&&p.path[a]=='/'))return false;
   if(e.nameSpace==p.nameSpace && (!strcmp(e.name,p.name)||strcmp(e.owner,p.owner)))return false;
  }
 }
 return true;
}
inline bool AppDataExport::configure(const RiscBoot::AppDataBackend*b,const Entry*e,size_t n,const char*label){
 if(configured_ || context_ || retained_ || !b || !b->stat || !b->read || !b->replace || !b->exitSafe || !validEntries(e,n) || !label || !*label || strnlen(label,sizeof(label_))>=sizeof(label_) || !hooks_.owner || !hooks_.safe || !hooks_.allocate || !hooks_.deallocate)return false;
 backend_=*b;std::copy(e,e+n,entries_);count_=n;strcpy(label_,label);configured_=true;return true;
}
inline AppDataExport* AppDataExport::resolve(void*c){
 uintptr_t key=reinterpret_cast<uintptr_t>(c);if(!key)return nullptr;
 for(auto* s:slots_)if(s && s->context_==key)return s;
 return nullptr;
}
inline int32_t AppDataExport::status(int32_t result){
 if(result==RISC_APP_DATA_RETAINED)retained_=true;
 if(result)snprintf(error_,sizeof(error_),"app-data status %d",int(result));else error_[0]=0;
 return result;
}
inline bool AppDataExport::enter(){
 admissionError_=RISC_APP_DATA_CONTEXT;
 if(!configured_ || !context_ || !hooks_.owner(hooks_.context) || busy_)return false;
 if(retained_){admissionError_=RISC_APP_DATA_RETAINED;return false;}
 if(!backend_.exitSafe(backend_.context)){admissionError_=status(RISC_APP_DATA_RETAINED);return false;}
 // Before I/O, a healthy native operation gate can be temporarily unavailable.
 // Losing that gate during a dispatched operation is different: leave retains.
 if(!hooks_.safe(hooks_.context)){admissionError_=status(RISC_APP_DATA_UNAVAILABLE);return false;}
 busy_=true;return true;
}
inline bool AppDataExport::leave(int32_t result){
 if(result==RISC_APP_DATA_RETAINED || !hooks_.owner(hooks_.context) || !hooks_.safe(hooks_.context) || !backend_.exitSafe(backend_.context))status(RISC_APP_DATA_RETAINED);else status(result);
 busy_=false;return !retained_;
}
inline bool AppDataExport::begin(risc_app_data_export_v1*out){
 if(!out || !configured_ || context_ || retained_ || busy_ || !hooks_.owner(hooks_.context) || !hooks_.safe(hooks_.context) || !backend_.exitSafe(backend_.context) || nextContext_==UINTPTR_MAX)return false;
 size_t slot=0;while(slot<SlotMax&&slots_[slot])++slot;if(slot==SlotMax)return false;
 context_=++nextContext_;slots_[slot]=this;
 // A missing configured file is an empty exposure; an unavailable mount is not.
 uint32_t size=0;uint64_t revision=0;int32_t admission=statRevision(entries_[0].path,&size,&revision);
 if(admission && admission!=RISC_APP_DATA_NOT_FOUND){
  if(!retained_){slots_[slot]=nullptr;context_=0;}return false;
 }
 risc_app_data_export_v1 api{};auto&ext=api.volume.terminal.power.volume;auto&v=ext.base;
 v={1,sizeof(api),reinterpret_cast<void*>(context_),
  [](void*c){auto*s=resolve(c);if(!s||!s->enter())return false;return s->leave(0);},
  [](void*c){auto*s=resolve(c);if(!s||!s->enter())return false;return s->leave(0);},
  [](void*c,char*out,size_t cap){auto*s=resolve(c);if(!s||!out||!cap||!s->enter())return false;bool ok=strlen(s->label_)<cap;if(ok)strcpy(out,s->label_);return s->leave(ok?0:RISC_APP_DATA_INVALID)&&ok;},
  [](void*c,const char*p,uint64_t*z,bool*d){auto*s=resolve(c);return s&&s->stat(p,z,d);},
  [](void*c,const char*p)->uint32_t{auto*s=resolve(c);return s?s->dirOpen(p):0;},
  [](void*c,uint32_t h,risc_storage_dirent_v1*e){auto*s=resolve(c);return s&&s->dirNext(h,e);},
  [](void*c,uint32_t h){auto*s=resolve(c);if(s)(void)s->dirClose(h);},
  [](void*c,const char*p,uint64_t*z)->uint32_t{auto*s=resolve(c);return s?s->openRead(p,z):0;},
  [](void*c,uint32_t h,void*b,size_t n)->size_t{auto*s=resolve(c);return s?s->read(h,b,n):0;},
  [](void*c,const char*p)->uint32_t{auto*s=resolve(c);return s?s->openWrite(p):0;},
  [](void*c,uint32_t h,const void*b,size_t n)->size_t{auto*s=resolve(c);return s?s->write(h,b,n):0;},
  [](void*c,uint32_t h,bool commit){auto*s=resolve(c);return s&&s->close(h,commit);},nullptr,
  [](void*c,char*out,size_t n){auto*s=resolve(c);if(!s||!s->hooks_.owner(s->hooks_.context)||!out||!n||strlen(s->error_)>=n)return false;strcpy(out,s->error_);return true;}};
 ext.dir_close_checked=[](void*c,uint32_t h){auto*s=resolve(c);return s&&s->dirClose(h);};
 ext.handle_error=[](void*c,uint32_t h,bool dir)->uint32_t{auto*s=resolve(c);if(!s||!s->hooks_.owner(s->hooks_.context))return uint32_t(RISC_APP_DATA_CONTEXT);if(s->retained_)return uint32_t(RISC_APP_DATA_RETAINED);if(dir)return h==s->dir_?uint32_t(s->dirError_):uint32_t(RISC_APP_DATA_CONTEXT);return h==s->write_?uint32_t(s->writeError_):h==s->read_?0:uint32_t(RISC_APP_DATA_CONTEXT);};
 bool writable=false;for(size_t i=0;i<count_;++i)writable=writable||entries_[i].writable;
 if(!writable){v.file_open_write=nullptr;v.file_write=nullptr;}
 api.export_tag=RISC_APP_DATA_EXPORT_TAG;api.export_version=1;
 api.stat_revision=[](void*c,const char*p,uint32_t*z,uint64_t*r)->int32_t{auto*s=resolve(c);if(!s){if(z)*z=0;
 if(r)*r=0;
 return RISC_APP_DATA_CONTEXT;}return s->statRevision(p,z,r);};
 api.read_revision=[](void*c,const char*p,uint64_t r,void*b,uint32_t cap,uint32_t*z,uint64_t*out)->int32_t{auto*s=resolve(c);if(!s){if(z)*z=0;if(out)*out=0;return RISC_APP_DATA_CONTEXT;}return s->readRevision(p,r,b,cap,z,out);};
 api.replace_revision=[](void*c,const char*p,uint64_t r,const void*b,uint32_t n)->int32_t{auto*s=resolve(c);return s?s->replaceRevision(p,r,b,n):RISC_APP_DATA_CONTEXT;};
 api.write_status=[](void*c,uint32_t h)->int32_t{auto*s=resolve(c);if(!s||!s->hooks_.owner(s->hooks_.context)||!h||h!=s->write_)return RISC_APP_DATA_CONTEXT;return s->retained_?RISC_APP_DATA_RETAINED:s->writeError_?s->writeError_:RISC_APP_DATA_EXPORT_WRITE_PENDING;};
 api.entry=[](void*c,uint32_t index,risc_app_data_export_entry_v1*out)->int32_t{
  if(out)memset(out,0,sizeof(*out));
  auto*s=resolve(c);if(!s||!s->hooks_.owner(s->hooks_.context))return RISC_APP_DATA_CONTEXT;
  if(!out)return RISC_APP_DATA_INVALID;
  if(!s->enter())return s->admissionError_;
  const int32_t result=index<s->count_?RISC_APP_DATA_OK:RISC_APP_DATA_NOT_FOUND;
  if(!s->leave(result))return RISC_APP_DATA_RETAINED;
  if(result)return result;
  strcpy(out->path,s->entries_[index].path);out->writable=s->entries_[index].writable?1u:0u;
  return RISC_APP_DATA_OK;
 };
 *out=api;return true;
}
inline const AppDataExport::Entry* AppDataExport::find(const char*p)const{
 if(!validPath(p,false))return nullptr;
 for(size_t i=0;i<count_;++i)if(!strcmp(p,entries_[i].path))return &entries_[i];
 return nullptr;
}
inline bool AppDataExport::directory(const char*p)const{
 if(!validPath(p))return false;
 if(!strcmp(p,"/"))return true;
 size_t n=strlen(p);for(size_t i=0;i<count_;++i)if(!strncmp(p,entries_[i].path,n)&&entries_[i].path[n]=='/')return true;
 return false;
}
inline int32_t AppDataExport::statRevision(const char*p,uint32_t*z,uint64_t*r){
 if(z)*z=0;
 if(r)*r=0;
 if(!z||!r)return RISC_APP_DATA_INVALID;
 if(!hooks_.owner(hooks_.context))return RISC_APP_DATA_CONTEXT;
 if(!enter())return admissionError_;
 const auto*e=find(p);uint32_t size=0;uint64_t revision=0;
 int32_t result=e?backend_.stat(backend_.context,e->nameSpace,e->name,&size,&revision):RISC_APP_DATA_CONTEXT;
 if(!result && (size>RISC_APP_DATA_FILE_MAX||!revision))result=RISC_APP_DATA_IO;
 if(!leave(result))return RISC_APP_DATA_RETAINED;
 if(!result){*z=size;*r=revision;}return result;
}
inline int32_t AppDataExport::readRevision(const char*p,uint64_t expected,void*out,uint32_t cap,uint32_t*z,uint64_t*r){
 if(z)*z=0;
 if(r)*r=0;
 if(!z||!r||(!out&&cap)||cap>RISC_APP_DATA_FILE_MAX)return RISC_APP_DATA_INVALID;
 if(!hooks_.owner(hooks_.context))return RISC_APP_DATA_CONTEXT;
 if(!enter())return admissionError_;
 const auto*e=find(p);uint32_t size=0;uint64_t revision=0;int32_t result=RISC_APP_DATA_CONTEXT;
 if(e){ioBytes_=cap?static_cast<unsigned char*>(hooks_.allocate(cap)):nullptr;
  result=cap&&!ioBytes_?RISC_APP_DATA_IO:backend_.read(backend_.context,e->nameSpace,e->name,expected,ioBytes_,cap,&size,&revision);
  if(!result&&(size>cap||!revision||revision!=expected))result=RISC_APP_DATA_IO;
  if(result==RISC_APP_DATA_BUFFER_SMALL && (size<=cap||size>RISC_APP_DATA_FILE_MAX||!revision))result=RISC_APP_DATA_IO;
 }
 if(!leave(result))return RISC_APP_DATA_RETAINED;
 if(!result){if(size)memcpy(out,ioBytes_,size);*z=size;*r=revision;}
 else if(result==RISC_APP_DATA_BUFFER_SMALL){*z=size;*r=revision;}
 if(ioBytes_){hooks_.deallocate(ioBytes_);ioBytes_=nullptr;}return result;
}
inline int32_t AppDataExport::replaceRevision(const char*p,uint64_t expected,const void*data,uint32_t size){
 if((!data&&size)||size>RISC_APP_DATA_FILE_MAX)return RISC_APP_DATA_INVALID;
 if(!hooks_.owner(hooks_.context))return RISC_APP_DATA_CONTEXT;
 if(!enter())return admissionError_;
 const auto*e=find(p);int32_t result=RISC_APP_DATA_CONTEXT;
 if(e&&e->writable){ioBytes_=size?static_cast<unsigned char*>(hooks_.allocate(size)):nullptr;
  if(size&&!ioBytes_)result=RISC_APP_DATA_IO;else{if(size)memcpy(ioBytes_,data,size);result=backend_.replace(backend_.context,e->nameSpace,e->name,expected,ioBytes_,size);}}
 if(!leave(result))return RISC_APP_DATA_RETAINED;
 if(ioBytes_){hooks_.deallocate(ioBytes_);ioBytes_=nullptr;}return result;
}
inline bool AppDataExport::stat(const char*p,uint64_t*z,bool*d){
 if(!z||!d)return false;
 if(directory(p)){if(!enter())return false;if(!leave(0))return false;*z=0;*d=true;return true;}
 uint32_t size;uint64_t revision;if(statRevision(p,&size,&revision))return false;*z=size;*d=false;return true;
}
inline uint32_t AppDataExport::dirOpen(const char*p){
 if(!enter())return 0;
 bool ok=!dir_&&directory(p)&&nextHandle_!=UINT32_MAX;
 if(ok){strcpy(dirPath_,p);dir_=++nextHandle_;dirAt_=0;dirError_=0;}
 return leave(ok?0:RISC_APP_DATA_INVALID)&&ok?dir_:0;
}
inline bool AppDataExport::dirNext(uint32_t h,risc_storage_dirent_v1*out){
 if(!out||!enter())return false;
 if(!h||h!=dir_||dirError_){leave(RISC_APP_DATA_INVALID);return false;}
 const size_t prefix=!strcmp(dirPath_,"/")?1:strlen(dirPath_)+1;
 for(;dirAt_<count_;){size_t i=dirAt_++;const auto&e=entries_[i];
  if(strncmp(e.path,dirPath_,prefix-1)||e.path[prefix-1]!='/')continue;
  const char*name=e.path+prefix;const char*slash=strchr(name,'/');size_t n=slash?size_t(slash-name):strlen(name);
  risc_storage_dirent_v1 value{};memcpy(value.name,name,n);value.is_directory=slash!=nullptr;
  if(slash){bool seen=false;for(size_t j=0;j<i;++j)if(!strncmp(entries_[j].path,e.path,prefix+n)&&entries_[j].path[prefix+n]=='/')seen=true;if(seen)continue;}
  else{uint32_t z=0;uint64_t r=0;int32_t rc=backend_.stat(backend_.context,e.nameSpace,e.name,&z,&r);
   if(!hooks_.owner(hooks_.context)||!hooks_.safe(hooks_.context)||!backend_.exitSafe(backend_.context)){dirError_=RISC_APP_DATA_RETAINED;leave(dirError_);return false;}
   if(rc==RISC_APP_DATA_NOT_FOUND)continue;
   if(rc||!r||z>RISC_APP_DATA_FILE_MAX){dirError_=rc?rc:RISC_APP_DATA_IO;leave(dirError_);return false;}value.size=z;}
  if(!leave(0))return false;
  *out=value;return true;
 }
 return leave(0)&&false;
}
inline bool AppDataExport::dirClose(uint32_t h){
 if(!enter())return false;
 bool ok=h&&h==dir_;if(ok){dir_=0;dirError_=0;}return leave(ok?0:RISC_APP_DATA_CONTEXT)&&ok;
}
inline void AppDataExport::dropRead(){if(readBytes_)hooks_.deallocate(readBytes_);readBytes_=nullptr;read_=readSize_=readAt_=0;}
inline void AppDataExport::dropWrite(){if(writeBytes_)hooks_.deallocate(writeBytes_);writeBytes_=nullptr;write_=writeSize_=0;writeEntry_=-1;writeError_=0;writeAttempted_=false;}
inline uint32_t AppDataExport::openRead(const char*p,uint64_t*out){
 if(!out||!enter())return 0;
 const auto*e=find(p);int32_t rc=RISC_APP_DATA_CONTEXT;uint32_t size=0,actual=0;uint64_t revision=0,got=0;
 if(e&&!read_&&nextHandle_!=UINT32_MAX){rc=backend_.stat(backend_.context,e->nameSpace,e->name,&size,&revision);
  if(!hooks_.owner(hooks_.context)||!hooks_.safe(hooks_.context)||!backend_.exitSafe(backend_.context))rc=RISC_APP_DATA_RETAINED;
  if(!rc&&(size>RISC_APP_DATA_FILE_MAX||!revision))rc=RISC_APP_DATA_IO;
  if(!rc){readBytes_=size?static_cast<unsigned char*>(hooks_.allocate(size)):nullptr;if(size&&!readBytes_)rc=RISC_APP_DATA_IO;
   else{rc=backend_.read(backend_.context,e->nameSpace,e->name,revision,readBytes_,size,&actual,&got);if(!rc&&(actual!=size||got!=revision))rc=RISC_APP_DATA_IO;}}
 }
 if(!leave(rc))return 0;
 if(rc){if(!read_)dropRead();return 0;}
 read_=++nextHandle_;readSize_=size;readAt_=0;*out=size;return read_;
}
inline size_t AppDataExport::read(uint32_t h,void*out,size_t cap){
 if((!out&&cap)||!enter())return 0;
 if(!h||h!=read_){leave(RISC_APP_DATA_CONTEXT);return 0;}
 size_t n=std::min<size_t>({cap,512,size_t(readSize_-readAt_)});
 if(!leave(0))return 0;
 if(n)memcpy(out,readBytes_+readAt_,n);
 readAt_+=uint32_t(n);return n;
}
inline uint32_t AppDataExport::openWrite(const char*p){
 if(!enter())return 0;
 const auto*e=find(p);int32_t rc=RISC_APP_DATA_CONTEXT;
 if(e&&e->writable&&!write_&&nextHandle_!=UINT32_MAX){uint32_t z=0;uint64_t r=0;rc=backend_.stat(backend_.context,e->nameSpace,e->name,&z,&r);
  if(rc==RISC_APP_DATA_NOT_FOUND){writeBytes_=static_cast<unsigned char*>(hooks_.allocate(RISC_APP_DATA_FILE_MAX));rc=writeBytes_?0:RISC_APP_DATA_IO;}
  else if(!rc)rc=RISC_APP_DATA_STALE;
 }
 if(!leave(rc))return 0;
 if(rc)return 0;
 write_=++nextHandle_;writeEntry_=int(e-entries_);writeSize_=0;writeError_=0;writeAttempted_=false;return write_;
}
inline size_t AppDataExport::write(uint32_t h,const void*data,size_t n){
 if((!data&&n)||!enter())return 0;
 if(!h||h!=write_||writeAttempted_||writeError_){leave(RISC_APP_DATA_CONTEXT);return 0;}
 size_t amount=std::min<size_t>(n,512);
 if(amount>RISC_APP_DATA_FILE_MAX-writeSize_){writeError_=RISC_APP_DATA_NO_SPACE;leave(writeError_);return 0;}
 if(amount)memcpy(writeBytes_+writeSize_,data,amount);
 writeSize_+=uint32_t(amount);
 return leave(0)?amount:0;
}
inline bool AppDataExport::close(uint32_t h,bool commit){
 if(!enter())return false;
 if(h&&h==read_){if(!leave(0))return false;dropRead();return true;}
 if(!h||h!=write_){leave(RISC_APP_DATA_CONTEXT);return false;}
 if(!commit){if(!leave(0))return false;dropWrite();return true;}
 if(writeAttempted_||writeError_){leave(writeError_?writeError_:RISC_APP_DATA_IO);return false;}
 writeAttempted_=true;const auto&e=entries_[writeEntry_];
 int32_t rc=backend_.replace(backend_.context,e.nameSpace,e.name,0,writeBytes_,writeSize_);writeError_=rc;
 if(!leave(rc))return false;
 if(rc)return false;
 dropWrite();return true;
}
inline bool AppDataExport::end(){
 if(!context_)return !retained_&&!busy_;
 if(!enter())return false;
 if(!leave(0))return false;
 dropRead();dropWrite();dir_=0;dirError_=0;
 for(auto&slot:slots_)if(slot==this)slot=nullptr;
 context_=0;return true;
}
}
