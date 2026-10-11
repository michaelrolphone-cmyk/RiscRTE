#pragma once
#include <RiscStorageVolumeFsV1.h>
#include <cstdio>
#include <cstring>
namespace RiscPackages {
// Cooperative metadata reader for /Apps/<package>/manifest.json. The owner
// schedules steps after first frame, outside input/display/provider polling.
// Every step performs at most one directory/open/read/checked-close operation.
// The callback owns installation-policy and package admission; this reader
// never hashes/maps an image or derives grants from package declarations.
class InstalledAppDiscovery {
 public:
  enum class State { Idle, Next, Open, CheckMissing, Read, Close, Admit, Closing, Complete, Failed, Retained };
  using Admit=bool (*)(void*,const char* relativeManifest,const char* json);
  bool start(const risc_storage_volume_api_v1* base,Admit admit,void* context) {
    if(state_!=State::Idle || !base || base->api_version!=1 ||
       base->struct_size<sizeof(risc_storage_volume_api_v1_ext) || !admit)return false;
    volume_=reinterpret_cast<const risc_storage_volume_api_v1_ext*>(base);
    if(!base->ready || !base->dir_open || !base->dir_next || !base->file_open_read ||
       !base->file_read || !base->file_close || !volume_->dir_close_checked || !volume_->handle_error ||
       !base->ready(base->context))return false;
    admit_=admit;context_=context;directory_=base->dir_open(base->context,"/Apps");
    state_=directory_?State::Next:State::Failed;return directory_!=0;
  }
  State state()const{return state_;}
  unsigned admitted()const{return admitted_;}
  unsigned rejected()const{return rejected_;}
  void step() {
    if(!volume_)return;
    const auto& v=volume_->base;
    switch(state_) {
      case State::Next: {
        risc_storage_dirent_v1 entry{};
        if(!v.dir_next(v.context,directory_,&entry)) {
          failed_=volume_->handle_error(v.context,directory_,true)!=0;state_=State::Closing;break;
        }
        if(++visited_>128){failed_=true;state_=State::Closing;break;}
        if(!entry.is_directory)break;
        if(!safeName(entry.name,sizeof(entry.name))){++rejected_;break;}
        const int n=std::snprintf(path_,sizeof(path_),"Apps/%s/manifest.json",entry.name);
        if(n<0 || size_t(n)>=sizeof(path_)){++rejected_;break;}
        state_=State::Open;break;
      }
      case State::Open: {
        char absolute[194];std::snprintf(absolute,sizeof(absolute),"/%s",path_);
        file_=v.file_open_read(v.context,absolute,&size_);used_=0;rejectedFile_=false;
        if(!file_){state_=State::CheckMissing;break;}
        if(!size_ || size_>4096){rejectedFile_=true;state_=State::Close;break;}
        state_=State::Read;break;
      }
      case State::CheckMissing: {
        // A confirmed absent manifest is an invalid package, not failed media.
        // Older volumes cannot distinguish absence from an I/O error: preserve
        // the old catalog rather than publishing a possibly incomplete scan.
        const auto*fs=risc_storage_volume_fs(&v);risc_storage_metadata_v1 meta{};
        meta.struct_size=sizeof(meta);char absolute[194];
        std::snprintf(absolute,sizeof(absolute),"/%s",path_);
        if(fs && fs->metadata(v.context,absolute,&meta)==RISC_STORAGE_FS_NOT_FOUND){++rejected_;state_=State::Next;}
        else {failed_=true;state_=State::Closing;}break;
      }
      case State::Read: {
        const size_t remaining=size_t(size_)-used_, request=remaining<512?remaining:512;
        const size_t n=v.file_read(v.context,file_,json_+used_,request);
        if(!n || n>request){failed_=true;state_=State::Close;break;}
        used_+=n;if(used_==size_)state_=State::Close;break;
      }
      case State::Close:
        if(volume_->handle_error(v.context,file_,false))failed_=true;
        if(!v.file_close(v.context,file_,false)){state_=State::Retained;break;}
        file_=0;if(rejectedFile_)++rejected_;
        state_=failed_?State::Closing:rejectedFile_?State::Next:State::Admit;break;
      case State::Admit:
        json_[used_]=0;
        if(std::memchr(json_,0,used_) || !admit_(context_,path_,json_))++rejected_;else ++admitted_;
        state_=State::Next;break;
      case State::Closing:
        if(!volume_->dir_close_checked(v.context,directory_)){state_=State::Retained;break;}
        directory_=0;state_=failed_?State::Failed:State::Complete;break;
      default:break;
    }
  }
  // Explicit cancellation closes only resources still owned. Never retry a
  // retained close; caller must retain the provider/invocation until reset.
  void cancel(){
    if(!volume_ || state_==State::Retained)return;
    failed_=true;if(file_)state_=State::Close;else if(directory_)state_=State::Closing;
    else state_=State::Failed;
  }
 private:
  static bool safeName(const char*s,size_t cap){
    size_t i=0;for(;i<cap&&s[i];++i){char c=s[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;}
    return i&&i<cap&&std::strcmp(s,".")&&std::strcmp(s,"..");
  }
  const risc_storage_volume_api_v1_ext* volume_=nullptr;
  Admit admit_=nullptr;void*context_=nullptr;
  State state_=State::Idle;uint32_t directory_=0,file_=0;
  uint64_t size_=0;size_t used_=0;unsigned visited_=0,admitted_=0,rejected_=0;
  bool failed_=false,rejectedFile_=false;char path_[193]{},json_[4097]{};
};
}
