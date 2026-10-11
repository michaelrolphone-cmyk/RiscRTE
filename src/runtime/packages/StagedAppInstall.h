#pragma once
#include <RiscStoragePackageAsyncV1.h>
#include <cstring>
namespace RiscPackages {
// Owns no app/provider pointer beyond the explicitly borrowed volume/session.
// The caller owns Record until terminal, persists it at Journal before proceed,
// and publishes metadata only after Ready. Restart starts a fresh validation of
// the selected immutable image; no launch path calls this coordinator.
class StagedAppInstall {
 public:
  struct Record {
   char manifestPath[205]{},imagePath[205]{},stagePath[205]{};
   char manifest[4097]{},previous[4097]{};
   uint32_t imageSize=0;uint8_t digest[32]{};bool previousPresent=false;
  };
  enum class State {Idle,Refresh,Recover,Read,Validate,Journal,Clean,Write,Replace,Reconcile,Verify,Ending,Ready,Failed,Cancelled,Retained};
  bool start(const risc_storage_volume_api_v1*v,Record&record,bool resume){
   if(state_!=State::Idle||!v||!record.imageSize||record.imageSize>2097152u||
      !std::memchr(record.manifestPath,0,sizeof(record.manifestPath))||!record.manifestPath[0]||
      !std::memchr(record.stagePath,0,sizeof(record.stagePath))||!record.stagePath[0]||
      !std::memchr(record.imagePath,0,sizeof(record.imagePath))||!record.imagePath[0]||
      !std::memchr(record.manifest,0,sizeof(record.manifest))||!record.manifest[0]||
      !std::memchr(record.previous,0,sizeof(record.previous)))return false;
   package_=risc_storage_volume_packages(v);if(!package_)return false;
   base_=v;record_=&record;resume_=resume;
   if(package_->base.begin(v->context,&session_)!=RISC_STORAGE_ASYNC_OK)return false;
   state_=State::Refresh;return true;
  }
  State state()const{return state_;}
  bool durable()const{return resume_||journaled_;}
  // The only authorization to begin a manifest mutation is the caller's
  // confirmed atomic journal write. A BUSY/ambiguous write cannot call this.
  bool journalCommitted(){if(state_!=State::Journal)return false;journaled_=true;state_=State::Clean;return true;}
  void cancel(){if(state_==State::Ready||state_==State::Failed||state_==State::Retained||state_==State::Idle)return;cancelled_=true;state_=State::Ending;}
  void step(){
   if(!session_||state_==State::Retained||state_==State::Journal)return;
   if(state_==State::Ending){
    int rc=package_->base.end(base_->context,session_);
    if(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY)return;
    if(rc!=RISC_STORAGE_ASYNC_OK){state_=State::Retained;return;}
    session_=operation_=0;state_=cancelled_?State::Cancelled:failed_?State::Failed:State::Ready;return;
   }
   if(!operation_){
    int rc;
    if(state_==State::Refresh||state_==State::Read||state_==State::Clean||state_==State::Verify){
     risc_storage_async_request_v1 q{};
     q.operation=state_==State::Refresh?RISC_STORAGE_ASYNC_REFRESH:state_==State::Clean?RISC_STORAGE_ASYNC_REMOVE_FILE:RISC_STORAGE_ASYNC_READ_SMALL_FILE;
     std::strcpy(q.path,state_==State::Clean?record_->stagePath:record_->manifestPath);
     rc=package_->base.submit(base_->context,session_,&q,&operation_);
    }else{
     request_={};
     request_.operation=state_==State::Validate?RISC_STORAGE_PACKAGE_VALIDATE:state_==State::Write?RISC_STORAGE_PACKAGE_WRITE_SMALL:state_==State::Replace?RISC_STORAGE_PACKAGE_REPLACE:RISC_STORAGE_PACKAGE_RECOVER;
     std::strcpy(request_.path,state_==State::Validate?record_->imagePath:(state_==State::Write||state_==State::Replace)?record_->stagePath:record_->manifestPath);
     if(state_==State::Validate){request_.size=record_->imageSize;std::memcpy(request_.sha256,record_->digest,32);}
     if(state_==State::Write){request_.size=uint32_t(std::strlen(record_->manifest));if(!request_.size||request_.size>sizeof(request_.data)){fail();return;}std::memcpy(request_.data,record_->manifest,request_.size);}
     if(state_==State::Replace)std::strcpy(request_.destination,record_->manifestPath);
     rc=package_->submit_package(base_->context,session_,&request_,&operation_);
    }
    if(rc==RISC_STORAGE_ASYNC_BUSY)return;
    if(rc!=RISC_STORAGE_ASYNC_OK){if(rc==RISC_STORAGE_ASYNC_RETAINED)state_=State::Retained;else fail();}
    return;
   }
   int rc=package_->base.collect(base_->context,session_,operation_,&result_);
   if(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY)return;
   operation_=0;
   if(rc==RISC_STORAGE_ASYNC_RETAINED){state_=State::Retained;return;}
   if(state_==State::Read){
    if(rc!=RISC_STORAGE_ASYNC_OK&&rc!=RISC_STORAGE_ASYNC_NOT_FOUND){fail();return;}
    if(rc==RISC_STORAGE_ASYNC_OK&&(result_.size>4096||!result_.size||std::memchr(result_.data,0,result_.size))){fail();return;}
    if(resume_){
     bool old=record_->previousPresent?rc==RISC_STORAGE_ASYNC_OK&&same(record_->previous):rc==RISC_STORAGE_ASYNC_NOT_FOUND;
     bool next=rc==RISC_STORAGE_ASYNC_OK&&same(record_->manifest);
     if(!old&&!next){fail();return;}
     alreadyPublished_=next;
    }else{
     record_->previousPresent=rc==RISC_STORAGE_ASYNC_OK;
     if(record_->previousPresent){std::memcpy(record_->previous,result_.data,result_.size);record_->previous[result_.size]=0;}
    }
    state_=State::Validate;return;
   }
   if(state_==State::Replace){state_=rc==RISC_STORAGE_ASYNC_OK?State::Verify:State::Reconcile;return;}
   if(rc!=RISC_STORAGE_ASYNC_OK){fail();return;}
   switch(state_){
    case State::Refresh:state_=State::Recover;break;
    case State::Recover:state_=State::Read;break;
    case State::Validate:state_=resume_?(alreadyPublished_?State::Ending:State::Clean):State::Journal;break;
    case State::Clean:state_=State::Write;break;
    case State::Write:state_=State::Replace;break;
    case State::Reconcile:state_=State::Verify;break;
    case State::Verify:if(!same(record_->manifest))failed_=true;state_=State::Ending;break;
    default:fail();break;
   }
  }
 private:
  bool same(const char*s)const{return std::strlen(s)==result_.size&&!std::memcmp(s,result_.data,result_.size);}
  void fail(){failed_=true;state_=State::Ending;}
  const risc_storage_volume_api_v1*base_=nullptr;const risc_storage_volume_api_v1_packages*package_=nullptr;Record*record_=nullptr;
  State state_=State::Idle;bool resume_=false,journaled_=false,alreadyPublished_=false,failed_=false,cancelled_=false;
  uint64_t session_=0,operation_=0;risc_storage_async_result_v1 result_{};risc_storage_package_request_v1 request_{};
};
}
