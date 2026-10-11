#pragma once
#include "NativeReplicaCore.h"
namespace RiscExperimental {
// Native-only admission/materialization. No public app ABI or product inventory
// implementation. The owner and all callback contexts must outlive admissions.
class NativeBaselineAdmission {
 public:
 using C=NativeReplicaCore;
 enum class RootState:uint32_t {NeverInitialized=1,InterruptedInit=2,Selected=3};
 struct Selection {uint32_t kind=0;uint64_t revision=0;uint8_t fingerprint[32]{};};
 struct Witness {uint32_t namespaceId=0;RootState root=RootState::Selected;uint32_t kind=0;uint64_t revision=0;uint8_t fingerprint[32]{};};
 struct Rows {void*context;int(*append)(void*,const char*,const void*,uint32_t);};
 struct Guard {void*context;bool(*workerWithLease)(void*);uint64_t(*leaseEpoch)(void*);};
 struct Digest {void*context;bool(*begin)(void*);bool(*update)(void*,const void*,uint32_t);bool(*finish)(void*,uint8_t[32]);};
 // These are trusted owner-supplied read-only FACT collectors, not approval
 // callbacks. Fresh inventory includes raw root/init sentinel, both native
 // banks and competing native legacy rows/anchors/journals only. FreshEmpty
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
 };
 NativeBaselineAdmission(void*owner,C&core,Guard guard,Digest digest,Sources sources,C::Allocator alloc=C::heap())
  :owner_(owner),core_(core),guard_(guard),hash_(digest),sources_(sources),alloc_(alloc){}
 NativeBaselineAdmission(const NativeBaselineAdmission&)=delete;NativeBaselineAdmission&operator=(const NativeBaselineAdmission&)=delete;
 bool exitSafe()const{return !retained_&&!entered_;}
 int admitFreshEmpty(C::Initialization*out){return admit(nullptr,out);}
 int admitImported(const Selection&selection,C::Initialization*out){return admit(&selection,out);}
 private:
 void*owner_;C&core_;Guard guard_;Digest hash_;Sources sources_;C::Allocator alloc_;uint64_t serial_=0,active_=0;bool entered_=false,retained_=false;
 struct Entry {NativeBaselineAdmission&f;bool lockCore;Entry(NativeBaselineAdmission&v,bool core):f(v),lockCore(core){f.entered_=true;if(lockCore)f.core_.admissionBusy_=true;}~Entry(){if(lockCore)f.core_.admissionBusy_=false;f.entered_=false;}};
 struct Sink {NativeBaselineAdmission*factory;C::Initialization*admission;int error=C::Ok;};
 int retain(int rc){if(rc==C::Retained)retained_=true;return rc;}
 bool configured(bool imported)const{return owner_&&guard_.workerWithLease&&guard_.leaseEpoch&&sources_.observe&&sources_.recheck&&(imported?(hash_.begin&&hash_.update&&hash_.finish&&sources_.materialize&&alloc_.allocate&&alloc_.release):sources_.freshInventory!=nullptr);}
 int custody(uint64_t expected=0){
  if(retained_)return C::Retained;
  const int state=sources_.observe(sources_.context);if(state!=C::Ok)return retain(state);
  if(!guard_.workerWithLease(guard_.context))return C::Busy;
  const uint64_t epoch=guard_.leaseEpoch(guard_.context);if(!epoch||(expected&&epoch!=expected))return C::Stale;
  return C::Ok;
 }
 int absent(){uint8_t root[C::RootBytes]{};const int rc=core_.b_.readRoot(core_.b_.context,root,sizeof root);C::wipe(root,sizeof root);return rc==C::Missing?int(C::Ok):retain(rc?rc:int(C::Conflict));}
 static bool same(const Selection&s,const Witness&w){return s.kind==w.kind&&s.revision==w.revision&&!std::memcmp(s.fingerprint,w.fingerprint,32);}
 static bool validWitness(const Witness&w,uint32_t ns,bool imported){return w.namespaceId==ns&&!C::zero(w.fingerprint,32)&&(imported?(w.kind&&(w.root==RootState::NeverInitialized||w.root==RootState::InterruptedInit)):(w.root==RootState::NeverInitialized&&!w.kind));}
 static int row(void*p,const char*key,const void*value,uint32_t size){auto&s=*static_cast<Sink*>(p);auto&a=*s.admission;if(s.error)return s.error;
  C::Value item{key,value,size};if(!C::validValues(&item,1))return s.error=C::Invalid;
  if(uint64_t(a.mapBytes_)+20+size>a.capacity_)return s.error=C::NoSpace;
  for(uint32_t at=0;at<a.mapBytes_;at+=20+C::word(a.map_+at+16))if(!std::strcmp(reinterpret_cast<const char*>(a.map_+at),key))return s.error=C::Conflict;
  a.mapBytes_=C::writeValue(a.map_,a.mapBytes_,item);++a.count_;return C::Ok;
 }
 int hashMap(const C::Initialization&a,uint8_t out[32]){
  uint8_t prefix[12]{};C::putWord(prefix,core_.namespaceId());C::putWord(prefix+4,a.count_);C::putWord(prefix+8,a.mapBytes_);
  if(!hash_.begin(hash_.context)||!hash_.update(hash_.context,prefix,sizeof prefix))return C::Io;
  for(uint32_t at=0;at<a.mapBytes_;){const uint32_t n=C::chunk(a.mapBytes_-at);if(!hash_.update(hash_.context,a.map_+at,n))return C::Io;at+=n;}
  return hash_.finish(hash_.context,out)?int(C::Ok):int(C::Io);
 }
 int admit(const Selection*selection,C::Initialization*out){
  if(entered_||core_.admissionBusy_)return C::Busy;
  if(retained_)return C::Retained;
  if(!out||out->prepared_||out->map_||!configured(selection!=nullptr)||!core_.validConfiguration())return C::Invalid;
  if(selection&&(!selection->kind||C::zero(selection->fingerprint,32)))return C::Invalid;
  Entry entry(*this,true);int rc=custody();if(rc)return rc;
  if(core_.bytes_)return C::Conflict;
  if(core_.imageCapacity()<C::HeaderV3+4||serial_==UINT64_MAX)return C::NoSpace;
  const uint64_t epoch=guard_.leaseEpoch(guard_.context);rc=absent();if(rc)return rc;
  Witness witness{};
  if(selection){
   out->capacity_=core_.imageCapacity()-C::HeaderV3-4;out->alloc_=alloc_;
   if(out->capacity_){out->map_=static_cast<uint8_t*>(alloc_.allocate(alloc_.context,out->capacity_));if(!out->map_){out->reset();return C::NoMemory;}std::memset(out->map_,0,out->capacity_);}
   Sink sink{this,out,C::Ok};Rows rows{&sink,row};rc=sources_.materialize(sources_.context,core_.namespaceId(),*selection,rows,&witness);if(!rc)rc=sink.error;
  }else rc=sources_.freshInventory(sources_.context,core_.namespaceId(),&witness);
  if(rc){out->reset();return retain(rc);}
  if(!validWitness(witness,core_.namespaceId(),selection!=nullptr)||(selection&&!same(*selection,witness))){out->reset();return C::Conflict;}
  auto*b=out->baseline_.bytes_;std::memcpy(b,"RTENBP1",8);C::putWord(b+8,core_.namespaceId());C::putWord(b+12,uint32_t(selection?C::Origin::ImportedLegacy:C::Origin::FreshEmpty));
  if(selection){C::putWord(b+16,out->mapBytes_);C::putWord(b+20,out->count_);std::memcpy(b+24,witness.fingerprint,32);C::putWord(b+88,1);rc=hashMap(*out,b+56);if(rc||C::zero(b+56,32)){out->reset();return rc?rc:int(C::Invalid);}}
  C::putWord(b+92,C::crc(b,92));rc=custody(epoch);if(!rc)rc=absent();if(rc){out->reset();return rc;}
  out->core_=&core_;out->factory_=this;out->owner_=owner_;out->namespace_=core_.namespaceId();out->leaseEpoch_=epoch;out->serial_=active_=++serial_;
  out->sourceState_=uint32_t(witness.root);out->sourceKind_=witness.kind;out->sourceRevision_=witness.revision;std::memcpy(out->sourceFingerprint_,witness.fingerprint,32);
  out->revalidate_=[](void*p,C::Initialization&a,C&c){return static_cast<NativeBaselineAdmission*>(p)->revalidate(a,c);};
  out->complete_=[](void*p,int result){static_cast<NativeBaselineAdmission*>(p)->retain(result);};out->prepared_=true;return C::Ok;
 }
 int revalidate(C::Initialization&a,C&core){
  if(entered_)return C::Busy;
  if(retained_)return C::Retained;
  if(&core!=&core_||a.factory_!=this||a.owner_!=owner_||a.namespace_!=core_.namespaceId()||!a.serial_||a.serial_!=active_)return C::Stale;
  Entry entry(*this,false);active_=0;int rc=custody(a.leaseEpoch_);if(rc)return rc;
  if(core_.bytes_)return C::Conflict;
  Witness witness{a.namespace_,RootState(a.sourceState_),a.sourceKind_,a.sourceRevision_,{}};std::memcpy(witness.fingerprint,a.sourceFingerprint_,32);
  if(!validWitness(witness,core_.namespaceId(),a.baseline_.origin()==C::Origin::ImportedLegacy))return C::Invalid;
  rc=sources_.recheck(sources_.context,core_.namespaceId(),witness);if(rc)return retain(rc);
  if(a.baseline_.origin()==C::Origin::ImportedLegacy){uint8_t digest[32]{};rc=hashMap(a,digest);const bool equal=!rc&&!std::memcmp(digest,a.baseline_.mapDigest(),32);C::wipe(digest,sizeof digest);if(!equal)return rc?rc:int(C::Conflict);}
  rc=custody(a.leaseEpoch_);return rc?rc:absent();
 }
};
}
