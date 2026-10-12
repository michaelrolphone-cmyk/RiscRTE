#pragma once
#include "CheckedSdSettingsCatalog.h"
#include "CheckedNvsFreshSources.h"
#include "CheckedNativeReplicaStore.h"
#include "NativeSdCompletion.h"
namespace RiscExperimental {
// One-shot native controller. Only an actual checked native catalog/reader/session may
// hand it a candidate. Owner issues the pinned receipt; the admitted settings
// worker alone inventories and writes native storage. No SD observer or provider
// callback runs on that worker. All objects live until checked collection.
class CheckedSdNativeRestore final {
 public:
 using C=NativeReplicaCore;using A=NativeBaselineAdmission;using Digest=A::Digest;
 struct Guard {void*context;bool(*owner)(void*);bool(*worker)(void*);bool(*workerWithLease)(void*);uint64_t(*leaseEpoch)(void*);};
 CheckedSdNativeRestore(C&core,CheckedNativeReplicaStore&store,CheckedNvsFreshSources&sources,Digest digest,Guard guard)
  :core_(core),store_(store),sources_(sources),hash_(digest),guard_(guard),completion_(this,worker,execute){}
 CheckedSdNativeRestore(const CheckedSdNativeRestore&)=delete;CheckedSdNativeRestore&operator=(const CheckedSdNativeRestore&)=delete;
 ~CheckedSdNativeRestore(){clearImage();}
 bool pending()const{return completion_.pending();}
 bool retained()const{return retained_||completion_.state()==NativeSdCompletion::State::Retained;}
 bool exitSafe()const{return !retained()&&completion_.exitSafe();}
 // Native-only inputs: no raw map, identity, receipt bytes or source-choice flag.
 int begin(NativeSdAttempt&attempt,CheckedSdReplicaReader::Candidate&candidate){
  if(!guard_.owner||!guard_.owner(guard_.context)||!attempt.owner())return C::Invalid;
  if(retained())return C::Retained;
  if(completion_.state()!=NativeSdCompletion::State::Idle||admission_.prepared_)return C::Busy;
  if(!configured()||!candidate.valid_||!candidate.restoreQualified_||candidate.sourceCore_!=&core_||candidate.sourceAttempt_!=&attempt||candidate.namespace_!=core_.namespaceId()||candidate.sourceEpoch_!=attempt.epoch()||candidate.sourceClosed_!=attempt.closedSequence())return C::Conflict;
  const uint64_t epoch=guard_.leaseEpoch(guard_.context);if(!epoch)return C::Busy;
  int rc=completion_.bind(attempt);if(rc)return rc;
  auto&a=admission_;a.alloc_=candidate.alloc_;a.map_=candidate.bytes_;a.capacity_=candidate.capacity_;a.mapBytes_=candidate.size_;a.count_=candidate.count_;a.sdIdentity_=candidate.identity_;
  candidate.bytes_=nullptr;candidate.reset();
  a.core_=&core_;a.factory_=this;a.owner_=guard_.context;a.backendContext_=&store_;a.namespace_=core_.namespaceId();a.leaseEpoch_=epoch;a.serial_=1;
  a.revalidate_=[](void*p,C::Initialization&a,C&core){return static_cast<CheckedSdNativeRestore*>(p)->revalidate(a,core);};
  a.sdCustody_=[](void*p,const C::Initialization&a,const C::SdInitReceipt*r){return static_cast<CheckedSdNativeRestore*>(p)->custody(a,r);};
  a.complete_=[](void*p,int result){if(result==C::Retained)static_cast<CheckedSdNativeRestore*>(p)->retained_=true;};
  rc=completion_.issue({1,a.count_,a.mapBytes_,0,0,a.sdIdentity_});
  if(rc)a.reset();
  return checked(rc);
 }
 void cancel(){completion_.cancel();}
 bool workerStep(){return completion_.workerStep();}
 // A committed native receipt survives cancellation during publication. Late SD
 // custody loss returns Retained and keeps the attempt pinned even after commit.
 int collect(NativeSdCompletion::Result*out){
  if(!guard_.owner||!guard_.owner(guard_.context))return C::Invalid;
  const int rc=completion_.collect(out);if(rc==C::Ok){admission_.reset();clearImage();}return rc;
 }
 private:
 C&core_;CheckedNativeReplicaStore&store_;CheckedNvsFreshSources&sources_;Digest hash_;Guard guard_;
 C::Initialization admission_;NativeSdCompletion completion_;uint8_t*image_=nullptr;uint32_t imageBytes_=0;std::atomic<bool>retained_{false};
 bool configured(){return guard_.context&&guard_.owner&&guard_.worker&&guard_.workerWithLease&&guard_.leaseEpoch&&hash_.begin&&hash_.update&&hash_.finish&&core_.validConfiguration()&&core_.sdHooks()&&core_.usesBackend(store_.backend())&&store_.fresh_==&sources_.artifacts_&&sources_.ns_==core_.namespaceId();}
 static bool worker(void*p){auto&s=*static_cast<CheckedSdNativeRestore*>(p);return s.guard_.worker&&s.guard_.worker(s.guard_.context);}
 void clearImage(){if(image_){C::wipe(image_,imageBytes_);core_.alloc_.release(core_.alloc_.context,image_);}image_=nullptr;imageBytes_=0;}
 int checked(int result){if(result==C::Retained)retained_=true;return result;}
 int observe(){
  if(retained_)return C::Retained;
  int rc=sources_.observation();if(rc)return checked(rc);
  const bool held=worker(this)&&guard_.workerWithLease(guard_.context);const uint64_t epoch=guard_.leaseEpoch(guard_.context);
  rc=sources_.observation();if(rc)return checked(rc);
  return !held?int(C::Busy):!epoch||epoch!=admission_.leaseEpoch_?int(C::Stale):int(C::Ok);
 }
 int digest(const void*prefix,uint32_t prefixBytes,const uint8_t*bytes,uint32_t size,uint8_t out[32]){
  auto after=[&](bool ok){const int rc=observe();return rc?rc:ok?int(C::Ok):int(C::Io);};
  int rc=observe();if(rc)return rc;rc=after(hash_.begin(hash_.context));if(rc)return rc;
  rc=after(hash_.update(hash_.context,prefix,prefixBytes));if(rc)return rc;
  for(uint32_t at=0;at<size;){const uint32_t n=C::chunk(size-at);rc=after(hash_.update(hash_.context,bytes+at,n));if(rc)return rc;at+=n;}
  return after(hash_.finish(hash_.context,out));
 }
 int verifyMap(){
  auto&a=admission_;uint8_t prefix[12]{},out[32]{};C::putWord(prefix,a.namespace_);C::putWord(prefix+4,a.count_);C::putWord(prefix+8,a.mapBytes_);
  const int rc=digest(prefix,sizeof prefix,a.map_,a.mapBytes_,out);const bool equal=!rc&&!std::memcmp(out,a.sdIdentity_.digest,32);C::wipe(out,sizeof out);return rc?rc:equal?int(C::Ok):int(C::Conflict);
 }
 int fingerprint(uint8_t out[32]){
  const auto original=sources_.sdOriginalWitness();uint8_t prefix[48]{};std::memcpy(prefix,"RTENSDRESTORE1",14);std::memcpy(prefix+16,original.fingerprint,32);
  const int rc=digest(prefix,sizeof prefix,image_,imageBytes_,out);C::wipe(prefix,sizeof prefix);return rc;
 }
 int inventory(const C::SdInitReceipt*receipt,bool allowReceipt){
  A::Witness current{};CheckedNvsFreshSources::SdCheck facts{receipt,admission_.sourceFingerprint_,image_,imageBytes_};
  const int rc=sources_.sdInventory(facts,allowReceipt,&current);if(rc)return checked(rc==C::Io?int(C::Conflict):rc);
  const auto original=sources_.sdOriginalWitness();return CheckedNvsFreshSources::same(current,original)?int(C::Ok):int(C::Conflict);
 }
 int selected(C::SdInitReceipt*out){
  const int rc=core_.b_.readSdInitialization(core_.b_.context,out);if(rc)return checked(rc==C::Io?int(C::Conflict):rc);
  return out->valid()&&out->backendContext_==&store_&&out->namespaceId()==core_.namespaceId()?int(C::Ok):int(C::Conflict);
 }
 int custody(const C::Initialization&a,const C::SdInitReceipt*receipt){
  if(&a!=&admission_||!a.prepared_||!a.consumed_||!core_.admissionBusy_||a.backendContext_!=&store_)return C::Stale;
  int rc=observe();if(rc||!receipt)return rc;
  if(!receipt->valid()||receipt->backendContext_!=&store_||receipt->namespaceId()!=a.namespace_||std::memcmp(receipt->fingerprint(),a.sourceFingerprint_,32)||(a.mode_==C::Initialization::Mode::ResumeSd&&std::memcmp(receipt->bytes_,a.sdReceipt_.bytes_,48)))return C::Conflict;
  rc=inventory(receipt,true);if(rc)return rc;return observe();
 }
 int revalidate(C::Initialization&a,C&core){
  if(&a!=&admission_||&core!=&core_||core_.bytes_)return C::Conflict;
  int rc=observe();if(rc)return rc;rc=verifyMap();if(rc)return rc;
  uint8_t expected[32]{};rc=fingerprint(expected);const bool equal=!rc&&!std::memcmp(expected,a.sourceFingerprint_,32);C::wipe(expected,sizeof expected);if(!equal)return rc?rc:int(C::Conflict);
  if(a.mode_==C::Initialization::Mode::ResumeSd){C::SdInitReceipt current;rc=selected(&current);if(rc)return rc;if(std::memcmp(current.bytes_,a.sdReceipt_.bytes_,48))return C::Conflict;rc=inventory(&current,true);}
  else rc=inventory(nullptr,false);
  return rc?rc:observe();
 }
 int initialize(){
  int rc=observe();if(rc)return rc;if(core_.bytes_||core_.admissionBusy_)return C::Conflict;
  auto&a=admission_;if(uint64_t(C::Header)+a.mapBytes_+4>core_.imageCapacity())return C::NoSpace;
  rc=verifyMap();if(rc)return rc;imageBytes_=C::Header+a.mapBytes_+4;image_=static_cast<uint8_t*>(core_.alloc_.allocate(core_.alloc_.context,imageBytes_));if(!image_)return C::NoMemory;
  C::makeSdImage(image_,a.namespace_,a.sdIdentity_,a.map_,a.mapBytes_,a.count_);if(!core_.validate(image_,imageBytes_))return C::Conflict;
  rc=fingerprint(a.sourceFingerprint_);if(rc)return rc;if(C::zero(a.sourceFingerprint_,32))return C::Invalid;
  // The raw checked store-selected record classifies create/resume. Bare
  // RTENINIT, Fresh/import receipts and selected roots cannot authorize restore.
  C::SdInitReceipt receipt;rc=selected(&receipt);
  if(!rc){if(std::memcmp(receipt.fingerprint(),a.sourceFingerprint_,32))return C::Conflict;a.sdReceipt_=receipt;a.mode_=C::Initialization::Mode::ResumeSd;rc=inventory(&receipt,true);}
  else if(rc==C::Missing){a.mode_=C::Initialization::Mode::CreateSd;rc=inventory(nullptr,false);}
  if(rc)return rc;
  a.prepared_=true;return core_.initializeCheckedSd(a);
 }
 static NativeSdCompletion::Result execute(void*p,const NativeSdCompletion::Evidence&e){
  auto&s=*static_cast<CheckedSdNativeRestore*>(p);auto&a=s.admission_;
  int rc=e.action==1&&e.count==a.count_&&e.mapBytes==a.mapBytes_&&C::sameIdentity(e.identity,a.sdIdentity_)?s.initialize():int(C::Conflict);
  s.store_.finishOperation();if(!s.store_.exitSafe()||!s.sources_.exitSafe())rc=C::Retained;s.checked(rc);
  return {rc,0,s.core_.revision()};
 }
};
}
