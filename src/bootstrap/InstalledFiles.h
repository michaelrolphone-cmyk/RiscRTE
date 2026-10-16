#pragma once
#include <RiscStorageVolumeV1.h>
#include "MetadataArray.h"
#include <cstdio>
#include <cstring>
#include <climits>
#include <memory>
#include <new>
#include "RuntimeFiles.h"
namespace RiscBoot {
/* Read-only view of admitted package artifacts. Unknown root files, private
 * state, provisioning input and NVS are never enumerated or addressable. All
 * native I/O is synchronous and at most 512 bytes per file-read callback; logical
 * handles retain identity/offset. A failed native close latches retention;
 * successful callbacks never retain a native descriptor. */
class InstalledFiles final {
 public:
  static constexpr size_t PathMax=193;
  struct Name {char value[PathMax]{};};
  bool configure(const char* root,const Name* names,size_t count,const RuntimeFileSources* sources) {
    if(configured_ || !root || root[0]!='/' || strlen(root)>255 || !count || !names)return false;
    if(!sources || sources->retained() || !sources->find(root))return false;
    auto copy=metadataArray<Name>(count);if(!copy)return false;
    for(size_t i=0;i<count;i++) {
      if(!valid(names[i].value,false))return false;
      for(size_t j=0;j<i;j++)if(!strcmp(names[j].value,names[i].value))return false;
      copy[i]=names[i];
    }
    sources_=sources;root_=root;names_=std::move(copy);count_=count;configured_=true;return true;
  }
  bool configured()const{return configured_;}
  bool cloneConfiguration(const InstalledFiles& source) {
    return source.configured_ && configure(source.root_,source.names_.get(),source.count_,source.sources_);
  }
  bool retained()const{return retained_ || (sources_ && sources_->retained());}
  bool end(){if(retained())return false;directory_=file_=0;directoryPath_[0]=last_[0]=filePath_[0]=0;offset_=0;error_=nullptr;return true;}
  bool refresh(){if(retained())return false;error_=nullptr;return configured_;}
  bool ready()const{return configured_ && !retained();}
  bool label(char*out,size_t n){return copy(out,n,"Installed files (read-only)");}
  bool error(char*out,size_t n)const{return copy(out,n,error_?error_:"");}
  bool stat(const char* path,uint64_t*size,bool*directory) {
    if(retained())return false;
    error_=nullptr;if(size)*size=0;if(directory)*directory=false;
    if(!size || !directory || !valid(path,true))return fail("Invalid file path");
    if(isDirectory(path)){*directory=true;return true;}
    if(!admitted(path))return fail("File is outside installed inventory");
    risc_storage_files_identity_value_v1 s{};if(!inspect(path,s))return false;*size=static_cast<uint64_t>(s.size);return true;
  }
  uint32_t dirOpen(const char*path) {
    if(retained())return false;
    error_=nullptr;if(directory_ || !valid(path,true) || !isDirectory(path) || !next_)return bad("Cannot open folder");
    if(!copy(directoryPath_,sizeof(directoryPath_),path))return bad("Path too long");
    last_[0]=0;directory_=next_++;return directory_;
  }
  bool dirNext(uint32_t handle,risc_storage_dirent_v1*out) {
    if(retained())return false;
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
    if(!folder){char path[PathMax+1];if(snprintf(path,sizeof(path),"%s%s%s",directoryPath_,prefix?"/":"",best)>=static_cast<int>(sizeof(path)))return fail("Path too long");risc_storage_files_identity_value_v1 s{};if(!inspect(path,s))return false;entry.size=static_cast<uint64_t>(s.size);}
    strcpy(last_,best);*out=entry;return true;
  }
  void dirClose(uint32_t handle){if(retained())return;error_=nullptr;if(handle && handle==directory_){directory_=0;directoryPath_[0]=last_[0]=0;}else fail("Folder handle expired");}
  uint32_t fileOpen(const char*path,uint64_t*size) {
    if(retained())return false;
    error_=nullptr;if(size)*size=0;
    if(file_ || !size || !valid(path,true) || !admitted(path) || !next_)return bad("Cannot open file");
    if(!inspect(path,identity_) || !copy(filePath_,sizeof(filePath_),path))return 0;
    offset_=0;*size=static_cast<uint64_t>(identity_.size);file_=next_++;return file_;
  }
  size_t fileRead(uint32_t handle,void*out,size_t capacity) {
    if(retained())return false;
    error_=nullptr;if(!handle || handle!=file_ || (!out && capacity))return bad("File handle expired");
    if(!capacity || offset_>=static_cast<uint64_t>(identity_.size))return 0;
    risc_storage_files_identity_value_v1 before{};if(!inspect(filePath_,before))return 0;
    if(!sameFileIdentity(before,identity_))return bad("File changed. Reopen it.");
    char absolute[512];if(!absolutePath(filePath_,absolute,sizeof(absolute)))return 0;
    auto source=view();uint32_t native=0;
    if(source.open(absolute,&native)!=RISC_FILES_OK)return bad("Could not read installed file");
    risc_storage_files_identity_value_v1 opened{};opened.struct_size=sizeof(opened);
    bool ok=source.identity(native,&opened)==RISC_FILES_OK && sameFileIdentity(opened,identity_) && source.seek(native,offset_)==RISC_FILES_OK;
    unsigned char bytes[512];size_t wanted=capacity<sizeof(bytes)?capacity:sizeof(bytes);
    uint64_t left=identity_.size-offset_;if(left<wanted)wanted=static_cast<size_t>(left);
    uint32_t got=0;
    while(ok && got<wanted){uint32_t part=0;
      ok=source.read(native,bytes+got,uint32_t(wanted-got),&part)==RISC_FILES_OK && part>0;
      got+=part;
    }
    risc_storage_files_identity_value_v1 after{};after.struct_size=sizeof(after);
    ok=ok && source.identity(native,&after)==RISC_FILES_OK && sameFileIdentity(after,identity_);
    if(source.close(native)!=RISC_FILES_OK){retained_=true;return bad("Storage close failed. Restart needed.");}
    if(!ok || retained())return bad("File changed or read failed");
    memcpy(out,bytes,got);offset_+=got;return got;
  }
  bool fileClose(uint32_t handle){if(retained())return false;error_=nullptr;if(!handle || handle!=file_)return fail("File handle expired");file_=0;filePath_[0]=0;offset_=0;return true;}
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
  RuntimeFileView view()const{return RuntimeFileView(sources_,root_);}
  bool inspect(const char*path,risc_storage_files_identity_value_v1&out) {
    char full[512];if(!admitted(path) || !absolutePath(path,full,sizeof(full)))return fail("File is outside installed inventory");
    out={};out.struct_size=sizeof(out);
    if(view().identity(full,&out)!=RISC_FILES_OK || out.size>LONG_MAX)return fail("Installed file unavailable");
    return true;
  }
  const RuntimeFileSources* sources_=nullptr;
  const char*root_=nullptr;MetadataArray<Name>names_;size_t count_=0;bool configured_=false,retained_=false;
  uint32_t next_=1,directory_=0,file_=0;uint64_t offset_=0;risc_storage_files_identity_value_v1 identity_{};
  char directoryPath_[PathMax+1]{},last_[RISC_STORAGE_VOLUME_NAME_MAX]{},filePath_[PathMax+1]{};
  const char*error_=nullptr;
};
}
