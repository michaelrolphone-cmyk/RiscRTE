#pragma once
#include "NativeReplicaCore.h"
namespace RiscExperimental {
// Native-only admission/materialization. No public app ABI or product inventory
// implementation. The owner and all callback contexts must outlive admissions.
class NativeBaselineAdmission {
 public:
 using C=NativeReplicaCore;
 static constexpr uint32_t FreshAbsenceV1=1,ImportedRecoveryV1=1;
 enum class RootState:uint32_t {NeverInitialized=1,InterruptedInit=2,Selected=3};
 struct Selection {uint32_t kind=0;uint64_t revision=0;uint8_t fingerprint[32]{};};
 struct Witness {uint32_t namespaceId=0;RootState root=RootState::Selected;uint32_t kind=0;uint64_t revision=0;uint8_t fingerprint[32]{};};
 struct Rows {void*context;int(*append)(void*,const char*,const void*,uint32_t);};
 struct Guard {void*context;bool(*workerWithLease)(void*);uint64_t(*leaseEpoch)(void*);};
 struct Digest {void*context;bool(*begin)(void*);bool(*update)(void*,const void*,uint32_t);bool(*finish)(void*,uint8_t[32]);};
 // These are trusted owner-supplied read-only FACT collectors, not approval
 // callbacks. FreshAbsenceV1 explicitly commits the stable logical native owner,
 // namespace, source/materializer schema and complete bounded inventory of
 // root/sequence/banks/private staging and competing legacy rows/anchors/journals.
 // The fingerprint covers these same source revision facts, never a RAM pointer
 // or live lease epoch. Resume checks owned-attempt artifacts before normalizing
 // only their receipt/sequence/bank/stage facts to original absence. Competing
 // native sources are never normalized away. FreshEmpty
 // never requires an SD card/catalog, import materializer, digest or entropy
 // service. Unknown SD lineage is handled by later selected-base reconciliation.
 // A retained native provider still keeps custody. Bare
 // RTENINIT never proves NeverInitialized. Imported materialization must be
 // complete and coherent; all original sources remain an archive obligation.
 // Recheck reproduces this exact witness under the SAME native lease/epoch.
 // No callback may reenter this factory/core or retain an append input pointer.
 struct Sources {
  void*context;
  int(*observe)(void*); // metadata only; Retained outranks lease loss
  int(*freshInventory)(void*,uint32_t,Witness*);
  int(*materialize)(void*,uint32_t,const Selection&,const Rows&,Witness*);
  int(*recheck)(void*,uint32_t,const Witness&);
  // Both collectors are required for FreshEmpty: an initially absent creation
  // also rechecks owned-attempt inventory after its receipt becomes durable.
  // recheck accepts that same normalized NeverInitialized witness on resume.
  uint32_t freshInventoryVersion=0;
  int(*resumeFreshInventory)(void*,uint32_t,const C::FreshInitReceipt&,Witness*)=nullptr;
  // ImportedRecoveryV1 is opt-in for the fixed UnanchoredNativeV1 decoder.
  // It requires all three backend hooks and both collectors; a partially wired
  // versioned source fails closed. Version zero preserves existing one-shot
  // synthetic/other import callers, and is never a receipt-resume fallback.
  uint32_t importedInventoryVersion=0;
  int(*resumeImportedMaterialize)(void*,uint32_t,const C::ImportInitReceipt&,const Rows&,Witness*)=nullptr;
  int(*recheckImported)(void*,uint32_t,const C::ImportInitReceipt&,const Witness&)=nullptr;
 };
 NativeBaselineAdmission(void*owner,C&core,Guard guard,Digest digest,Sources sources,C::Allocator alloc=C::heap())
  :owner_(owner),core_(core),guard_(guard),hash_(digest),sources_(sources),alloc_(alloc){}
 NativeBaselineAdmission(const NativeBaselineAdmission&)=delete;NativeBaselineAdmission&operator=(const NativeBaselineAdmission&)=delete;
 bool exitSafe()const{return !retained_&&!entered_&&!core_.admissionBusy_&&!consuming_;}
 int admitFreshEmpty(C::Initialization*out){return admit(nullptr,out);}
 int admitFreshResume(C::Initialization*out){return admit(nullptr,out,true);}
 int admitImported(const Selection&selection,C::Initialization*out){return admit(&selection,out);}
 int admitImportedResume(C::Initialization*out){return admit(nullptr,out,false,true);}
 private:
 void*owner_;C&core_;Guard guard_;Digest hash_;Sources sources_;C::Allocator alloc_;uint64_t serial_=0,active_=0;const C::Initialization*consuming_=nullptr;bool entered_=false,retained_=false;
 struct Entry {NativeBaselineAdmission&f;bool lockCore;Entry(NativeBaselineAdmission&v,bool core):f(v),lockCore(core){f.entered_=true;if(lockCore)f.core_.admissionBusy_=true;}~Entry(){if(lockCore)f.core_.admissionBusy_=false;f.entered_=false;}};
 struct Sink {NativeBaselineAdmission*factory;C::Initialization*admission;int error=C::Ok;};
 int retain(int rc){if(rc==C::Retained)retained_=true;return rc;}
 bool importRecovery()const{return sources_.importedInventoryVersion==ImportedRecoveryV1&&core_.importHooks()&&sources_.resumeImportedMaterialize&&sources_.recheckImported;}
 bool configured(bool imported)const{return owner_&&guard_.workerWithLease&&guard_.leaseEpoch&&sources_.observe&&sources_.recheck&&(imported?(hash_.begin&&hash_.update&&hash_.finish&&sources_.materialize&&alloc_.allocate&&alloc_.release&&(!sources_.importedInventoryVersion||importRecovery())):(core_.freshHooks()&&sources_.freshInventoryVersion==FreshAbsenceV1&&sources_.freshInventory&&sources_.resumeFreshInventory));}
 int custody(uint64_t expected=0){
  if(retained_)return C::Retained;
  const int state=sources_.observe(sources_.context);if(state!=C::Ok)return retain(state);
  const bool held=guard_.workerWithLease(guard_.context);
  const uint64_t epoch=guard_.leaseEpoch(guard_.context);
  // A guard callback can discover retained custody while reporting lost lease
  // or epoch. Observe again before returning either ordinary failure.
  const int after=sources_.observe(sources_.context);if(after!=C::Ok)return retain(after);
  if(!held)return C::Busy;
  if(!epoch||(expected&&epoch!=expected))return C::Stale;
  return C::Ok;
 }
 int absent(){uint8_t root[C::RootBytes]{};const int rc=core_.b_.readRoot(core_.b_.context,root,sizeof root);C::wipe(root,sizeof root);return rc==C::Missing?int(C::Ok):retain(rc?rc:int(C::Conflict));}
 int selectedReceipt(C::FreshInitReceipt*out){
  uint8_t root[C::RootBytes]{};int rc=core_.b_.readRoot(core_.b_.context,root,sizeof root);C::wipe(root,sizeof root);
  if(rc!=C::Initializing)return retain(rc?rc:int(C::Conflict));
  rc=core_.b_.readInitialization(core_.b_.context,out);if(rc)return retain(rc);
  return out->valid()&&out->backendContext_==core_.b_.context&&out->namespaceId()==core_.namespaceId()?int(C::Ok):int(C::Io);
 }
 int rootMatches(bool resume,const C::FreshInitReceipt&receipt){
  if(!resume)return absent();
  C::FreshInitReceipt current;const int rc=selectedReceipt(&current);if(rc)return rc;
  return current.backendContext_==receipt.backendContext_&&!std::memcmp(current.bytes_,receipt.bytes_,sizeof current.bytes_)?int(C::Ok):int(C::Stale);
 }
 int selectedImportReceipt(C::ImportInitReceipt*out){
  uint8_t root[C::RootBytes]{};int rc=core_.b_.readRoot(core_.b_.context,root,sizeof root);C::wipe(root,sizeof root);
  if(rc!=C::Initializing)return retain(rc?rc:int(C::Conflict));
  rc=core_.b_.readImportedInitialization(core_.b_.context,out);if(rc)return retain(rc);
  return out->valid()&&out->backendContext_==core_.b_.context&&out->namespaceId()==core_.namespaceId()?int(C::Ok):int(C::Io);
 }
 int importRootMatches(bool resume,const C::ImportInitReceipt&receipt){
  if(!resume)return absent();
  C::ImportInitReceipt current;const int rc=selectedImportReceipt(&current);if(rc)return rc;
  return current.backendContext_==receipt.backendContext_&&!std::memcmp(current.bytes_,receipt.bytes_,sizeof current.bytes_)?int(C::Ok):int(C::Stale);
 }
 static bool same(const Selection&s,const Witness&w){return s.kind==w.kind&&s.revision==w.revision&&!std::memcmp(s.fingerprint,w.fingerprint,32);}
 static bool same(const Witness&a,const Witness&b){return a.namespaceId==b.namespaceId&&a.root==b.root&&a.kind==b.kind&&a.revision==b.revision&&!std::memcmp(a.fingerprint,b.fingerprint,32);}
 static bool validWitness(const Witness&w,uint32_t ns,bool imported){return w.namespaceId==ns&&!C::zero(w.fingerprint,32)&&(imported?(w.kind&&(w.root==RootState::NeverInitialized||w.root==RootState::InterruptedInit)):(w.root==RootState::NeverInitialized&&!w.kind));}
 static int row(void*p,const char*key,const void*value,uint32_t size){auto&s=*static_cast<Sink*>(p);auto&a=*s.admission;if(s.error)return s.error;
  C::Value item{key,value,size};if(!C::validValues(&item,1))return s.error=C::Invalid;
  if(uint64_t(a.mapBytes_)+20+size>a.capacity_)return s.error=C::NoSpace;
  for(uint32_t at=0;at<a.mapBytes_;at+=20+C::word(a.map_+at+16))if(!std::strcmp(reinterpret_cast<const char*>(a.map_+at),key))return s.error=C::Conflict;
  a.mapBytes_=C::writeValue(a.map_,a.mapBytes_,item);++a.count_;return C::Ok;
 }
 int hashMap(const C::Initialization&a,uint8_t out[32],uint64_t expectedEpoch=0){
  const uint64_t epoch=expectedEpoch?expectedEpoch:a.leaseEpoch_;
  uint8_t prefix[12]{};C::putWord(prefix,core_.namespaceId());C::putWord(prefix+4,a.count_);C::putWord(prefix+8,a.mapBytes_);
  // A digest callback can discover retained custody while returning failure.
  // Observe after every callback before converting false to an ordinary Io.
  auto checked=[&](bool ok){const int rc=custody(epoch);return rc?rc:ok?int(C::Ok):int(C::Io);};
  int rc=custody(epoch);if(rc)return rc;
  rc=checked(hash_.begin(hash_.context));if(rc)return rc;
  rc=checked(hash_.update(hash_.context,prefix,sizeof prefix));if(rc)return rc;
  for(uint32_t at=0;at<a.mapBytes_;){const uint32_t n=C::chunk(a.mapBytes_-at);rc=checked(hash_.update(hash_.context,a.map_+at,n));if(rc)return rc;at+=n;}
  return checked(hash_.finish(hash_.context,out));
 }
 int admit(const Selection*selection,C::Initialization*out,bool resume=false,bool resumeImport=false){
  const bool imported=selection||resumeImport,checkedImport=imported&&sources_.importedInventoryVersion;
  if(entered_||core_.admissionBusy_)return C::Busy;
  if(retained_)return C::Retained;
  if(!out||out->prepared_||out->map_||!configured(imported)||(resumeImport&&!importRecovery())||!core_.validConfiguration())return C::Invalid;
  if(selection&&(!selection->kind||C::zero(selection->fingerprint,32)))return C::Invalid;
  Entry entry(*this,true);int rc=custody();if(rc)return rc;
  if(core_.bytes_)return C::Conflict;
  if(core_.imageCapacity()<C::HeaderV3+4||serial_==UINT64_MAX)return C::NoSpace;
  const uint64_t epoch=guard_.leaseEpoch(guard_.context);C::FreshInitReceipt receipt;C::ImportInitReceipt importReceipt;
  rc=resumeImport?selectedImportReceipt(&importReceipt):resume?selectedReceipt(&receipt):absent();if(rc)return rc;
  Witness witness{};
  if(imported){
   out->capacity_=core_.imageCapacity()-C::HeaderV3-4;out->alloc_=alloc_;
   if(out->capacity_){out->map_=static_cast<uint8_t*>(alloc_.allocate(alloc_.context,out->capacity_));if(!out->map_){out->reset();return C::NoMemory;}std::memset(out->map_,0,out->capacity_);}
   Sink sink{this,out,C::Ok};Rows rows{&sink,row};rc=resumeImport?sources_.resumeImportedMaterialize(sources_.context,core_.namespaceId(),importReceipt,rows,&witness):sources_.materialize(sources_.context,core_.namespaceId(),*selection,rows,&witness);if(!rc)rc=sink.error;
  }else if(resume)rc=sources_.resumeFreshInventory(sources_.context,core_.namespaceId(),receipt,&witness);
  else rc=sources_.freshInventory(sources_.context,core_.namespaceId(),&witness);
  if(rc){out->reset();return retain(rc);}
  if(!validWitness(witness,core_.namespaceId(),imported)||(selection&&!same(*selection,witness))||(resume&&std::memcmp(witness.fingerprint,receipt.fingerprint(),32))||(resumeImport&&std::memcmp(witness.fingerprint,importReceipt.fingerprint(),32))){out->reset();return C::Conflict;}
  auto*b=out->baseline_.bytes_;std::memcpy(b,"RTENBP1",8);C::putWord(b+8,core_.namespaceId());C::putWord(b+12,uint32_t(imported?C::Origin::ImportedLegacy:C::Origin::FreshEmpty));
  if(imported){C::putWord(b+16,out->mapBytes_);C::putWord(b+20,out->count_);std::memcpy(b+24,witness.fingerprint,32);C::putWord(b+88,1);rc=hashMap(*out,b+56,epoch);if(rc||C::zero(b+56,32)){out->reset();return rc?rc:int(C::Invalid);}}
  C::putWord(b+92,C::crc(b,92));rc=custody(epoch);if(!rc)rc=resumeImport?importRootMatches(true,importReceipt):rootMatches(resume,receipt);if(rc){out->reset();return rc;}
  out->core_=&core_;out->factory_=this;out->owner_=owner_;out->namespace_=core_.namespaceId();out->leaseEpoch_=epoch;out->serial_=active_=++serial_;
  out->backendContext_=core_.b_.context;out->receipt_=receipt;out->importReceipt_=importReceipt;out->mode_=checkedImport?(resumeImport?C::Initialization::Mode::ResumeImport:C::Initialization::Mode::CreateImport):imported?C::Initialization::Mode::Imported:resume?C::Initialization::Mode::ResumeFresh:C::Initialization::Mode::CreateFresh;
  out->sourceState_=uint32_t(witness.root);out->sourceKind_=witness.kind;out->sourceRevision_=witness.revision;std::memcpy(out->sourceFingerprint_,witness.fingerprint,32);
  out->revalidate_=[](void*p,C::Initialization&a,C&c){return static_cast<NativeBaselineAdmission*>(p)->revalidate(a,c);};
  out->freshCustody_=[](void*p,const C::Initialization&a,const C::FreshInitReceipt*r){return static_cast<NativeBaselineAdmission*>(p)->freshCustody(a,r);};
  out->importCustody_=[](void*p,const C::Initialization&a,const C::ImportInitReceipt*r){return static_cast<NativeBaselineAdmission*>(p)->importCustody(a,r);};
  out->complete_=[](void*p,int result){auto&f=*static_cast<NativeBaselineAdmission*>(p);f.consuming_=nullptr;f.retain(result);};out->prepared_=true;return C::Ok;
 }
 bool bound(const C::Initialization&a)const{return a.core_==&core_&&a.factory_==this&&a.owner_==owner_&&a.backendContext_==core_.b_.context&&a.namespace_==core_.namespaceId()&&a.serial_;}
 int freshCustody(const C::Initialization&a,const C::FreshInitReceipt*receipt){
  if(retained_)return C::Retained;
  if(entered_)return C::Busy;
  // Before allocation, Core may observe custody for the exact still-pending
  // serial. Store checks after revalidation require the exact active admission.
  if(!bound(a)||!a.prepared_||!a.consumed_||!core_.admissionBusy_||a.baseline_.origin()!=C::Origin::FreshEmpty||
     (consuming_!=&a&&(consuming_||receipt||active_!=a.serial_)))return C::Stale;
  Entry entry(*this,false);int rc=custody(a.leaseEpoch_);if(rc||!receipt)return rc;
  if(!receipt->valid()||receipt->backendContext_!=a.backendContext_||receipt->namespaceId()!=a.namespace_||
     std::memcmp(receipt->fingerprint(),a.sourceFingerprint_,32)||
     (a.mode_==C::Initialization::Mode::ResumeFresh&&std::memcmp(receipt->bytes_,a.receipt_.bytes_,sizeof receipt->bytes_)))return C::Stale;
  Witness expected{a.namespace_,RootState(a.sourceState_),a.sourceKind_,a.sourceRevision_,{}};std::memcpy(expected.fingerprint,a.sourceFingerprint_,32);
  Witness current{};rc=sources_.resumeFreshInventory(sources_.context,a.namespace_,*receipt,&current);if(rc)return retain(rc);
  if(!same(expected,current))return C::Conflict;
  rc=sources_.recheck(sources_.context,a.namespace_,current);if(rc)return retain(rc);
  return custody(a.leaseEpoch_);
 }
 int importCustody(const C::Initialization&a,const C::ImportInitReceipt*receipt){
  if(retained_)return C::Retained;
  if(entered_)return C::Busy;
  if(!bound(a)||!a.prepared_||!a.consumed_||!core_.admissionBusy_||a.baseline_.origin()!=C::Origin::ImportedLegacy||!importRecovery()||
     (consuming_!=&a&&(consuming_||receipt||active_!=a.serial_)))return C::Stale;
  Entry entry(*this,false);int rc=custody(a.leaseEpoch_);if(rc||!receipt)return rc;
  if(!receipt->valid()||receipt->backendContext_!=a.backendContext_||receipt->namespaceId()!=a.namespace_||
     std::memcmp(receipt->fingerprint(),a.sourceFingerprint_,32)||
     (a.mode_==C::Initialization::Mode::ResumeImport&&std::memcmp(receipt->bytes_,a.importReceipt_.bytes_,sizeof receipt->bytes_)))return C::Stale;
  Witness expected{a.namespace_,RootState(a.sourceState_),a.sourceKind_,a.sourceRevision_,{}};std::memcpy(expected.fingerprint,a.sourceFingerprint_,32);
  rc=sources_.recheckImported(sources_.context,a.namespace_,*receipt,expected);if(rc)return retain(rc);
  uint8_t digest[32]{};rc=hashMap(a,digest);const bool equal=!rc&&!std::memcmp(digest,a.baseline_.mapDigest(),32);C::wipe(digest,sizeof digest);
  if(!equal)return rc?rc:int(C::Conflict);
  return custody(a.leaseEpoch_);
 }
 int revalidate(C::Initialization&a,C&core){
  if(entered_)return C::Busy;
  if(retained_)return C::Retained;
  if(&core!=&core_||!bound(a)||a.serial_!=active_)return C::Stale;
  Entry entry(*this,false);active_=0;int rc=custody(a.leaseEpoch_);if(rc)return rc;
  if(core_.bytes_)return C::Conflict;
  Witness witness{a.namespace_,RootState(a.sourceState_),a.sourceKind_,a.sourceRevision_,{}};std::memcpy(witness.fingerprint,a.sourceFingerprint_,32);
  const bool imported=a.baseline_.origin()==C::Origin::ImportedLegacy,resume=a.mode_==C::Initialization::Mode::ResumeFresh,resumeImport=a.mode_==C::Initialization::Mode::ResumeImport;
  if(!configured(imported)||!validWitness(witness,core_.namespaceId(),imported))return C::Invalid;
  if(!imported){
   rc=rootMatches(resume,a.receipt_);if(rc)return rc;
   Witness current{};
   rc=resume?sources_.resumeFreshInventory(sources_.context,core_.namespaceId(),a.receipt_,&current):sources_.freshInventory(sources_.context,core_.namespaceId(),&current);
   if(rc)return retain(rc);
   if(!same(witness,current)||(resume&&std::memcmp(current.fingerprint,a.receipt_.fingerprint(),32)))return C::Conflict;
  }
  rc=resumeImport?sources_.recheckImported(sources_.context,core_.namespaceId(),a.importReceipt_,witness):sources_.recheck(sources_.context,core_.namespaceId(),witness);if(rc)return retain(rc);
  if(imported){uint8_t digest[32]{};rc=hashMap(a,digest);const bool equal=!rc&&!std::memcmp(digest,a.baseline_.mapDigest(),32);C::wipe(digest,sizeof digest);if(!equal)return rc?rc:int(C::Conflict);}
  rc=custody(a.leaseEpoch_);if(!rc)rc=resumeImport?importRootMatches(true,a.importReceipt_):rootMatches(resume,a.receipt_);if(!rc)rc=custody(a.leaseEpoch_);if(!rc)consuming_=&a;return rc;
 }
};
}
