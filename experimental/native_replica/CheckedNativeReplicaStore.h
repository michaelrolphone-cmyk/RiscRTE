#pragma once
#include "NativeReplicaCore.h"
#include "FreshInitArtifactInspector.h"
#include "runtime/storage/AppDataFiles.h"
#include <climits>
namespace RiscExperimental {
// Worker-only bridge to existing checked AppDataFiles. The caller must configure
// files with a firmware-private root, NOT the public /appdata namespace root.
// Metadata callbacks access only a dedicated native NVS namespace per KV space.
class CheckedNativeReplicaStore {
 public:
 using C=NativeReplicaCore;
 struct Metadata {
  void*context;
  int(*read)(void*,uint32_t,const char*,void*,uint32_t);
  int(*write)(void*,uint32_t,const char*,const void*,uint32_t);
 };
 struct Guard {
  void*context;
  bool(*workerWithExclusiveLease)(void*);
  bool(*space)(void*,uint32_t*total,uint32_t*used);
 };
 static constexpr uint32_t PartitionBytes=0x80000,RecoveryReserve=8*4096;
 static constexpr uint32_t ObjectHeader=24,ImageMax=RISC_APP_DATA_FILE_MAX-ObjectHeader;
 CheckedNativeReplicaStore(uint32_t ns,RiscStorage::AppDataFiles&files,Metadata metadata,Guard guard,C::Allocator allocator=C::heap(),FreshInitArtifactInspector fresh={})
  :ns_(ns),files_(files),metadata_(metadata),guard_(guard),alloc_(allocator),fresh_(fresh){}
 ~CheckedNativeReplicaStore(){finishOperation();}
 CheckedNativeReplicaStore(const CheckedNativeReplicaStore&)=delete;
 CheckedNativeReplicaStore&operator=(const CheckedNativeReplicaStore&)=delete;
 C::Backend backend(){return {this,
  [](void*p,void*out,uint32_t n){return static_cast<CheckedNativeReplicaStore*>(p)->root(out,n);},
  [](void*p,uint32_t n,uint64_t*id){return static_cast<CheckedNativeReplicaStore*>(p)->reserve(n,id);},
  [](void*p,uint64_t id,uint32_t at,const void*bytes,uint32_t n){return static_cast<CheckedNativeReplicaStore*>(p)->write(id,at,bytes,n);},
  [](void*p,uint64_t id,uint32_t at,void*out,uint32_t n){return static_cast<CheckedNativeReplicaStore*>(p)->read(id,at,out,n);},
  [](void*p,uint64_t id){return static_cast<CheckedNativeReplicaStore*>(p)->seal(id);},
  [](void*p,const void*bytes,uint32_t n){return static_cast<CheckedNativeReplicaStore*>(p)->replaceRoot(bytes,n);},
  [](void*p,uint64_t id){static_cast<CheckedNativeReplicaStore*>(p)->retire(id);},
  [](void*p,C::FreshInitReceipt*out){return static_cast<CheckedNativeReplicaStore*>(p)->readInitialization(out);},
  [](void*p,const C::Initialization&a){return static_cast<CheckedNativeReplicaStore*>(p)->beginFreshInitialization(a);},
  [](void*p){static_cast<CheckedNativeReplicaStore*>(p)->endFreshInitialization();}};}
 static C::Limits limits(){return {ImageMax,4096,1024};}
 // Export custody pins the physical bank under its logical object identity.
 // No namespace or filename enters from the caller. One independent lease per
 // bank is enough for the checkpoint/export pair; a second request is BUSY.
 int pin(uint64_t object,uint64_t*out){
  if(out)*out=0;
  if(!out||!object)return C::Invalid;
  if(const int rc=admission())return rc;
  const unsigned bank=unsigned(object&1);
  if(pins_[bank])return C::Busy;
  Transition transition(transitioning_);int rc=loadBank(object,true);if(rc)return rc;
  if(pinSerial_==UINT64_MAX)return C::NoSpace;
  pins_[bank]=++pinSerial_;pinnedObjects_[bank]=object;*out=pins_[bank];return C::Ok;
 }
 int unpin(uint64_t token){
  if(!token)return C::Invalid;
  for(unsigned i=0;i<2;++i)if(pins_[i]==token){pins_[i]=pinnedObjects_[i]=0;return C::Ok;}
  return C::Stale;
 }
 uint64_t activeObject()const{return active_;}
 bool exitSafe()const{return !retained_&&(!fresh_||fresh_->exitSafe())&&files_.exitSafe()&&!pins_[0]&&!pins_[1];}
 void finishOperation(){if(transitioning_)return;endFreshInitialization();clearBuffers();}
 private:
 uint32_t ns_;RiscStorage::AppDataFiles&files_;Metadata metadata_;Guard guard_;C::Allocator alloc_;
 FreshInitArtifactInspector fresh_{};
 uint8_t freshReceipt_[C::RootBytes]{};bool freshArmed_=false,transitioning_=false;const C::Initialization*freshAdmission_=nullptr;
 struct Transition {bool&v;explicit Transition(bool&b):v(b){v=true;}~Transition(){v=false;}};
 void clearBuffers(){release(stage_,stageSize_);release(cache_,cacheSize_);candidate_=cached_=0;written_=0;sealed_=false;}
 uint64_t active_=0,candidate_=0,cached_=0,pinSerial_=0,pins_[2]{},pinnedObjects_[2]{};
 uint8_t*stage_=nullptr,*cache_=nullptr;uint32_t stageSize_=0,cacheSize_=0,written_=0;bool initialized_=false,sealed_=false,retained_=false;
 struct Scratch {uint8_t*p;size_t n;~Scratch(){C::wipe(p,n);}};
 static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 static uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
 static void putWord(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
 static void putWide(uint8_t*p,uint64_t n){putWord(p,uint32_t(n));putWord(p+4,uint32_t(n>>32));}
 static uint32_t crc(const void*v,uint32_t n){auto*p=static_cast<const uint8_t*>(v);uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
 static bool zeros(const uint8_t*p,uint32_t n){while(n--)if(*p++)return false;return true;}
 static const char*name(uint64_t id){return id&1?"replica-b.bin":"replica-a.bin";}
 // Retained close is a lifetime terminal state, ahead of worker/lease checks.
 // Never update the NVS sequence or touch files after custody becomes unsafe.
 int admission(){if(retained_||files_.retained()||(fresh_&&fresh_->retained())){retained_=true;return C::Retained;}if(transitioning_)return C::Busy;if(freshAdmission_){Transition transition(transitioning_);const int rc=freshCustody(nullptr);if(rc)return rc;}return ns_&&ns_<=INT32_MAX&&guard_.workerWithExclusiveLease&&guard_.workerWithExclusiveLease(guard_.context)&&metadata_.read&&metadata_.write&&alloc_.allocate&&alloc_.release?C::Ok:C::Busy;}
 void release(uint8_t*&p,uint32_t&n){if(p){C::wipe(p,n);alloc_.release(alloc_.context,p);}p=nullptr;n=0;}
 int mapped(int rc){
  if(rc==RISC_APP_DATA_OK)return C::Ok;
  if(rc==RISC_APP_DATA_NOT_FOUND)return C::Missing;
  if(rc==RISC_APP_DATA_NO_SPACE)return C::NoSpace;
  if(rc==RISC_APP_DATA_RETAINED){retained_=true;return C::Retained;}
  if(rc==RISC_APP_DATA_BUSY||rc==RISC_APP_DATA_STALE||rc==RISC_APP_DATA_UNAVAILABLE)return C::Busy;
  return C::Io;
 }
 bool validRoot(const uint8_t*p,bool initial=false)const{
  return !std::memcmp(p,initial?"RTENINIT":"RTENROOT",8)&&word(p+8)==ns_&&word(p+44)==crc(p,44)&&zeros(p+36,8)&&
   (initial?zeros(p+12,24):word(p+12)>=C::Header+4&&word(p+12)<=ImageMax&&wide(p+16)&&wide(p+24));
 }
 int root(void*out,uint32_t n){
  if(const int rc=admission())return rc;
  Transition transition(transitioning_);return readRootRecord(out,n);
 }
 int readRootRecord(void*out,uint32_t n){
  if(!out||n!=C::RootBytes)return C::Invalid;
  uint8_t bytes[C::RootBytes]{};Scratch wipe{bytes,sizeof bytes};int rc=metadata_.read(metadata_.context,ns_,"root",bytes,sizeof bytes);
  if(rc==C::Missing){
   // A truly absent root with existing banks is metadata loss, not permission
   // to initialize defaults. First creation writes RTENINIT before any bank.
   for(unsigned bank=0;bank<2;++bank){uint32_t size=0;uint64_t rev=0;int found=files_.stat(ns_,name(bank),&size,&rev);if(found==RISC_APP_DATA_OK)return C::Io;if(found!=RISC_APP_DATA_NOT_FOUND)return mapped(found);}
   active_=0;initialized_=false;return C::Missing;
  }
  if(rc)return rc;
  if(validFreshReceipt(bytes)){active_=0;initialized_=true;return C::Initializing;}
  if(validRoot(bytes,true)){active_=0;initialized_=true;return C::Missing;}
  if(!validRoot(bytes))return C::Io;
  active_=wide(bytes+24);initialized_=true;std::memcpy(out,bytes,n);return C::Ok;
 }
 bool validFreshReceipt(const uint8_t*p)const{return !std::memcmp(p,"RTENFI1",8)&&word(p+8)==ns_&&!zeros(p+12,32)&&word(p+44)==crc(p,44);}
 static constexpr uint32_t FreshImageBytes=484;
 void freshImage(uint8_t*p)const{C::makeFreshEmpty(p,ns_);}
 bool canonicalFresh(const uint8_t*p,uint32_t n)const{if(n!=FreshImageBytes)return false;uint8_t expected[FreshImageBytes]{};Scratch wipe{expected,sizeof expected};freshImage(expected);return !std::memcmp(p,expected,n);}
 int sequence(uint64_t*out,bool*missing){uint8_t p[24]{};Scratch wipe{p,sizeof p};int rc=metadata_.read(metadata_.context,ns_,"next",p,sizeof p);*out=0;*missing=rc==C::Missing;if(*missing)return C::Ok;if(rc)return rc;if(std::memcmp(p,"RTENSEQ",8)||word(p+16)||word(p+20)!=crc(p,20)||!wide(p+8)||wide(p+8)>UINT64_MAX/2)return C::Io;*out=wide(p+8);return C::Ok;}
 int inspectFreshArtifacts(){
  if(!fresh_||fresh_->namespaceId()!=ns_)return C::Invalid;
  uint64_t next=0;bool missing=false;int rc=sequence(&next,&missing);if(rc)return rc;
  uint8_t image[FreshImageBytes]{};Scratch wipe{image,sizeof image};freshImage(image);
  rc=missing?fresh_->inspectAbsent():fresh_->inspectAttempt(image,sizeof image,next);if(rc==C::Retained)retained_=true;return rc;
 }
 int freshCustody(const C::FreshInitReceipt*receipt){
  if(!freshAdmission_||!freshAdmission_->freshCustody_)return C::Invalid;
  const int rc=freshAdmission_->freshCustody_(freshAdmission_->factory_,*freshAdmission_,receipt);if(rc==C::Retained)retained_=true;return rc;
 }
 int checkFreshReceiptAndArtifacts(){
  int rc=freshCustody(nullptr);if(rc)return rc;
  C::FreshInitReceipt receipt;rc=metadata_.read(metadata_.context,ns_,"root",receipt.bytes_,sizeof receipt.bytes_);if(rc)return rc;
  if(!validFreshReceipt(receipt.bytes_)||std::memcmp(receipt.bytes_,freshReceipt_,sizeof freshReceipt_))return C::Stale;
  receipt.backendContext_=this;rc=inspectFreshArtifacts();if(rc)return rc;
  rc=freshCustody(&receipt);if(rc)return rc;
  uint8_t current[C::RootBytes]{};Scratch wipe{current,sizeof current};rc=metadata_.read(metadata_.context,ns_,"root",current,sizeof current);if(rc)return rc;
  if(std::memcmp(current,freshReceipt_,sizeof current))return C::Stale;
  return freshCustody(nullptr);
 }
 int readInitialization(C::FreshInitReceipt*out){
  if(const int rc=admission())return rc;
  if(!out||out->valid())return C::Invalid;
  Transition transition(transitioning_);
  uint8_t p[C::RootBytes]{};Scratch wipe{p,sizeof p};int rc=metadata_.read(metadata_.context,ns_,"root",p,sizeof p);if(rc)return rc;
  if(!validFreshReceipt(p))return validRoot(p)||validRoot(p,true)?int(C::Missing):int(C::Io);
  std::memcpy(out->bytes_,p,sizeof p);out->backendContext_=this;return C::Ok;
 }
 int beginFreshInitialization(const C::Initialization&a){
  if(const int rc=admission())return rc;
  if(freshArmed_)return C::Busy;
  if(!fresh_||fresh_->namespaceId()!=ns_||!a.freshCustody_||!a.prepared_||!a.consumed_||!a.activating_||!a.core_||!a.core_->admissionBusy_||a.core_->b_.context!=this||a.backendContext_!=this||a.namespace_!=ns_||a.baseline_.origin()!=C::Origin::FreshEmpty||a.mapBytes_||a.count_||zeros(a.sourceFingerprint_,32))return C::Invalid;
  Transition transition(transitioning_);freshAdmission_=&a;int rc=freshCustody(nullptr);if(rc)return rc;
  uint8_t p[C::RootBytes]{};Scratch wipe{p,sizeof p};rc=metadata_.read(metadata_.context,ns_,"root",p,sizeof p);
  if(a.mode_==C::Initialization::Mode::CreateFresh){
   if(rc!=C::Missing)return rc?rc:int(C::Conflict);
   uint64_t next=0;bool missing=false;rc=sequence(&next,&missing);if(rc)return rc;if(!missing)return C::Conflict;
   rc=fresh_->inspectAbsent();if(rc){if(rc==C::Retained)retained_=true;return rc;}
   std::memcpy(p,"RTENFI1",8);putWord(p+8,ns_);std::memcpy(p+12,a.sourceFingerprint_,32);putWord(p+44,crc(p,44));
   rc=metadata_.write(metadata_.context,ns_,"root",p,sizeof p);if(rc)return rc==C::Retained?(retained_=true,C::Retained):C::Uncertain;
  }else if(a.mode_==C::Initialization::Mode::ResumeFresh){
   if(rc)return rc;
   if(!a.receipt_.valid()||a.receipt_.backendContext_!=this||!validFreshReceipt(p)||std::memcmp(p,a.receipt_.bytes_,sizeof p)||std::memcmp(p+12,a.sourceFingerprint_,32))return C::Stale;
  }else return C::Invalid;
  std::memcpy(freshReceipt_,p,sizeof p);rc=checkFreshReceiptAndArtifacts();if(rc){C::wipe(freshReceipt_,sizeof freshReceipt_);return rc;}
  freshArmed_=true;active_=0;initialized_=true;return C::Ok;
 }
 void endFreshInitialization(){freshAdmission_=nullptr;freshArmed_=false;C::wipe(freshReceipt_,sizeof freshReceipt_);}
 int reserve(uint32_t n,uint64_t*out){
  if(out)*out=0;
  if(const int rc=admission())return rc;
  Transition transition(transitioning_);
  uint8_t current[C::RootBytes]{};Scratch wipeCurrent{current,sizeof current};const int selected=readRootRecord(current,sizeof current);
  if(selected!=C::Ok&&selected!=C::Missing&&selected!=C::Initializing)return selected;
  if(selected==C::Initializing){if(!freshArmed_)return C::Initializing;const int check=checkFreshReceiptAndArtifacts();if(check)return check;}
  else if(freshArmed_)return C::Stale;
  if(!out||n<C::Header+4||(freshArmed_&&n!=FreshImageBytes))return C::Invalid;
  if(n>ImageMax)return C::NoSpace;
  const unsigned bank=active_?unsigned((active_&1)^1):0;
  if(pins_[bank])return C::Busy;
  uint32_t total=0,used=0;if(!guard_.space||!guard_.space(guard_.context,&total,&used)||total!=PartitionBytes||used>total)return C::Io;
  const uint64_t required=((uint64_t(n)+ObjectHeader+4095)/4096)*4096+RecoveryReserve;
  if(required>total-used)return C::NoSpace;
  clearBuffers();stageSize_=n+ObjectHeader;stage_=static_cast<uint8_t*>(alloc_.allocate(alloc_.context,stageSize_));if(!stage_){stageSize_=0;return C::NoMemory;}
  std::memset(stage_,0,stageSize_);
  if(!initialized_){uint8_t start[C::RootBytes]{};Scratch wipe{start,sizeof start};std::memcpy(start,"RTENINIT",8);putWord(start+8,ns_);putWord(start+44,crc(start,44));int rc=metadata_.write(metadata_.context,ns_,"root",start,sizeof start);if(rc){clearBuffers();return rc;}initialized_=true;}
  uint8_t counter[24]{};Scratch wipe{counter,sizeof counter};int rc=metadata_.read(metadata_.context,ns_,"next",counter,sizeof counter);uint64_t next=0;
  if(rc==C::Missing){if(active_){clearBuffers();return C::Io;}}
  else if(rc||std::memcmp(counter,"RTENSEQ",8)||word(counter+20)!=crc(counter,20)||word(counter+16)){clearBuffers();return rc?rc:C::Io;}
  else next=wide(counter+8);
  if(next>=UINT64_MAX/2||(active_&&next<(active_>>1))){clearBuffers();return C::NoSpace;}
  ++next;std::memset(counter,0,sizeof counter);std::memcpy(counter,"RTENSEQ",7);putWide(counter+8,next);putWord(counter+20,crc(counter,20));
  rc=metadata_.write(metadata_.context,ns_,"next",counter,sizeof counter);if(rc){clearBuffers();return rc;}
  candidate_=(next<<1)|bank;*out=candidate_;return C::Ok;
 }
 int write(uint64_t id,uint32_t at,const void*p,uint32_t n){
  if(const int rc=admission())return rc;
  if(!stage_||id!=candidate_||sealed_||!p||!n||at!=written_||at>stageSize_-ObjectHeader||n>stageSize_-ObjectHeader-at)return C::Invalid;
  std::memcpy(stage_+ObjectHeader+at,p,n);written_+=n;return C::Ok;
 }
 int seal(uint64_t id){
  if(const int rc=admission())return rc;
  Transition transition(transitioning_);
  if(!stage_||id!=candidate_||sealed_||written_!=stageSize_-ObjectHeader)return C::Invalid;
  if(freshArmed_){int check=checkFreshReceiptAndArtifacts();if(check)return check;if(!canonicalFresh(stage_+ObjectHeader,written_))return C::Invalid;}
  else {uint8_t current[C::RootBytes]{};Scratch wipe{current,sizeof current};int rc=metadata_.read(metadata_.context,ns_,"root",current,sizeof current);if(rc)return rc;if(validFreshReceipt(current))return C::Initializing;if(!validRoot(current)&&!validRoot(current,true))return C::Io;}
  std::memcpy(stage_,"RTENOBJ",7);putWide(stage_+8,id);putWord(stage_+16,written_);putWord(stage_+20,crc(stage_+ObjectHeader,written_));
  uint32_t oldSize=0;uint64_t rev=0;int rc=files_.stat(ns_,name(id),&oldSize,&rev);if(rc&&rc!=RISC_APP_DATA_NOT_FOUND)return mapped(rc);
  rc=files_.replace(ns_,name(id),rev,stage_,stageSize_);if(rc)return mapped(rc);
  rc=loadBank(id,true);if(rc)return rc;
  if(cacheSize_!=stageSize_||std::memcmp(cache_,stage_,stageSize_))return C::Io;
  sealed_=true;return C::Ok;
 }
 int loadBank(uint64_t id,bool internal=false){
  if(!internal){if(const int rc=admission())return rc;}
  if(!id)return C::Invalid;
  if(cached_==id&&cache_)return C::Ok;
  release(cache_,cacheSize_);cached_=0;
  uint32_t size=0;uint64_t rev=0;int rc=files_.stat(ns_,name(id),&size,&rev);if(rc)return mapped(rc);
  if(size<ObjectHeader+C::Header+4||size>RISC_APP_DATA_FILE_MAX)return C::Io;
  cache_=static_cast<uint8_t*>(alloc_.allocate(alloc_.context,size));if(!cache_)return C::NoMemory;cacheSize_=size;
  uint32_t got=0;uint64_t actual=0;rc=files_.read(ns_,name(id),rev,cache_,size,&got,&actual);
  if(rc||got!=size||actual!=rev||std::memcmp(cache_,"RTENOBJ",8)||wide(cache_+8)!=id||word(cache_+16)!=size-ObjectHeader||word(cache_+20)!=crc(cache_+ObjectHeader,size-ObjectHeader)){release(cache_,cacheSize_);return rc?mapped(rc):C::Io;}
  cached_=id;return C::Ok;
 }
 int read(uint64_t id,uint32_t at,void*out,uint32_t n){
  if(const int rc=admission())return rc;
  Transition transition(transitioning_);
  if(!out||!n)return C::Invalid;
  int rc=loadBank(id,true);if(rc)return rc;
  const uint32_t bytes=cacheSize_-ObjectHeader;if(at>bytes||n>bytes-at)return C::Invalid;
  std::memcpy(out,cache_+ObjectHeader+at,n);return C::Ok;
 }
 int replaceRoot(const void*p,uint32_t n){
  if(const int rc=admission())return rc;
  Transition transition(transitioning_);
  if(!p||n!=C::RootBytes||!stage_||!sealed_)return C::Invalid;
  auto*bytes=static_cast<const uint8_t*>(p);
  if(freshArmed_){int check=checkFreshReceiptAndArtifacts();if(check)return check;if(!canonicalFresh(stage_+ObjectHeader,written_))return C::Invalid;}
  else {uint8_t current[C::RootBytes]{};Scratch wipe{current,sizeof current};int rc=metadata_.read(metadata_.context,ns_,"root",current,sizeof current);if(rc)return rc;if(validFreshReceipt(current))return C::Initializing;if(!validRoot(current)&&!validRoot(current,true))return C::Io;}

  if(!validRoot(bytes)||wide(bytes+24)!=candidate_||word(bytes+12)!=written_||word(bytes+32)!=word(stage_+stageSize_-4))return C::Invalid;
  int rc=metadata_.write(metadata_.context,ns_,"root",p,n);if(!rc)active_=candidate_;return rc;
 }
 void retire(uint64_t id){
  // Two permanent physical names are recycled only by reserve's inactive-bank
  // and export-lease checks. Logical IDs are never recycled, even after errors.
  if(id==candidate_)finishOperation();
 }
};
}
