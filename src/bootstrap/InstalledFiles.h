#pragma once
#include <RiscStorageVolumeV1.h>
#include "MetadataArray.h"
#include <cstdio>
#include <cstring>
#include <climits>
#include <memory>
#include <new>
#include <sys/stat.h>
namespace RiscBoot {
/* Read-only view of admitted package artifacts. Unknown root files, private
 * state, provisioning input and NVS are never enumerated or addressable. All
 * native I/O is synchronous and at most512 bytes per file-read callback; logical
 * handles retain identity/offset, not a native descriptor across callbacks. */
class InstalledFiles final {
 public:
  static constexpr size_t PathMax=193;
  struct Name {char value[PathMax]{};};
  bool configure(const char* root,const Name* names,size_t count) {
    if(configured_ || !root || root[0]!='/' || strlen(root)>255 || !count || !names)return false;
    auto copy=metadataArray<Name>(count);if(!copy)return false;
    for(size_t i=0;i<count;i++) {
      if(!valid(names[i].value,false))return false;
      for(size_t j=0;j<i;j++)if(!strcmp(names[j].value,names[i].value))return false;
      copy[i]=names[i];
    }
    root_=root;names_=std::move(copy);count_=count;configured_=true;return true;
  }
  bool configured()const{return configured_;}
  void end(){directory_=file_=0;directoryPath_[0]=last_[0]=filePath_[0]=0;offset_=0;error_=nullptr;}
  bool refresh(){error_=nullptr;return configured_;}
  bool ready()const{return configured_;}
  bool label(char*out,size_t n){return copy(out,n,"Installed files (read-only)");}
  bool error(char*out,size_t n)const{return copy(out,n,error_?error_:"");}
  bool stat(const char* path,uint64_t*size,bool*directory) {
    error_=nullptr;if(size)*size=0;if(directory)*directory=false;
    if(!size || !directory || !valid(path,true))return fail("Invalid file path");
    if(isDirectory(path)){*directory=true;return true;}
    if(!admitted(path))return fail("File is outside installed inventory");
    struct ::stat s{};if(!inspect(path,s))return false;*size=static_cast<uint64_t>(s.st_size);return true;
  }
  uint32_t dirOpen(const char*path) {
    error_=nullptr;if(directory_ || !valid(path,true) || !isDirectory(path) || !next_)return bad("Cannot open folder");
    if(!copy(directoryPath_,sizeof(directoryPath_),path))return bad("Path too long");
    last_[0]=0;directory_=next_++;return directory_;
  }
  bool dirNext(uint32_t handle,risc_storage_dirent_v1*out) {
    error_=nullptr;if(!handle || handle!=directory_ || !out)return fail("Folder handle expired");
    const size_t prefix=!strcmp(directoryPath_,"/")?0:strlen(directoryPath_);
    char best[RISC_STORAGE_VOLUME_NAME_MAX]{};bool folder=false;
    for(size_t i=0;i<count_;i++) {
      const char* p=names_[i].value;
      if(prefix){if(strncmp(p,directoryPath_+1,prefix-1) || p[prefix-1]!='/')continue;p+=prefix;}
      const char*slash=strchr(p,'/');size_t n=slash?static_cast<size_t>(slash-p):strlen(p);
      if(!n || n>=sizeof(best))return fail("Invalid admitted file name");
      char name[sizeof(best)]{};memcpy(name,p,n);
      if(strcmp(name,last_)<=0 || (best[0] && strcmp(name,best)>=0))continue;
      strcpy(best,name);folder=slash!=nullptr;
    }
    if(!best[0])return false;
    risc_storage_dirent_v1 entry{};strcpy(entry.name,best);entry.is_directory=folder;
    if(!folder){char path[PathMax+1];if(snprintf(path,sizeof(path),"%s%s%s",directoryPath_,prefix?"/":"",best)>=static_cast<int>(sizeof(path)))return fail("Path too long");struct ::stat s{};if(!inspect(path,s))return false;entry.size=static_cast<uint64_t>(s.st_size);}
    strcpy(last_,best);*out=entry;return true;
  }
  void dirClose(uint32_t handle){error_=nullptr;if(handle && handle==directory_){directory_=0;directoryPath_[0]=last_[0]=0;}else fail("Folder handle expired");}
  uint32_t fileOpen(const char*path,uint64_t*size) {
    error_=nullptr;if(size)*size=0;
    if(file_ || !size || !valid(path,true) || !admitted(path) || !next_)return bad("Cannot open file");
    if(!inspect(path,identity_) || !copy(filePath_,sizeof(filePath_),path))return 0;
    offset_=0;*size=static_cast<uint64_t>(identity_.st_size);file_=next_++;return file_;
  }
  size_t fileRead(uint32_t handle,void*out,size_t capacity) {
    error_=nullptr;if(!handle || handle!=file_ || (!out && capacity))return bad("File handle expired");
    if(!capacity || offset_>=static_cast<uint64_t>(identity_.st_size))return 0;
    struct ::stat before{};if(!inspect(filePath_,before))return 0;
    if(!same(before,identity_))return bad("File changed. Reopen it.");
    char absolute[512];if(!absolutePath(filePath_,absolute,sizeof(absolute)))return 0;
    FILE*f=fopen(absolute,"rb");if(!f)return bad("Could not read installed file");
    struct ::stat opened{};bool ok=fstat(fileno(f),&opened)==0 && same(opened,identity_) && fseek(f,static_cast<long>(offset_),SEEK_SET)==0;
    unsigned char bytes[512];size_t wanted=capacity<sizeof(bytes)?capacity:sizeof(bytes);
    uint64_t left=static_cast<uint64_t>(identity_.st_size)-offset_;if(left<wanted)wanted=static_cast<size_t>(left);
    size_t got=ok?fread(bytes,1,wanted,f):0;
    struct ::stat after{};ok=ok && got==wanted && !ferror(f) && fstat(fileno(f),&after)==0 && same(after,identity_);
    if(fclose(f)!=0)ok=false;
    if(!ok)return bad("File changed or read failed");
    memcpy(out,bytes,got);offset_+=got;return got;
  }
  bool fileClose(uint32_t handle){error_=nullptr;if(!handle || handle!=file_)return fail("File handle expired");file_=0;filePath_[0]=0;offset_=0;return true;}
  static bool valid(const char*path,bool leading) {
    if(!path || !*path)return false;
    size_t n=strlen(path);if(n>=PathMax+static_cast<size_t>(leading) || (leading && *path!='/') || (!leading && *path=='/'))return false;
    if(leading && n==1)return true;
    const char*start=path+(leading?1:0),*part=start;
    for(const char*p=start;;p++) {
      unsigned char c=static_cast<unsigned char>(*p);
      if(c && (c<32 || c==127 || c=='\\'))return false;
      if(c=='/' || !c){size_t len=static_cast<size_t>(p-part);if(!len || len>=RISC_STORAGE_VOLUME_NAME_MAX || (len==1 && part[0]=='.') || (len==2 && part[0]=='.' && part[1]=='.'))return false;if(!c)return true;part=p+1;}
    }
  }
 private:
  static bool copy(char*out,size_t cap,const char*s){if(!out || !cap || strlen(s)>=cap)return false;strcpy(out,s);return true;}
  bool fail(const char*s){error_=s;return false;}
  uint32_t bad(const char*s){error_=s;return 0;}
  bool admitted(const char*path)const{for(size_t i=0;i<count_;i++)if(!strcmp(path+1,names_[i].value))return true;return false;}
  bool isDirectory(const char*path)const{
    if(!strcmp(path,"/"))return true;
    size_t n=strlen(path)-1;for(size_t i=0;i<count_;i++)if(!strncmp(path+1,names_[i].value,n) && names_[i].value[n]=='/')return true;return false;
  }
  bool absolutePath(const char*path,char*out,size_t cap){return snprintf(out,cap,"%s%s",root_,path)<static_cast<int>(cap) || fail("Path too long");}
  bool inspect(const char*path,struct ::stat&out) {
    char full[512];if(!admitted(path) || !absolutePath(path,full,sizeof(full)))return fail("File is outside installed inventory");
#ifndef ESP_PLATFORM
    /* POSIX hosts support symlinks; SPIFFS cannot create them. Check each
     * component, including the trusted root, before any file read. */
    for(char*p=full+1;;p++)if(*p=='/' || !*p){char saved=*p;*p=0;struct ::stat component{};int result=lstat(full,&component);*p=saved;if(result || S_ISLNK(component.st_mode))return fail("File path unavailable or aliased");if(!saved)break;}
#endif
    if(::stat(full,&out) || !S_ISREG(out.st_mode) || out.st_size<0 || out.st_size>LONG_MAX)return fail("Installed file unavailable");
    return true;
  }
  static bool same(const struct ::stat&a,const struct ::stat&b){
    bool equal=a.st_dev==b.st_dev && a.st_ino==b.st_ino && a.st_size==b.st_size && a.st_mtime==b.st_mtime && a.st_ctime==b.st_ctime;
#if defined(__linux__) && !defined(ESP_PLATFORM)
    equal=equal && a.st_mtim.tv_nsec==b.st_mtim.tv_nsec && a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
#endif
    return equal;
  }
  const char*root_=nullptr;MetadataArray<Name>names_;size_t count_=0;bool configured_=false;
  uint32_t next_=1,directory_=0,file_=0;uint64_t offset_=0;struct ::stat identity_{};
  char directoryPath_[PathMax+1]{},last_[RISC_STORAGE_VOLUME_NAME_MAX]{},filePath_[PathMax+1]{};
  const char*error_=nullptr;
};
}
