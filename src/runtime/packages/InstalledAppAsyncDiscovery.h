#pragma once
#include <RiscStorageVolumeAsyncV1.h>
#include <cstdio>
#include <cstring>
namespace RiscPackages {
// Owner-side coordinator. Every call is RAM/submit/collect only; the selected
// volume owns copied requests and performs its I/O on the scoped native worker.
// No panel/input state, ELF read, checksum or app identity list appears here.
class InstalledAppAsyncDiscovery {
 public:
  enum class State {Idle,Refresh,Open,Next,Read,Ending,Complete,Failed,Retained};
  using Offer=bool(*)(void*,const char*,const char*);
  bool start(const risc_storage_volume_api_v1*base,Offer offer,void*context){
    if(state_!=State::Idle||!offer)return false;
    api_=risc_storage_volume_async(base);if(!api_)return false;
    base_=base;offer_=offer;context_=context;
    if(api_->begin(base_->context,&session_)!=RISC_STORAGE_ASYNC_OK)return false;
    state_=State::Refresh;return true;
  }
  State state()const{return state_;}
  unsigned admitted()const{return admitted_;}
  unsigned rejected()const{return rejected_;}
  bool ready()const{return ready_;}
  void cancel(){if(state_!=State::Retained&&state_!=State::Idle&&state_!=State::Complete&&state_!=State::Failed){failed_=true;state_=State::Ending;}}
  void step(){
    if(!api_||!session_)return;
    if(state_==State::Ending){
      const int32_t rc=api_->end(base_->context,session_);
      if(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY)return;
      if(rc==RISC_STORAGE_ASYNC_RETAINED){state_=State::Retained;return;}
      session_=operation_=0;state_=!failed_&&rc==RISC_STORAGE_ASYNC_OK?State::Complete:State::Failed;return;
    }
    if(state_==State::Retained)return;
    if(!operation_){
      risc_storage_async_request_v1 q{};
      switch(state_){
       case State::Refresh:q.operation=RISC_STORAGE_ASYNC_REFRESH;break;
       case State::Open:q.operation=RISC_STORAGE_ASYNC_OPEN_DIRECTORY;std::strcpy(q.path,"/Apps");break;
       case State::Next:q.operation=RISC_STORAGE_ASYNC_NEXT_ENTRY;break;
       case State::Read:q.operation=RISC_STORAGE_ASYNC_READ_SMALL_FILE;q.path[0]='/';std::memcpy(q.path+1,path_,std::strlen(path_)+1);break;
       default:return;
      }
      const int32_t rc=api_->submit(base_->context,session_,&q,&operation_);
      if(rc==RISC_STORAGE_ASYNC_BUSY)return;
      if(rc!=RISC_STORAGE_ASYNC_OK){failed_=true;state_=rc==RISC_STORAGE_ASYNC_RETAINED?State::Retained:State::Ending;}
      return;
    }
    const int32_t rc=api_->collect(base_->context,session_,operation_,&result_);
    if(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY)return;
    operation_=0;
    if(rc==RISC_STORAGE_ASYNC_RETAINED){state_=State::Retained;return;}
    switch(state_){
     case State::Refresh:
      if(rc==RISC_STORAGE_ASYNC_OK){ready_=true;state_=State::Open;return;}break;
     case State::Open:
      if(rc==RISC_STORAGE_ASYNC_OK){state_=State::Next;return;}
      if(rc==RISC_STORAGE_ASYNC_NOT_FOUND){state_=State::Ending;return;}break;
     case State::Next:
      if(rc==RISC_STORAGE_ASYNC_END){state_=State::Ending;return;}
      if(rc==RISC_STORAGE_ASYNC_OK){
       if(++visited_>128)break;
       const auto&e=result_.entry;if(!e.is_directory)return;
       if(!safeName(e.name,sizeof(e.name))){++rejected_;return;}
       const int n=std::snprintf(path_,sizeof(path_),"Apps/%s/manifest.json",e.name);
       if(n<0||size_t(n)+1>=sizeof(path_)){++rejected_;return;}
       state_=State::Read;return;
      }break;
     case State::Read:
      if(rc==RISC_STORAGE_ASYNC_INVALID||rc==RISC_STORAGE_ASYNC_NOT_FOUND){++rejected_;state_=State::Next;return;}
      if(rc==RISC_STORAGE_ASYNC_OK&&result_.size&&result_.size<=4096){
       if(std::memchr(result_.data,0,result_.size)){++rejected_;state_=State::Next;return;}
       std::memcpy(json_,result_.data,result_.size);json_[result_.size]=0;
       if(offer_(context_,path_,json_))++admitted_;else ++rejected_;
       state_=State::Next;return;
      }break;
     default:return;
    }
    failed_=true;ready_=false;state_=State::Ending;
  }
 private:
  static bool safeName(const char*s,size_t cap){size_t n=0;for(;n<cap&&s[n];++n){char c=s[n];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return false;}return n&&n<cap&&std::strcmp(s,".")&&std::strcmp(s,"..");}
  const risc_storage_volume_api_v1_async*api_=nullptr;const risc_storage_volume_api_v1*base_=nullptr;
  Offer offer_=nullptr;void*context_=nullptr;uint64_t session_=0,operation_=0;
  State state_=State::Idle;bool failed_=false,ready_=false;unsigned visited_=0,admitted_=0,rejected_=0;
  char path_[193]{},json_[4097]{};risc_storage_async_result_v1 result_{};
};
}
