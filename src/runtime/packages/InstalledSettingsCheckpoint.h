#pragma once
#include <RiscInstallationV1.h>
#include <RiscKeyValueV1.h>
#include <RiscStoragePackageAsyncV1.h>
#include <cstring>
#include <cstdio>
namespace RiscPackages {
// Scoped copied-worker checkpoint. Native restore establishes a durable journal
// before this coordinator mutates SD. Ack is after exact readback AND checked
// worker close; only then may native discard the journal's value payload.
class InstalledSettingsCheckpoint {
 public:
  enum class State {Idle,Refresh,Recover,Read,EndRead,Restore,Snapshot,BeginWrite,RefreshWrite,RecoverWrite,Root,Directory,Remove,Write,Replace,Verify,EndWrite,Acknowledge,Ending,Done,Failed,Cancelled,Retained};
  bool start(const risc_installation_v1*n,const risc_storage_volume_api_v1*v,uint32_t slot,const char*owner){
   if(state_!=State::Idle||!n||n->struct_size<sizeof(*n)||!n->settings_capacity||slot<1||slot>n->settings_capacity||
      !n->settings_restore||!n->settings_status||!n->settings_snapshot||!n->settings_ack||!risc_storage_volume_download(v)||
      !owner||!*owner||std::strlen(owner)>=sizeof(owner_)||std::strchr(owner,'/')||std::strchr(owner,'\\')||std::strstr(owner,"..")||!std::strcmp(owner,"."))return false;
   native_=n;volume_=v;package_=risc_storage_volume_packages(v);slot_=slot;std::strcpy(owner_,owner);
   std::snprintf(directory_,sizeof(directory_),"/AppData/%s",owner);
   std::snprintf(path_,sizeof(path_),"%s/settings.kv",directory_);std::snprintf(partial_,sizeof(partial_),"%s/.settings.next",directory_);
   if(package_->base.begin(v->context,&session_)!=RISC_STORAGE_ASYNC_OK)return false;
   state_=State::Refresh;return true;
  }
  State state()const{return state_;}
  void cancel(){if(terminal()||state_==State::Idle)return;cancelled_=true;state_=State::Ending;}
  void step(){
   if(terminal()||state_==State::Idle)return;
   if(state_==State::EndRead||state_==State::EndWrite||state_==State::Ending){
    if(session_){const int rc=package_->base.end(volume_->context,session_);if(rc==RISC_STORAGE_ASYNC_BUSY||rc==RISC_STORAGE_ASYNC_PENDING)return;
     if(rc!=RISC_STORAGE_ASYNC_OK){state_=State::Retained;return;}session_=operation_=0;}
    state_=state_==State::EndRead?State::Restore:state_==State::EndWrite?State::Acknowledge:cancelled_?State::Cancelled:State::Failed;return;
   }
   if(state_==State::Restore){
    int rc=native_->settings_restore(native_->context,slot_,owner_,missing_?nullptr:result_.data,missing_?0:result_.size,missing_);
    if(rc==RISC_KEY_VALUE_BUSY)return;
    if(rc!=RISC_KEY_VALUE_OK){state_=State::Failed;return;}
    const auto status=native_->settings_status(native_->context,slot_);
    state_=(status==1||status==4)?State::Done:State::Snapshot;return;
   }
   if(state_==State::Snapshot){
    int rc=native_->settings_snapshot(native_->context,slot_,request_.data,sizeof(request_.data),&size_,&generation_);
    if(rc==RISC_KEY_VALUE_BUSY)return;
    if(rc!=RISC_KEY_VALUE_OK||!size_||size_>sizeof(request_.data)){state_=State::Failed;return;}
    state_=State::BeginWrite;return;
   }
   if(state_==State::Acknowledge){
    const int rc=native_->settings_ack(native_->context,slot_,generation_);
    // A newer app write deliberately makes this checkpoint obsolete. Its
    // journal remains dirty and a later owner pass will checkpoint that value.
    if(rc==RISC_KEY_VALUE_BUSY){state_=State::Done;return;}
    state_=rc==RISC_KEY_VALUE_OK?State::Done:State::Failed;return;
   }
   if(state_==State::BeginWrite){
    int rc=package_->base.begin(volume_->context,&session_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;
    if(rc!=RISC_STORAGE_ASYNC_OK){state_=State::Failed;return;}state_=State::RefreshWrite;return;
   }
   if(!operation_){
    int rc;
    if(state_==State::Refresh||state_==State::RefreshWrite||state_==State::Read||state_==State::Verify||state_==State::Remove){
     risc_storage_async_request_v1 q{};
     q.operation=(state_==State::Refresh||state_==State::RefreshWrite)?RISC_STORAGE_ASYNC_REFRESH:state_==State::Remove?RISC_STORAGE_ASYNC_REMOVE_FILE:RISC_STORAGE_ASYNC_READ_SMALL_FILE;
     std::strcpy(q.path,state_==State::Remove?partial_:path_);rc=package_->base.submit(volume_->context,session_,&q,&operation_);
    }else{
     request_.operation=(state_==State::Recover||state_==State::RecoverWrite)?RISC_STORAGE_PACKAGE_RECOVER:
      (state_==State::Root||state_==State::Directory)?RISC_STORAGE_PACKAGE_MKDIR:state_==State::Write?RISC_STORAGE_PACKAGE_WRITE_SMALL:RISC_STORAGE_PACKAGE_REPLACE;
     const char*path=(state_==State::Recover||state_==State::RecoverWrite)?path_:state_==State::Root?"/AppData":state_==State::Directory?directory_:partial_;
     std::strcpy(request_.path,path);std::strcpy(request_.destination,path_);request_.size=size_;
     rc=package_->submit_package(volume_->context,session_,&request_,&operation_);
    }
    if(rc==RISC_STORAGE_ASYNC_BUSY)return;
    if(rc!=RISC_STORAGE_ASYNC_OK){fail(rc);return;}return;
   }
   const int rc=package_->base.collect(volume_->context,session_,operation_,&result_);
   if(rc==RISC_STORAGE_ASYNC_BUSY||rc==RISC_STORAGE_ASYNC_PENDING)return;
   operation_=0;
   if(state_==State::Read&&rc==RISC_STORAGE_ASYNC_NOT_FOUND){missing_=true;state_=State::EndRead;return;}
   if(rc!=RISC_STORAGE_ASYNC_OK){fail(rc);return;}
   switch(state_){
    case State::Refresh:state_=State::Recover;break;
    case State::Recover:state_=State::Read;break;
    case State::Read:state_=State::EndRead;break;
    case State::RefreshWrite:state_=State::RecoverWrite;break;
    case State::RecoverWrite:state_=State::Root;break;
    case State::Root:state_=State::Directory;break;
    case State::Directory:state_=State::Remove;break;
    case State::Remove:state_=State::Write;break;
    case State::Write:state_=State::Replace;break;
    case State::Replace:state_=State::Verify;break;
    case State::Verify:if(result_.size!=size_||std::memcmp(result_.data,request_.data,size_)){fail(RISC_STORAGE_ASYNC_IO);return;}state_=State::EndWrite;break;
    default:fail(RISC_STORAGE_ASYNC_INVALID);break;
   }
  }
 private:
  bool terminal()const{return state_==State::Done||state_==State::Failed||state_==State::Cancelled||state_==State::Retained;}
  void fail(int rc){state_=rc==RISC_STORAGE_ASYNC_RETAINED?State::Retained:State::Ending;}
  State state_=State::Idle;const risc_installation_v1*native_=nullptr;const risc_storage_volume_api_v1*volume_=nullptr;
  const risc_storage_volume_api_v1_packages*package_=nullptr;uint32_t slot_=0,size_=0;uint64_t session_=0,operation_=0,generation_=0;
  bool missing_=false,cancelled_=false;char owner_[96]{},directory_[128]{},path_[160]{},partial_[160]{};
  risc_storage_package_request_v1 request_{};risc_storage_async_result_v1 result_{};
};
}
