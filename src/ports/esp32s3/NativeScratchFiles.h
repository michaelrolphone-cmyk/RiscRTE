#pragma once
#include <RiscStorageFilesV1.h>
#include "runtime/storage/FileValidation.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>
namespace RiscCpu {
// Optional concrete provider for one provisioned private file. This is the only
// layer which sees its physical name and selected VFS fault policy. No mount,
// format, stage/seal/handoff, application authority, or destructor I/O lives here.
class NativeScratchFiles final {
public:
 explicit NativeScratchFiles(const char* path,bool terminalIo=true,bool successErrno=true):path_(path),terminalIo_(terminalIo),successErrno_(successErrno){
  api_={1,sizeof(api_),this,
   [](void*c,const char*p,uint32_t f,uint32_t*h){return self(c).open(p,f,h);},
   [](void*c,uint32_t h){return self(c).close(h);},
   [](void*c,uint32_t h,void*b,uint32_t n,uint32_t*a){return self(c).read(h,b,n,a);},
   [](void*c,uint32_t h,const void*b,uint32_t n,uint32_t*a){return self(c).write(h,b,n,a);},
   [](void*c,uint32_t h,uint64_t o){return self(c).seek(h,o);},
   [](void*c,uint32_t h,uint64_t*n,uint64_t*o){return self(c).info(h,n,o);},
   [](void*c,uint32_t h){return self(c).sync(h);},
   [](void*c,uint32_t,uint64_t)->int32_t{return self(c).unavailable();},
   [](void*c,const char*,risc_storage_files_stat_v1*)->int32_t{return self(c).unavailable();},
   [](void*c,const char*)->int32_t{return self(c).unavailable();},
   [](void*c,const char*,const char*)->int32_t{return self(c).unavailable();},
   [](void*c,const char*p){return self(c).remove(p);},
   [](void*c,const char*,uint32_t*h)->int32_t{if(h)*h=0;return self(c).unavailable();},
   [](void*c,uint32_t,risc_storage_files_entry_v1*)->int32_t{return self(c).unavailable();},
   [](void*c,uint32_t)->int32_t{return self(c).unavailable();},
   [](void*c,uint32_t)->int32_t{return self(c).unavailable();},
   [](void*c,uint32_t,uint64_t*)->int32_t{return self(c).unavailable();},
   [](void*c,uint32_t,uint64_t)->int32_t{return self(c).unavailable();}};
 }
 const risc_storage_files_v1* api()const{return &api_;}
 bool retained()const{return retained_;}
 bool exitSafe()const{return !retained_&&!token_;}
 static constexpr uint64_t maximumSize(){return uint64_t(std::numeric_limits<off_t>::max());}
private:
 const char*path_;bool terminalIo_,successErrno_,retained_=false;int fd_=-1;uint32_t token_=0,generation_=0,flags_=0;risc_storage_files_v1 api_{};
 static NativeScratchFiles& self(void*c){return *static_cast<NativeScratchFiles*>(c);}
 bool name(const char*p)const{return RiscStorage::validPath(path_)&&p&&!strcmp(p,"/.risc-file-source");}
 bool badSuccess()const{return successErrno_&&errno!=0;}
 int32_t unavailable()const{return retained_?RISC_FILES_RETAINED:RISC_FILES_UNAVAILABLE;}
 int32_t retain(){retained_=true;return RISC_FILES_RETAINED;}
 int32_t error(){if(errno==ENOSPC||errno==EDQUOT)return RISC_FILES_NO_SPACE;if(errno==ENOENT)return RISC_FILES_NOT_FOUND;if(errno==EEXIST)return RISC_FILES_EXISTS;return terminalIo_?retain():int32_t(RISC_FILES_IO);}
 int32_t gate(uint32_t h)const{return retained_?RISC_FILES_RETAINED:!h||h!=token_?RISC_FILES_INVALID:RISC_FILES_OK;}
 int32_t open(const char*p,uint32_t flags,uint32_t*out){
  if(out)*out=0;if(retained_)return RISC_FILES_RETAINED;
  if(!out||!name(p)||!RiscStorage::validFlags(flags))return RISC_FILES_INVALID;
  if(flags&~uint32_t(RISC_FILES_READ|RISC_FILES_WRITE|RISC_FILES_CREATE|RISC_FILES_EXCLUSIVE))return RISC_FILES_UNAVAILABLE;
  if((flags&RISC_FILES_CREATE)&&!(flags&RISC_FILES_EXCLUSIVE))return RISC_FILES_UNAVAILABLE;
  if(token_||generation_==UINT32_MAX)return RISC_FILES_BUSY;
  int mode=flags&RISC_FILES_WRITE?(flags&RISC_FILES_READ?O_RDWR:O_WRONLY):O_RDONLY;
  if(flags&RISC_FILES_CREATE)mode|=O_CREAT|O_EXCL;
#ifdef O_NOFOLLOW
  mode|=O_NOFOLLOW;
#endif
  errno=0;fd_=::open(path_,mode,0600);
  if(fd_>=0&&badSuccess())return retain();
  if(fd_<0)return error();
  token_=++generation_;flags_=flags;
  uint64_t size=0,at=0;const int32_t rc=info(token_,&size,&at);
  if(rc){if(retained_)return rc;const int32_t closed=close(token_);return closed?closed:rc;}
  *out=token_;return RISC_FILES_OK;
 }
 int32_t close(uint32_t h){const int32_t rc=gate(h);if(rc)return rc;errno=0;if(::close(fd_)||badSuccess())return retain();fd_=-1;token_=flags_=0;return RISC_FILES_OK;}
 int32_t info(uint32_t h,uint64_t*n,uint64_t*o){
  const int32_t rc=gate(h);if(rc)return rc;if(!n||!o)return RISC_FILES_INVALID;
  struct stat st{};errno=0;if(fstat(fd_,&st)||badSuccess()||!S_ISREG(st.st_mode)||st.st_size<0)return terminalIo_?retain():int32_t(RISC_FILES_IO);
  errno=0;const off_t at=lseek(fd_,0,SEEK_CUR);if(at<0||badSuccess())return terminalIo_?retain():int32_t(RISC_FILES_IO);
  *n=uint64_t(st.st_size);*o=uint64_t(at);return RISC_FILES_OK;
 }
 int32_t seek(uint32_t h,uint64_t offset){const int32_t rc=gate(h);if(rc)return rc;if(offset>maximumSize())return RISC_FILES_INVALID;
  uint64_t n=0,o=0;const int32_t inspect=info(h,&n,&o);if(inspect)return inspect;if(offset>n)return RISC_FILES_INVALID;
  errno=0;const off_t at=lseek(fd_,off_t(offset),SEEK_SET);if(at!=off_t(offset)||badSuccess())return terminalIo_?retain():int32_t(RISC_FILES_IO);return RISC_FILES_OK;}
 int32_t read(uint32_t h,void*b,uint32_t n,uint32_t*a){if(a)*a=0;const int32_t rc=gate(h);if(rc)return rc;if(!a||(!b&&n)||n>RISC_STORAGE_FILES_IO_MAX)return RISC_FILES_INVALID;if(!(flags_&RISC_FILES_READ))return RISC_FILES_DENIED;
  errno=0;const ssize_t count=::read(fd_,b,n);if(count<0)return error();if(badSuccess()||uint64_t(count)>n)return retain();*a=uint32_t(count);return RISC_FILES_OK;}
 int32_t write(uint32_t h,const void*b,uint32_t n,uint32_t*a){if(a)*a=0;const int32_t rc=gate(h);if(rc)return rc;if(!a||(!b&&n)||n>RISC_STORAGE_FILES_IO_MAX)return RISC_FILES_INVALID;if(!(flags_&RISC_FILES_WRITE))return RISC_FILES_DENIED;
  errno=0;const ssize_t count=::write(fd_,b,n);if(count<0)return error();if(badSuccess()||uint64_t(count)>n)return retain();*a=uint32_t(count);return RISC_FILES_OK;}
 int32_t sync(uint32_t h){const int32_t rc=gate(h);if(rc)return rc;errno=0;if(fsync(fd_)||badSuccess())return error();return RISC_FILES_OK;}
 int32_t remove(const char*p){if(retained_)return RISC_FILES_RETAINED;if(!name(p))return RISC_FILES_INVALID;if(token_)return RISC_FILES_BUSY;
  errno=0;const int rc=unlink(path_);if(rc)return error();if(badSuccess())return retain();return RISC_FILES_OK;}
};
}
