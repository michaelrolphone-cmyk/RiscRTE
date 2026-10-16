#include "ReopenedFiles.h"
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace RiscStorage {
ReopenedFiles::ReopenedFiles(Hooks hooks):hooks_(hooks){
  api_={1,sizeof(api_),this,
    [](void*c,const char*p,uint32_t f,uint32_t*h){return static_cast<ReopenedFiles*>(c)->open(p,f,h);},
    [](void*c,uint32_t h){return static_cast<ReopenedFiles*>(c)->close(h);},
    [](void*c,uint32_t h,void*b,uint32_t n,uint32_t*a){return static_cast<ReopenedFiles*>(c)->read(h,b,n,a);},
    [](void*c,uint32_t h,const void*b,uint32_t n,uint32_t*a){return static_cast<ReopenedFiles*>(c)->write(h,b,n,a);},
    [](void*c,uint32_t h,uint64_t o){return static_cast<ReopenedFiles*>(c)->seek(h,o);},
    [](void*c,uint32_t h,uint64_t*n,uint64_t*o){return static_cast<ReopenedFiles*>(c)->info(h,n,o);},
    [](void*c,uint32_t h){return static_cast<ReopenedFiles*>(c)->sync(h);},
    [](void*c,uint32_t h,uint64_t n){return static_cast<ReopenedFiles*>(c)->truncate(h,n);},
    [](void*c,const char*p,risc_storage_files_stat_v1*s){return static_cast<ReopenedFiles*>(c)->stat(p,s);},
    [](void*c,const char*p){return static_cast<ReopenedFiles*>(c)->mkdir(p);},
    [](void*,const char*,const char*)->int32_t{return RISC_FILES_UNAVAILABLE;},
    [](void*c,const char*p){return static_cast<ReopenedFiles*>(c)->remove(p);},
    [](void*c,const char*p,uint32_t*h){return static_cast<ReopenedFiles*>(c)->dirOpen(p,h);},
    [](void*c,uint32_t h,risc_storage_files_entry_v1*e){return static_cast<ReopenedFiles*>(c)->dirNext(h,e);},
    [](void*c,uint32_t h){return static_cast<ReopenedFiles*>(c)->dirClose(h);},
    [](void*c,uint32_t h){return static_cast<ReopenedFiles*>(c)->dirSeek(h,0);},
    [](void*c,uint32_t h,uint64_t*o){return static_cast<ReopenedFiles*>(c)->dirTell(h,o);},
    [](void*c,uint32_t h,uint64_t o){return static_cast<ReopenedFiles*>(c)->dirSeek(h,o);}};
}
bool ReopenedFiles::configure(const char* root){
  if(configured_||!validPath(root)||!strcmp(root,"/")||strlen(root)>=sizeof(root_)||
     !hooks_.enter||!hooks_.leave||!hooks_.retained||!hooks_.retain||!hooks_.usage)return false;
  strcpy(root_,root);configured_=true;return true;
}
int32_t ReopenedFiles::retain(){retained_=true;hooks_.retain(hooks_.context);return RISC_FILES_RETAINED;}
int32_t ReopenedFiles::error(){
  if(retained()||errno==EIO)return retain();
  return errno==ENOENT?RISC_FILES_NOT_FOUND:errno==EEXIST?RISC_FILES_EXISTS:
    errno==ENOSPC?RISC_FILES_NO_SPACE:errno==EBUSY?RISC_FILES_BUSY:
    errno==EACCES||errno==EPERM?RISC_FILES_DENIED:RISC_FILES_IO;
}
bool ReopenedFiles::path(const char* name,char* out,size_t capacity)const{
  if(!validPath(name))return false;
  const int n=snprintf(out,capacity,"%s%s",root_,strcmp(name,"/")?name:"");
  return n>0&&size_t(n)<capacity;
}
int32_t ReopenedFiles::space(uint64_t growth){
  uint64_t total=0,used=0;
  if(!hooks_.usage(hooks_.context,&total,&used)||used>total)return retained()?retain():int32_t(RISC_FILES_BUSY);
  const uint64_t free=total-used;
  return free>=ReservedBytes+4096 && growth<=free-ReservedBytes-4096?RISC_FILES_OK:RISC_FILES_NO_SPACE;
}
int32_t ReopenedFiles::inspect(const char* full,risc_storage_files_stat_v1* out){
  struct ::stat st{};errno=0;if(::stat(full,&st))return error();
  if((!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode))||st.st_size<0)return RISC_FILES_DENIED;
  *out={sizeof(*out),S_ISDIR(st.st_mode)?uint32_t(RISC_FILES_DIRECTORY):0u,uint64_t(st.st_size),0};return RISC_FILES_OK;
}
int32_t ReopenedFiles::closePhysical(int32_t result,bool writing){
  if(retained())return retain(); // Never retry a physical operation after custody loss.
  if(descriptor_<0)return result;
  if(writing){errno=0;if(::fsync(descriptor_))return retain();}
  errno=0;if(::close(descriptor_))return retain();
  descriptor_=-1;return retained()?retain():result;
}
int32_t ReopenedFiles::closeDirectory(int32_t result){
  if(retained())return retain();
  if(!directory_)return result;
  errno=0;if(::closedir(static_cast<DIR*>(directory_)))return retain();
  directory_=nullptr;return retained()?retain():result;
}
int32_t ReopenedFiles::provision(){return call([&]()->int32_t{
  if(token_)return RISC_FILES_BUSY;
  risc_storage_files_stat_v1 st{};int32_t rc=inspect(root_,&st);
  if(rc==RISC_FILES_NOT_FOUND){rc=space(0);if(rc)return rc;errno=0;if(::mkdir(root_,0700))return error();rc=inspect(root_,&st);}
  if(rc)return rc;
  return (st.flags&RISC_FILES_DIRECTORY)?RISC_FILES_OK:RISC_FILES_DENIED;
});}
int32_t ReopenedFiles::reopen(bool writing){
  char full[512];if(!path(selected_,full,sizeof(full)))return RISC_FILES_INVALID;
  errno=0;descriptor_=::open(full,writing?O_WRONLY:O_RDONLY,0600);
  if(descriptor_<0)return error();
  struct ::stat st{};errno=0;
  if(fstat(descriptor_,&st))return closePhysical(error());
  // Only this provider can mutate its private root. A changed object is an
  // authority breach, not permission to seek into a replacement file.
  if(!S_ISREG(st.st_mode)||st.st_size<0||uint64_t(st.st_size)!=size_)return retain();
  if(offset_>size_||offset_>LONG_MAX)return closePhysical(RISC_FILES_INVALID);
  errno=0;const off_t at=lseek(descriptor_,off_t(offset_),SEEK_SET);
  if(at<0)return closePhysical(error());
  if(uint64_t(at)!=offset_)return retain();
  return RISC_FILES_OK;
}
int32_t ReopenedFiles::open(const char* name,uint32_t flags,uint32_t* out){
  if(out)*out=0;
  return call([&]()->int32_t{
    char full[512];if(!out||!path(name,full,sizeof(full))||!strcmp(name,"/")||!validFlags(flags))return RISC_FILES_INVALID;
    if(token_||generation_==UINT32_MAX)return RISC_FILES_BUSY;
    risc_storage_files_stat_v1 st{};int32_t rc=inspect(full,&st);
    if(rc==RISC_FILES_OK&&(st.flags&RISC_FILES_DIRECTORY))return RISC_FILES_DENIED;
    if(rc!=RISC_FILES_OK&&rc!=RISC_FILES_NOT_FOUND)return rc;
    if(rc==RISC_FILES_NOT_FOUND&&!(flags&RISC_FILES_CREATE))return rc;
    if(rc==RISC_FILES_OK&&(flags&(RISC_FILES_CREATE|RISC_FILES_EXCLUSIVE))==(RISC_FILES_CREATE|RISC_FILES_EXCLUSIVE))return RISC_FILES_EXISTS;
    if(flags&RISC_FILES_WRITE){int32_t capacity=space(0);if(capacity)return capacity;}
    int mode=flags&RISC_FILES_WRITE?((flags&RISC_FILES_READ)?O_RDWR:O_WRONLY):O_RDONLY;
    if(flags&RISC_FILES_CREATE)mode|=O_CREAT;
    if(flags&RISC_FILES_EXCLUSIVE)mode|=O_EXCL;
    if(flags&RISC_FILES_TRUNCATE)mode|=O_TRUNC;
    errno=0;descriptor_=::open(full,mode,0600);if(descriptor_<0)return error();
    struct ::stat opened{};errno=0;
    if(fstat(descriptor_,&opened))rc=error();
    else rc=(!S_ISREG(opened.st_mode)||opened.st_size<0)?RISC_FILES_DENIED:RISC_FILES_OK;
    rc=closePhysical(rc,flags&RISC_FILES_WRITE);if(rc)return rc;
    strcpy(selected_,name);flags_=flags;size_=uint64_t(opened.st_size);offset_=flags&RISC_FILES_APPEND?size_:0;
    isDirectory_=false;token_=++generation_;*out=token_;return RISC_FILES_OK;
  });
}
int32_t ReopenedFiles::close(uint32_t token){return call([&]()->int32_t{if(!file(token))return RISC_FILES_INVALID;token_=0;return RISC_FILES_OK;});}
int32_t ReopenedFiles::read(uint32_t token,void* bytes,uint32_t capacity,uint32_t* actual){
  if(actual)*actual=0;
  return call([&]()->int32_t{
    if(!file(token)||!actual||(!bytes&&capacity)||capacity>RISC_STORAGE_FILES_IO_MAX)return RISC_FILES_INVALID;
    if(!(flags_&RISC_FILES_READ))return RISC_FILES_DENIED;
    int32_t rc=reopen(false);if(rc)return rc;
    errno=0;ssize_t got=::read(descriptor_,bytes,capacity);
    if(got<0)rc=error();else if(uint64_t(got)>capacity)return retain();
    else{*actual=uint32_t(got);offset_+=uint64_t(got);}
    return closePhysical(rc);
  });
}
int32_t ReopenedFiles::write(uint32_t token,const void* bytes,uint32_t length,uint32_t* actual){
  if(actual)*actual=0;
  return call([&]()->int32_t{
    if(!file(token)||!actual||(!bytes&&length)||length>RISC_STORAGE_FILES_IO_MAX)return RISC_FILES_INVALID;
    if(!(flags_&RISC_FILES_WRITE))return RISC_FILES_DENIED;
    if(flags_&RISC_FILES_APPEND)offset_=size_;
    int32_t rc=space(length);if(rc)return rc;
    rc=reopen(true);if(rc)return rc;
    errno=0;ssize_t got=::write(descriptor_,bytes,length);
    if(got<0)rc=error();else if(uint64_t(got)>length)return retain();
    else{*actual=uint32_t(got);offset_+=uint64_t(got);if(offset_>size_)size_=offset_;}
    return closePhysical(rc,true);
  });
}
int32_t ReopenedFiles::seek(uint32_t token,uint64_t offset){return call([&]()->int32_t{
  if(!file(token)||offset>size_||offset>LONG_MAX)return RISC_FILES_INVALID;
  offset_=offset;return RISC_FILES_OK;
});}
int32_t ReopenedFiles::info(uint32_t token,uint64_t* size,uint64_t* offset){return call([&]()->int32_t{
  if(!file(token)||!size||!offset)return RISC_FILES_INVALID;
  *size=size_;*offset=offset_;return RISC_FILES_OK;
});}
int32_t ReopenedFiles::sync(uint32_t token){return call([&]()->int32_t{
  if(!file(token))return RISC_FILES_INVALID;
  int32_t rc=reopen(flags_&RISC_FILES_WRITE);return rc?rc:closePhysical(RISC_FILES_OK,flags_&RISC_FILES_WRITE);
});}
int32_t ReopenedFiles::truncate(uint32_t token,uint64_t size){return call([&]()->int32_t{
  if(!file(token)||size>size_||size>LONG_MAX)return RISC_FILES_INVALID;
  if(!(flags_&RISC_FILES_WRITE))return RISC_FILES_DENIED;
  int32_t rc=reopen(true);if(rc)return rc;
  errno=0;if(ftruncate(descriptor_,off_t(size)))rc=error();else{size_=size;if(offset_>size_)offset_=size_;}
  return closePhysical(rc,true);
});}
int32_t ReopenedFiles::stat(const char* name,risc_storage_files_stat_v1* out){return call([&]()->int32_t{
  char full[512];if(!out||out->struct_size<sizeof(*out)||!path(name,full,sizeof(full)))return RISC_FILES_INVALID;return inspect(full,out);
});}
int32_t ReopenedFiles::mkdir(const char* name){return call([&]()->int32_t{
  char full[512];if(!path(name,full,sizeof(full))||!strcmp(name,"/"))return RISC_FILES_INVALID;
  if(token_)return RISC_FILES_BUSY;
  int32_t rc=space(0);if(rc)return rc;
  errno=0;return ::mkdir(full,0700)?error():int32_t(RISC_FILES_OK);
});}
int32_t ReopenedFiles::remove(const char* name){return call([&]()->int32_t{
  char full[512];if(!path(name,full,sizeof(full))||!strcmp(name,"/"))return RISC_FILES_INVALID;
  if(token_)return RISC_FILES_BUSY;
  risc_storage_files_stat_v1 st{};int32_t rc=inspect(full,&st);if(rc)return rc;
  if(st.flags&RISC_FILES_DIRECTORY)return RISC_FILES_DENIED; // Never remove folders.
  errno=0;return ::unlink(full)?error():int32_t(RISC_FILES_OK);
});}
int32_t ReopenedFiles::dirOpen(const char* name,uint32_t* out){
  if(out)*out=0;
  return call([&]()->int32_t{
    char full[512];if(!out||!path(name,full,sizeof(full)))return RISC_FILES_INVALID;
    if(token_||generation_==UINT32_MAX)return RISC_FILES_BUSY;
    errno=0;directory_=::opendir(full);if(!directory_)return error();
    int32_t rc=closeDirectory(RISC_FILES_OK);if(rc)return rc;
    strcpy(selected_,name);isDirectory_=true;offset_=0;token_=++generation_;*out=token_;return RISC_FILES_OK;
  });
}
int32_t ReopenedFiles::dirNext(uint32_t token,risc_storage_files_entry_v1* out){return call([&]()->int32_t{
  if(!dir(token)||!out||out->struct_size<sizeof(*out))return RISC_FILES_INVALID;
  char full[512];if(!path(selected_,full,sizeof(full)))return RISC_FILES_INVALID;
  errno=0;directory_=::opendir(full);if(!directory_)return error();
  uint32_t seen=0;dirent* entry=nullptr;
  // Replaying a capped count is valid across fresh directory streams; portable
  // telldir cookies are not. The exclusive root cannot change under this handle.
  for(uint32_t steps=0;steps<DirectoryLimit+3;++steps){
    errno=0;entry=::readdir(static_cast<DIR*>(directory_));
    if(!entry)return closeDirectory(errno?error():int32_t(RISC_FILES_END));
    if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
    if(seen++==offset_)break;
    entry=nullptr;
  }
  if(!entry||offset_>=DirectoryLimit)return closeDirectory(RISC_FILES_UNAVAILABLE);
  if(strlen(entry->d_name)>=sizeof(out->name))return closeDirectory(RISC_FILES_INVALID);
  char child[512];const int n=snprintf(child,sizeof(child),"%s/%s",full,entry->d_name);
  if(n<0||size_t(n)>=sizeof(child))return closeDirectory(RISC_FILES_INVALID);
  risc_storage_files_stat_v1 st{};int32_t rc=inspect(child,&st);
  if(!rc){*out={};out->struct_size=sizeof(*out);out->flags=st.flags;out->size=st.size;strcpy(out->name,entry->d_name);++offset_;}
  return closeDirectory(rc);
});}
int32_t ReopenedFiles::dirClose(uint32_t token){return call([&]()->int32_t{if(!dir(token))return RISC_FILES_INVALID;token_=0;return RISC_FILES_OK;});}
int32_t ReopenedFiles::dirSeek(uint32_t token,uint64_t offset){return call([&]()->int32_t{if(!dir(token)||offset>DirectoryLimit)return RISC_FILES_INVALID;
  offset_=offset;return RISC_FILES_OK;});}
int32_t ReopenedFiles::dirTell(uint32_t token,uint64_t* offset){return call([&]()->int32_t{if(!dir(token)||!offset)return RISC_FILES_INVALID;*offset=offset_;return RISC_FILES_OK;});}
}
