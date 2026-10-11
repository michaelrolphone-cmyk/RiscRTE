#pragma once
#include "InstalledSettingsCheckpoint.h"
#include <cstdlib>
#include <new>
namespace RiscPackages {
// Immutable <=4096-byte pages, then one checked atomic index replacement.
// Native journal acknowledgement is last, after copied worker close succeeds.
class PagedSettingsCheckpoint {
 public:
 using State=InstalledSettingsCheckpoint::State;
 ~PagedSettingsCheckpoint(){std::free(bytes_);}
 bool start(const risc_installation_v1*n,const risc_storage_volume_api_v1*v,uint32_t slot,const char*owner){
  if(phase_!=Idle||!n||!n->settings_status||!n->settings_restore||!n->settings_snapshot||!n->settings_ack||!slot||!owner||!*owner||strlen(owner)>=96||strchr(owner,'/')||strstr(owner,"..")||!risc_storage_volume_download(v))return false;
  native_=n;volume_=v;package_=risc_storage_volume_packages(v);slot_=slot;
  strcpy(owner_,owner);strcpy(root_,"/AppData");
  if(slot>n->namespace_capacity){
   if(n->struct_size<RISC_INSTALLATION_SHARED_SETTINGS_SIZE||!n->shared_settings_namespace||
      slot-n->namespace_capacity-1>=n->shared_settings_count)return false;
   const uint32_t ns=n->shared_settings_namespace(n->context,slot-n->namespace_capacity-1);
   char expected[32];snprintf(expected,sizeof expected,"shared-%08x",unsigned(ns));
   if(!ns||strcmp(owner,expected))return false;
   strcpy(root_,"/RTECFG");snprintf(directory_,sizeof directory_,"%s/%08X",root_,unsigned(ns));
  }else snprintf(directory_,sizeof directory_,"%s/%s",root_,owner);
  snprintf(index_,sizeof index_,"%s/settings.index",directory_);snprintf(temporary_,sizeof temporary_,"%s/.settings.index.next",directory_);
  if(package_->base.begin(v->context,&session_)!=RISC_STORAGE_ASYNC_OK){return false;}phase_=Refresh;return true;
 }
 State state()const{return phase_==Done?State::Done:phase_==Failed?State::Failed:phase_==Cancelled?State::Cancelled:phase_==Retained?State::Retained:State::Refresh;}
 void cancel(){if(terminal())return;cancelled_=true;phase_=Ending;}
 void step(){
  if(terminal()||phase_==Idle)return;
  if(phase_==Ending||phase_==EndRead||phase_==EndWrite||phase_==EndCleanup){
   if(session_){int rc=package_->base.end(volume_->context,session_);if(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc!=RISC_STORAGE_ASYNC_OK){phase_=Retained;return;}session_=operation_=0;}
   phase_=phase_==EndRead?Restore:phase_==EndWrite?Ack:phase_==EndCleanup?Done:cancelled_?Cancelled:Failed;return;
  }
  if(phase_==Restore){int rc=native_->settings_restore(native_->context,slot_,owner_,missing_?nullptr:bytes_,missing_?0:size_,missing_);if(rc==RISC_KEY_VALUE_BUSY)return;if(rc){phase_=Failed;return;}
   std::free(bytes_);bytes_=nullptr;size_=0;phase_=native_->settings_status(native_->context,slot_)==1?Done:Snapshot;return;}
  if(phase_==Snapshot){
   uint32_t needed=0;int rc=native_->settings_snapshot(native_->context,slot_,nullptr,0,&needed,&generation_);
   if(rc==RISC_KEY_VALUE_BUSY){return;}if(rc!=RISC_KEY_VALUE_BUFFER_SMALL||needed<132){phase_=Failed;return;}
   bytes_=static_cast<uint8_t*>(std::malloc(needed));if(!bytes_){phase_=Failed;return;}
   size_=needed;rc=native_->settings_snapshot(native_->context,slot_,bytes_,size_,&needed,&generation_);
   if(rc||needed!=size_){phase_=Failed;return;}
   std::memset(indexBytes_,0,sizeof indexBytes_);std::memcpy(indexBytes_,"RTEKVP2",7);word(indexBytes_+8,size_);wide(indexBytes_+12,generation_);word(indexBytes_+20,crc(bytes_,size_));word(indexBytes_+24,crc(indexBytes_,24));
   page_=0;phase_=BeginWrite;return;
  }
  if(phase_==BeginWrite){int rc=package_->base.begin(volume_->context,&session_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc!=RISC_STORAGE_ASYNC_OK){phase_=Failed;return;}phase_=RefreshWrite;return;}
  if(phase_==Ack){int rc=native_->settings_ack(native_->context,slot_,generation_);if(rc==RISC_KEY_VALUE_BUSY)return;phase_=rc?Failed:priorPresent_&&priorGeneration_!=generation_?BeginCleanup:Done;return;}
  if(phase_==BeginCleanup){int rc=package_->base.begin(volume_->context,&session_);if(rc==RISC_STORAGE_ASYNC_BUSY)return;if(rc!=RISC_STORAGE_ASYNC_OK){phase_=Done;return;}phase_=CheckCleanup;page_=0;return;}
  if(!operation_){
   int rc;request_={};
   if(phase_==Refresh||phase_==RefreshWrite||phase_==ReadIndex||phase_==ReadLegacy||phase_==ReadPage||phase_==ExistingPage||phase_==VerifyPage||phase_==VerifyIndex||phase_==ReadPrevious||phase_==RemovePage||phase_==CheckCleanup||phase_==RemovePrevious||phase_==RemoveTemp){
    risc_storage_async_request_v1 q{};
    q.operation=phase_==Refresh||phase_==RefreshWrite?RISC_STORAGE_ASYNC_REFRESH:(phase_==RemoveTemp||phase_==RemovePage||phase_==RemovePrevious)?RISC_STORAGE_ASYNC_REMOVE_FILE:RISC_STORAGE_ASYNC_READ_SMALL_FILE;
    const char*path=index_;if(phase_==ReadLegacy){snprintf(pagePath_,sizeof pagePath_,"%s/settings.kv",directory_);path=pagePath_;}
    if(phase_==ReadPage||phase_==ExistingPage||phase_==VerifyPage||phase_==RemovePage){pagePath();path=pagePath_;}if(phase_==RemoveTemp)path=temporary_;
    if(phase_==RemovePrevious){snprintf(pagePath_,sizeof pagePath_,"%s/kv-%08x%08x-%08x",directory_,unsigned(priorGeneration_>>32),unsigned(priorGeneration_),unsigned(page_));path=pagePath_;}
    strcpy(q.path,path);rc=package_->base.submit(volume_->context,session_,&q,&operation_);
   }else{
    request_.operation=phase_==RecoverIndex||phase_==RecoverLegacy||phase_==RecoverWrite?RISC_STORAGE_PACKAGE_RECOVER:phase_==Root||phase_==Directory?RISC_STORAGE_PACKAGE_MKDIR:phase_==ReplaceIndex?RISC_STORAGE_PACKAGE_REPLACE:RISC_STORAGE_PACKAGE_WRITE_SMALL;
    const char*path=index_;
    if(phase_==RecoverLegacy){snprintf(pagePath_,sizeof pagePath_,"%s/settings.kv",directory_);path=pagePath_;}
    if(phase_==Root){path=root_;}if(phase_==Directory){path=directory_;}
    if(phase_==WritePage){pagePath();path=pagePath_;request_.size=pageSize();memcpy(request_.data,bytes_+page_*4096,request_.size);}
    if(phase_==WriteIndex||phase_==ReplaceIndex){path=temporary_;strcpy(request_.destination,index_);request_.size=sizeof indexBytes_;memcpy(request_.data,indexBytes_,sizeof indexBytes_);}
    strcpy(request_.path,path);rc=package_->submit_package(volume_->context,session_,&request_,&operation_);
   }
   if(rc==RISC_STORAGE_ASYNC_BUSY){return;}if(rc!=RISC_STORAGE_ASYNC_OK){fail(rc);}return;
  }
  int rc=package_->base.collect(volume_->context,session_,operation_,&result_);if(rc==RISC_STORAGE_ASYNC_PENDING||rc==RISC_STORAGE_ASYNC_BUSY)return;operation_=0;
  if(rc==RISC_STORAGE_ASYNC_NOT_FOUND){
   if(phase_==ReadIndex){phase_=RecoverLegacy;return;}
   if(phase_==ReadLegacy){missing_=true;phase_=EndRead;return;}
   if(phase_==ExistingPage){phase_=WritePage;return;}
   if(phase_==RemoveTemp){phase_=WriteIndex;return;}
   if(phase_==ReadPrevious){priorPresent_=false;phase_=Root;return;}
   if(phase_==RemovePage){phase_=WritePage;return;}
   if(phase_==RemovePrevious){rc=RISC_STORAGE_ASYNC_OK;}
   if(phase_==CheckCleanup){phase_=EndCleanup;return;}
  }
  if(rc!=RISC_STORAGE_ASYNC_OK){fail(rc);return;}
  switch(phase_){
   case Refresh:phase_=RecoverIndex;break;
   case RecoverIndex:phase_=ReadIndex;break;
   case RecoverLegacy:phase_=ReadLegacy;break;
   case ReadLegacy:
    size_=result_.size;if(!size_){fail(-1);break;}bytes_=static_cast<uint8_t*>(std::malloc(size_));if(!bytes_){fail(-1);break;}memcpy(bytes_,result_.data,size_);phase_=EndRead;break;
   case ReadIndex:
    if(result_.size!=sizeof indexBytes_||memcmp(result_.data,"RTEKVP2",8)||word(result_.data+24)!=crc(result_.data,24)){fail(-1);break;}
    memcpy(indexBytes_,result_.data,sizeof indexBytes_);size_=word(indexBytes_+8);generation_=wide(indexBytes_+12);
    if(size_<132){fail(-1);break;}bytes_=static_cast<uint8_t*>(std::malloc(size_));if(!bytes_){fail(-1);break;}page_=0;phase_=ReadPage;break;
   case ReadPage:
    if(result_.size!=pageSize()){fail(-1);break;}memcpy(bytes_+page_*4096,result_.data,result_.size);++page_;
    if(page_*uint64_t(4096)>=size_){if(crc(bytes_,size_)!=word(indexBytes_+20)){fail(-1);break;}phase_=EndRead;}break;
   case RefreshWrite:phase_=RecoverWrite;break;
   case RecoverWrite:phase_=ReadPrevious;break;
   case ReadPrevious:
    if(result_.size!=sizeof indexBytes_||memcmp(result_.data,"RTEKVP2",8)||word(result_.data+24)!=crc(result_.data,24)||wide(result_.data+12)>generation_){fail(-1);break;}
    priorPresent_=true;priorGeneration_=wide(result_.data+12);priorSize_=word(result_.data+8);phase_=Root;break;
   case CheckCleanup:
    phase_=(result_.size==sizeof indexBytes_&&!memcmp(result_.data,indexBytes_,sizeof indexBytes_))?RemovePrevious:EndCleanup;break;
   case RemovePrevious:++page_;if(page_*uint64_t(4096)>=priorSize_)phase_=EndCleanup;break;
   case RemovePage:phase_=WritePage;break;
   case Root:phase_=Directory;break;
   case Directory:phase_=ExistingPage;break;
   case ExistingPage:case VerifyPage:
    if(result_.size!=pageSize()||memcmp(result_.data,bytes_+page_*4096,result_.size)){
     if(phase_==ExistingPage&&(!priorPresent_||priorGeneration_!=generation_))phase_=RemovePage;else fail(-1);break;
    }
    ++page_;phase_=page_*uint64_t(4096)>=size_?RemoveTemp:ExistingPage;break;
   case WritePage:phase_=VerifyPage;break;
   case RemoveTemp:phase_=WriteIndex;break;
   case WriteIndex:phase_=ReplaceIndex;break;
   case ReplaceIndex:phase_=VerifyIndex;break;
   case VerifyIndex:
    if(result_.size!=sizeof indexBytes_||memcmp(result_.data,indexBytes_,sizeof indexBytes_)){fail(-1);break;}phase_=EndWrite;break;
   default:fail(-1);break;
  }
 }
 private:
 enum Phase{Idle,Refresh,RecoverIndex,ReadIndex,RecoverLegacy,ReadLegacy,ReadPage,EndRead,Restore,Snapshot,BeginWrite,RefreshWrite,RecoverWrite,Root,Directory,ExistingPage,WritePage,VerifyPage,RemoveTemp,WriteIndex,ReplaceIndex,VerifyIndex,EndWrite,Ack,ReadPrevious,RemovePage,BeginCleanup,CheckCleanup,RemovePrevious,EndCleanup,Ending,Done,Failed,Cancelled,Retained};
 bool terminal()const{return phase_==Done||phase_==Failed||phase_==Cancelled||phase_==Retained;}
 void fail(int rc){phase_=rc==RISC_STORAGE_ASYNC_RETAINED?Retained:Ending;}
 static uint32_t word(const uint8_t*p){uint32_t n=0;for(unsigned i=0;i<4;++i)n|=uint32_t(p[i])<<(8*i);return n;}
 static void word(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
 static uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
 static void wide(uint8_t*p,uint64_t n){word(p,uint32_t(n));word(p+4,uint32_t(n>>32));}
 static uint32_t crc(const void*data,uint32_t n){auto*p=static_cast<const uint8_t*>(data);uint32_t v=~0u;while(n--){v^=*p++;for(unsigned i=0;i<8;++i)v=(v>>1)^((v&1)?0xedb88320u:0);}return ~v;}
 uint32_t pageSize()const{uint64_t at=uint64_t(page_)*4096;return uint32_t(size_-at>4096?4096:size_-at);}
 void pagePath(){snprintf(pagePath_,sizeof pagePath_,"%s/kv-%08x%08x-%08x",directory_,unsigned(generation_>>32),unsigned(generation_),unsigned(page_));}
 Phase phase_=Idle;bool missing_=false,cancelled_=false,priorPresent_=false;uint64_t priorGeneration_=0;uint32_t priorSize_=0;const risc_installation_v1*native_=nullptr;const risc_storage_volume_api_v1*volume_=nullptr;const risc_storage_volume_api_v1_packages*package_=nullptr;
 uint64_t session_=0,operation_=0,generation_=0;uint32_t slot_=0,size_=0,page_=0;uint8_t*bytes_=nullptr;uint8_t indexBytes_[28]{};
 char root_[12]{},owner_[96]{},directory_[128]{},index_[160]{},temporary_[160]{},pagePath_[192]{};
 risc_storage_package_request_v1 request_{};risc_storage_async_result_v1 result_{};
};
class SettingsCheckpoint {
 public:
 using State=InstalledSettingsCheckpoint::State;
 ~SettingsCheckpoint(){if(constructed_){if(paged_)paged()->~PagedSettingsCheckpoint();else legacy()->~InstalledSettingsCheckpoint();}}
 bool start(const risc_installation_v1*n,const risc_storage_volume_api_v1*v,uint32_t slot,const char*id){
  paged_=n&&n->struct_size>=RISC_INSTALLATION_SETTINGS_FORMAT_SIZE&&n->settings_format==2;constructed_=true;
  if(paged_)return (new(memory_)PagedSettingsCheckpoint())->start(n,v,slot,id);
  return (new(memory_)InstalledSettingsCheckpoint())->start(n,v,slot,id);
 }
 State state()const{return paged_?paged()->state():legacy()->state();}
 void cancel(){if(paged_)paged()->cancel();else legacy()->cancel();}
 void step(){if(paged_)paged()->step();else legacy()->step();}
 private:
 PagedSettingsCheckpoint*paged(){return reinterpret_cast<PagedSettingsCheckpoint*>(memory_);}const PagedSettingsCheckpoint*paged()const{return reinterpret_cast<const PagedSettingsCheckpoint*>(memory_);}
 InstalledSettingsCheckpoint*legacy(){return reinterpret_cast<InstalledSettingsCheckpoint*>(memory_);}const InstalledSettingsCheckpoint*legacy()const{return reinterpret_cast<const InstalledSettingsCheckpoint*>(memory_);}
 bool paged_=false,constructed_=false;
 alignas(PagedSettingsCheckpoint) unsigned char memory_[sizeof(PagedSettingsCheckpoint)>sizeof(InstalledSettingsCheckpoint)?sizeof(PagedSettingsCheckpoint):sizeof(InstalledSettingsCheckpoint)];
};
}
