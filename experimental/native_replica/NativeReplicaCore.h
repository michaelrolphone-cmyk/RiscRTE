#pragma once
// Unconnected experimental core. No production Runtime routing or SDK ABI.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>
namespace RiscExperimental {
class NativeBaselineAdmission;
class CheckedNativeReplicaStore;
class CheckedFirstSdCheckpoint;
class CheckedSdNativeRestore;
class NativeReplicaCore {
 public:
 enum Result { Ok=0, Missing=-1, Invalid=-2, NoMemory=-3, NoSpace=-4,
               Io=-5, Uncertain=-6, Conflict=-7, Stale=-8, BufferSmall=-9, Busy=-10, Retained=-11, Initializing=-12 };
 struct Identity {
  bool known=false; uint64_t generation=0; uint8_t lineage[16]{}, digest[32]{};
 };
 struct Value { const char*key; const void*data; uint32_t size; };
 enum Kind:uint32_t { Put=1, Delete=2 };
 struct Mutation { const char*key; Kind kind; const void*data; uint32_t size; };
 struct Limits { uint32_t imageBytes=1024*1024, groupBytes=4096, intentBytes=1024; };
 struct Allocator {
  void*context;
  void*(*allocate)(void*,size_t);
  void(*release)(void*,void*);
 };
 class FreshInitReceipt;
 class ImportInitReceipt;
 class SdInitReceipt;
 class Initialization;
 // Backend requirements: fresh immutable object IDs; checked bounds; no ID
 // reuse; root replacement is one atomic durable record (old OR new on failure).
 // A failed root replacement/readback is ambiguous. Objects referenced by the
 // root may NEVER be removed. retire is best-effort garbage collection only.
 struct Backend {
  void*context;
  int(*readRoot)(void*,void*,uint32_t);
  int(*reserve)(void*,uint32_t,uint64_t*);
  int(*write)(void*,uint64_t,uint32_t,const void*,uint32_t);
  int(*read)(void*,uint64_t,uint32_t,void*,uint32_t);
  // Complete and durably verify the staged object before root publication.
  int(*seal)(void*,uint64_t);
  int(*replaceRoot)(void*,const void*,uint32_t);
  void(*retire)(void*,uint64_t);
  // Optional native-only FreshAbsenceV1 capability. FreshEmpty never falls
  // back to an unclassified initializer when any hook is absent.
  int(*readInitialization)(void*,FreshInitReceipt*)=nullptr;
  int(*beginFreshInitialization)(void*,const Initialization&)=nullptr;
  void(*endFreshInitialization)(void*)=nullptr;
  // Distinct, opt-in checked legacy import protocol. Never reuse Fresh receipts.
  int(*readImportedInitialization)(void*,ImportInitReceipt*)=nullptr;
  int(*beginImportedInitialization)(void*,const Initialization&)=nullptr;
  void(*endImportedInitialization)(void*)=nullptr;
  // Distinct checked-SD restoration; neither Fresh nor legacy-import receipts apply.
  int(*readSdInitialization)(void*,SdInitReceipt*)=nullptr;
  int(*beginSdInitialization)(void*,const Initialization&)=nullptr;
  void(*endSdInitialization)(void*)=nullptr;
 };
 struct Ticket { uint64_t serial=0, revision=0, through=0; uint32_t mapCrc=0; };
 enum class Origin:uint32_t {Unclassified=0,FreshEmpty=1,ImportedLegacy=2};
 class Baseline {
  public:
  Origin origin()const{return Origin(NativeReplicaCore::word(bytes_+12));}
  uint32_t mapSize()const{return NativeReplicaCore::word(bytes_+16);}
  uint32_t count()const{return NativeReplicaCore::word(bytes_+20);}
  const uint8_t*sourceFingerprint()const{return bytes_+24;}
  const uint8_t*mapDigest()const{return bytes_+56;}
  bool archiveObligation()const{return NativeReplicaCore::word(bytes_+88)!=0;}
  private:friend class NativeReplicaCore;friend class NativeBaselineAdmission;friend class CheckedNativeReplicaStore;friend class CheckedSdNativeRestore;uint8_t bytes_[96]{};
 };
 // A receipt is evidence only while its exact bytes are selected by its bound
 // store. No public decoder, setter, or caller-provided recovery argument.
 class FreshInitReceipt {
  public:
  bool valid()const{return backendContext_&&NativeReplicaCore::validFreshReceipt(bytes_);}
  uint32_t namespaceId()const{return valid()?NativeReplicaCore::word(bytes_+8):0;}
  const uint8_t*fingerprint()const{return bytes_+12;}
  private:
  friend class NativeReplicaCore;friend class NativeBaselineAdmission;friend class CheckedNativeReplicaStore;friend class CheckedSdNativeRestore;
  uint8_t bytes_[48]{};void*backendContext_=nullptr;
 };
 // RTENLI1 commits the exact source witness and its versioned, deterministic
 // UnanchoredNativeV1 canonical materializer, including archive obligation.
 // A store-selected receipt can only be constructed by the checked backend.
 class ImportInitReceipt {
  public:
  bool valid()const{return backendContext_&&NativeReplicaCore::validImportReceipt(bytes_);}
  uint32_t namespaceId()const{return valid()?NativeReplicaCore::word(bytes_+8):0;}
  const uint8_t*fingerprint()const{return bytes_+12;}
  private:
  friend class NativeReplicaCore;friend class NativeBaselineAdmission;friend class CheckedNativeReplicaStore;friend class CheckedSdNativeRestore;
  uint8_t bytes_[48]{};void*backendContext_=nullptr;
 };
 // The digest commits the checked native absence witness and the complete
 // canonical known-SD image. Only the selected checked store can decode it.
 class SdInitReceipt {
  public:
  bool valid()const{return backendContext_&&NativeReplicaCore::validSdReceipt(bytes_);}
  uint32_t namespaceId()const{return valid()?NativeReplicaCore::word(bytes_+8):0;}
  const uint8_t*fingerprint()const{return bytes_+12;}
  private:
  friend class NativeReplicaCore;friend class CheckedSdNativeRestore;friend class CheckedNativeReplicaStore;
  uint8_t bytes_[48]{};void*backendContext_=nullptr;
 };
 // Native-only one-shot capability. Only the bound inventory factory can fill
 // it; source bytes and fingerprint cannot be supplied as a detached setter.
 class Initialization {
  public:
  Initialization()=default;~Initialization(){reset();}
  Initialization(const Initialization&)=delete;Initialization&operator=(const Initialization&)=delete;
  bool valid()const{return prepared_&&!consumed_;}
  uint32_t namespaceId()const{return valid()?namespace_:0;}
  Baseline baseline()const{return baseline_;}
  const uint8_t*map()const{return valid()?map_:nullptr;}
  uint32_t mapSize()const{return valid()?mapBytes_:0;}
  uint32_t count()const{return valid()?count_:0;}
  void reset(){if(map_){NativeReplicaCore::wipe(map_,capacity_);alloc_.release(alloc_.context,map_);}map_=nullptr;capacity_=mapBytes_=count_=namespace_=0;prepared_=consumed_=activating_=false;core_=nullptr;factory_=owner_=backendContext_=nullptr;revalidate_=nullptr;freshCustody_=nullptr;importCustody_=nullptr;sdCustody_=nullptr;complete_=nullptr;leaseEpoch_=serial_=0;NativeReplicaCore::wipe(&baseline_,sizeof baseline_);NativeReplicaCore::wipe(sourceFingerprint_,32);sourceState_=sourceKind_=0;sourceRevision_=0;mode_=Mode::None;receipt_={};importReceipt_={};sdReceipt_={};sdIdentity_={};}
  private:
  friend class NativeReplicaCore;friend class NativeBaselineAdmission;friend class CheckedNativeReplicaStore;friend class CheckedSdNativeRestore;
  enum class Mode:uint32_t {None=0,CreateFresh=1,ResumeFresh=2,Imported=3,CreateImport=4,ResumeImport=5,CreateSd=6,ResumeSd=7};
  Allocator alloc_{};NativeReplicaCore*core_=nullptr;void*factory_=nullptr;void*owner_=nullptr;
  void*backendContext_=nullptr;Mode mode_=Mode::None;FreshInitReceipt receipt_{};ImportInitReceipt importReceipt_{};SdInitReceipt sdReceipt_{};Identity sdIdentity_{};
  int(*revalidate_)(void*,Initialization&,NativeReplicaCore&)=nullptr;void(*complete_)(void*,int)=nullptr;
  // Private operation-lifetime check. A selected receipt requests complete
  // normalized source revalidation; null observes custody/lease only.
  int(*freshCustody_)(void*,const Initialization&,const FreshInitReceipt*)=nullptr;
  int(*importCustody_)(void*,const Initialization&,const ImportInitReceipt*)=nullptr;
  int(*sdCustody_)(void*,const Initialization&,const SdInitReceipt*)=nullptr;
  uint8_t*map_=nullptr;uint32_t capacity_=0,mapBytes_=0,count_=0,namespace_=0;Baseline baseline_{};
  uint64_t leaseEpoch_=0,serial_=0,sourceRevision_=0;uint32_t sourceState_=0,sourceKind_=0;uint8_t sourceFingerprint_[32]{};
  bool prepared_=false,consumed_=false,activating_=false;
 };
 // Owned, immutable complete image custody. Exporters expose only the map;
 // pending intent bytes remain native and are never written to the SD format.
 class Snapshot {
  public:
  Snapshot()=default;
  ~Snapshot(){reset();}
  Snapshot(const Snapshot&)=delete;
  Snapshot&operator=(const Snapshot&)=delete;
  bool valid()const{return bytes_!=nullptr;}
  uint32_t namespaceId()const{return valid()?NativeReplicaCore::word(bytes_+8):0;}
  uint32_t imageSize()const{return valid()?size_:0;}
  uint32_t mapSize()const{return valid()?NativeReplicaCore::word(bytes_+104):0;}
  uint32_t count()const{return valid()?NativeReplicaCore::word(bytes_+96):0;}
  uint32_t headerSize()const{return valid()?NativeReplicaCore::imageHeader(bytes_):0;}
  uint32_t formatVersion()const{return valid()?NativeReplicaCore::imageVersion(bytes_):0;}
  Baseline baseline()const{return valid()?NativeReplicaCore::baselineAt(bytes_):Baseline{};}
  const uint8_t*map()const{return valid()?bytes_+headerSize():nullptr;}
  Identity base()const{return valid()?NativeReplicaCore::identityAt(bytes_):Identity{};}
  Ticket ticket()const{return ticket_;}
  void reset(){if(bytes_){NativeReplicaCore::wipe(bytes_,size_);alloc_.release(alloc_.context,bytes_);}bytes_=nullptr;size_=0;ticket_={};owner_=nullptr;}
  private:
  friend class NativeReplicaCore;
  uint8_t*bytes_=nullptr;uint32_t size_=0;Allocator alloc_{};Ticket ticket_{};const NativeReplicaCore*owner_=nullptr;
 };
 static constexpr uint32_t Header=128, HeaderV2=384, HeaderV3=480, HeaderV4=512, ProvenanceBytes=96, MarkerBytes=256, GroupHeader=88, RootBytes=48, ValueMax=2048;
 // A copied handle has authority only while these exact bytes are selected by
 // this native root. There is deliberately no decoder/import setter API.
 class ExportMarker {
  public:
  bool valid()const{return NativeReplicaCore::validMarkerEncoding(bytes_);}
  const uint8_t*encoded()const{return bytes_;}
  bool firstCreation()const{return valid()&&!std::memcmp(bytes_,"RTENEX02",8);}
  uint32_t namespaceId()const{return NativeReplicaCore::word(bytes_+8);}
  const uint8_t*incarnation()const{return bytes_+16;}
  uint64_t capturedRevision()const{return NativeReplicaCore::wide(bytes_+32);}
  uint64_t through()const{return NativeReplicaCore::wide(bytes_+40);}
  uint64_t previousAck()const{return NativeReplicaCore::wide(bytes_+48);}
  uint32_t prefixCount()const{return NativeReplicaCore::word(bytes_+56);}
  uint32_t prefixBytes()const{return NativeReplicaCore::word(bytes_+60);}
  uint32_t mapSize()const{return NativeReplicaCore::word(bytes_+64);}
  uint32_t count()const{return NativeReplicaCore::word(bytes_+68);}
  Identity base()const{return NativeReplicaCore::markerIdentity(bytes_+72);}
  Identity target()const{return NativeReplicaCore::markerIdentity(bytes_+136);}
  private:
  friend class NativeReplicaCore;
  uint8_t bytes_[MarkerBytes]{};
 };

 static void wipe(void*p,size_t n){auto*v=static_cast<volatile uint8_t*>(p);while(n--)*v++=0;}
 static Allocator heap(){return {nullptr,[](void*,size_t n)->void*{if(n>SIZE_MAX-sizeof(uint64_t))return nullptr;auto*p=static_cast<uint64_t*>(std::malloc(n+sizeof(uint64_t)));if(!p)return nullptr;*p=n;return p+1;},[](void*,void*p){auto*base=static_cast<uint64_t*>(p)-1;const size_t n=size_t(*base);wipe(p,n);wipe(base,sizeof(uint64_t));std::free(base);}};}
 NativeReplicaCore(uint32_t ns,const Backend&backend,const Limits&limits,const Allocator&alloc)
  :ns_(ns),b_(backend),limits_(limits),alloc_(alloc){}
 ~NativeReplicaCore(){drop(bytes_);}
 NativeReplicaCore(const NativeReplicaCore&)=delete;
 NativeReplicaCore&operator=(const NativeReplicaCore&)=delete;
 bool ready()const{return bytes_&&!uncertain_;}
 uint32_t namespaceId()const{return ns_;}
 // Read-only exact native composition check; it grants no initialization authority.
 bool usesBackend(const Backend&b)const{return b_.context==b.context&&b_.readRoot==b.readRoot&&b_.reserve==b.reserve&&b_.write==b.write&&b_.read==b.read&&b_.seal==b.seal&&b_.replaceRoot==b.replaceRoot&&b_.retire==b.retire&&b_.readInitialization==b.readInitialization&&b_.beginFreshInitialization==b.beginFreshInitialization&&b_.endFreshInitialization==b.endFreshInitialization&&b_.readImportedInitialization==b.readImportedInitialization&&b_.beginImportedInitialization==b.beginImportedInitialization&&b_.endImportedInitialization==b.endImportedInitialization&&b_.readSdInitialization==b.readSdInitialization&&b_.beginSdInitialization==b.beginSdInitialization&&b_.endSdInitialization==b.endSdInitialization;}
 uint32_t imageCapacity()const{return limits_.imageBytes;}
 uint32_t headerSize()const{return ready()?imageHeader(bytes_):0;}
 uint32_t formatVersion()const{return ready()?imageVersion(bytes_):0;}
 Baseline baseline()const{return ready()?baselineAt(bytes_):Baseline{};}
 bool pendingOnlyAdoptionAllowed()const{return ready()&&(identity().known||baseline().origin()==Origin::FreshEmpty);}
 bool exportIncarnationRequired()const{return ready()&&(headerSize()==Header||zero(bytes_+112,16));}
 bool exportPrepared()const{return ready()&&headerSize()>=HeaderV2&&!zero(bytes_+Header,MarkerBytes);}
 uint64_t revision()const{return ready()?wide(bytes_+16):0;}
 uint64_t sequence()const{return ready()?wide(bytes_+24):0;}
 uint32_t pendingCount()const{return ready()?word(bytes_+100):0;}
 Identity identity()const{return ready()?identityAt(bytes_):Identity{};}
 // Load never invents empty/default values on missing or corrupt storage.
 int load(){
  if(admissionBusy_)return Busy;
  if(!validConfiguration())return Invalid;
  ticketLive_=false;uncertain_=true;
  uint8_t root[RootBytes]{};int rc=b_.readRoot(b_.context,root,sizeof root);
  if(rc)return rc;
  if(!validRoot(root))return Io;
  const uint32_t size=word(root+12);auto*p=allocate(size);if(!p)return NoMemory;
  const uint64_t object=wide(root+24);
  for(uint32_t at=0;at<size;){const uint32_t n=chunk(size-at);rc=b_.read(b_.context,object,at,p+at,n);if(rc){drop(p);return rc;}at+=n;}
  if(!validate(p,size)||wide(p+16)!=wide(root+16)||word(p+size-4)!=word(root+32)){drop(p);return Io;}
  drop(bytes_);bytes_=p;size_=size;object_=object;uncertain_=false;return Ok;
 }
 // Compatibility initialization, only with absent root and no retained RAM image.
 // Origin stays Unclassified even for an empty map. A known base requires a
 // trusted controller's complete validated selected-baseline values.
 int initialize(const Value*values,uint32_t count,const Identity&base){
  if(admissionBusy_)return Busy;
  if(!validConfiguration()||!validIdentity(base)||!validValues(values,count))return Invalid;
  if(bytes_)return Conflict;
  uint8_t root[RootBytes]{};int rc=b_.readRoot(b_.context,root,sizeof root);
  if(rc!=Missing)return rc?rc:Conflict;
  uint64_t mapBytes=0;for(uint32_t i=0;i<count;++i)mapBytes+=20+values[i].size;
  const uint64_t total=Header+mapBytes+4;if(!fits(total))return NoSpace;
  auto*p=allocate(uint32_t(total));if(!p)return NoMemory;
  makeHeader(p,1,0,0,base,count,0,uint32_t(mapBytes),uint32_t(total),Header);
  uint32_t at=Header;for(uint32_t i=0;i<count;++i)at=writeValue(p,at,values[i]);finish(p,uint32_t(total));
  return publish(p,uint32_t(total));
 }
 int initializeFreshEmpty(Initialization&admission){return initializeAdmitted(admission,Origin::FreshEmpty);}
 int initializeImportedBaseline(Initialization&admission){return initializeAdmitted(admission,Origin::ImportedLegacy);}
 int initializeCheckedSd(Initialization&admission){return initializeSdAdmitted(admission);}
 int get(const char*key,void*out,uint32_t capacity,uint32_t*size)const{
  if(admissionBusy_)return Busy;
  if(size)*size=0;
  if(!size||!validKey(key)||(!out&&capacity))return Invalid;
  if(!ready())return Uncertain;
  uint32_t at=headerSize();for(uint32_t i=0;i<word(bytes_+96);++i){const uint32_t n=word(bytes_+at+16);
   if(!std::strcmp(reinterpret_cast<const char*>(bytes_+at),key)){*size=n;if(capacity<n)return BufferSmall;std::memcpy(out,bytes_+at+20,n);return Ok;}at+=20+n;}
  return Missing;
 }
 // A successful group makes map changes AND explicit intent durable together.
 // An empty or same-value edit is still an explicit operation. No implicit
 // defaults/migration writes are admitted through this method.
 int apply(uint64_t expectedRevision,const Mutation*changes,uint32_t count,const void*intent,uint32_t intentSize){
  if(admissionBusy_)return Busy;
  if(!ready())return Uncertain;
  if(expectedRevision!=revision())return Stale;
  if(!intent||!intentSize||intentSize>limits_.intentBytes||!validMutations(changes,count))return Invalid;
  if(revision()==UINT64_MAX||sequence()==UINT64_MAX)return NoSpace;
  uint64_t opBytes=0;for(uint32_t i=0;i<count;++i)opBytes+=24+changes[i].size;
  const uint64_t groupSize=GroupHeader+opBytes+intentSize;if(groupSize>limits_.groupBytes)return NoSpace;
  uint64_t mapBytes=0;uint32_t entries=0,at=headerSize();
  for(uint32_t i=0;i<word(bytes_+96);++i){const char*k=reinterpret_cast<const char*>(bytes_+at);const uint32_t n=word(bytes_+at+16);const auto*m=mutation(changes,count,k);
   if(!m){mapBytes+=20+n;++entries;}else if(m->kind==Put){mapBytes+=20+m->size;++entries;}at+=20+n;}
  for(uint32_t i=0;i<count;++i)if(changes[i].kind==Put&&!has(changes[i].key)){mapBytes+=20+changes[i].size;++entries;}
  const uint32_t oldGroups=size_-headerSize()-word(bytes_+104)-4;
  const uint64_t total=headerSize()+mapBytes+oldGroups+groupSize+4;if(!fits(total))return NoSpace;
  auto*p=allocate(uint32_t(total));if(!p)return NoMemory;
  makeHeader(p,revision()+1,sequence()+1,wide(bytes_+32),identity(),entries,pendingCount()+1,uint32_t(mapBytes),uint32_t(total));
  uint32_t out=headerSize();at=headerSize();
  for(uint32_t i=0;i<word(bytes_+96);++i){const char*k=reinterpret_cast<const char*>(bytes_+at);const uint32_t n=word(bytes_+at+16);const auto*m=mutation(changes,count,k);
   if(!m)out=writeValue(p,out,{k,bytes_+at+20,n});else if(m->kind==Put)out=writeValue(p,out,{k,m->data,m->size});at+=20+n;}
  for(uint32_t i=0;i<count;++i)if(changes[i].kind==Put&&!has(changes[i].key))out=writeValue(p,out,{changes[i].key,changes[i].data,changes[i].size});
  std::memcpy(p+out,bytes_+headerSize()+word(bytes_+104),oldGroups);out+=oldGroups;
  std::memset(p+out,0,GroupHeader);putWide(p+out,sequence()+1);putWide(p+out+8,identity().generation);putWord(p+out+16,identity().known?1:0);putWord(p+out+20,count);putWord(p+out+24,intentSize);putWord(p+out+28,uint32_t(opBytes));
  std::memcpy(p+out+32,identity().lineage,16);std::memcpy(p+out+48,identity().digest,32);out+=GroupHeader;
  for(uint32_t i=0;i<count;++i){std::memset(p+out,0,24);std::strcpy(reinterpret_cast<char*>(p+out),changes[i].key);putWord(p+out+16,changes[i].kind);putWord(p+out+20,changes[i].size);if(changes[i].size)std::memcpy(p+out+24,changes[i].data,changes[i].size);out+=24+changes[i].size;}
  std::memcpy(p+out,intent,intentSize);finish(p,uint32_t(total));return publish(p,uint32_t(total));
 }
 // Only current-process tickets are accepted. The transport must confirm that
 // this exact captured map is durably on SD, including checked close, first.
 // No "generation zero beats newer SD" comparison is made here.
 int capture(Ticket*out){
  if(admissionBusy_)return Busy;
  if(!out)return Invalid;
  if(!ready())return Uncertain;
  if(ticketSerial_==UINT64_MAX)return NoSpace;
  ticket_={++ticketSerial_,revision(),sequence(),crc(bytes_+headerSize(),word(bytes_+104))};ticketBase_=identity();ticketLive_=true;*out=ticket_;return Ok;
 }
 // Call under the native serialization lease. No storage I/O; copy precedes
 // ticket replacement so an allocation failure cannot invalidate a live export.
 int captureSnapshot(Snapshot*out){
  if(admissionBusy_)return Busy;
  if(!out||out->valid())return Invalid;
  if(!ready())return Uncertain;
  auto*p=allocate(size_);if(!p)return NoMemory;
  std::memcpy(p,bytes_,size_);Ticket t{};const int rc=capture(&t);
  if(rc){wipe(p,size_);drop(p);return rc;}
  out->bytes_=p;out->size_=size_;out->alloc_=alloc_;out->ticket_=t;out->owner_=this;return Ok;
 }
 // Explicit durable-export arm. The native owner supplies a fresh nonzero
 // incarnation for a v1 upgrade or zero-incarnation v3, never SD lineage or clock-derived IDs.
 // Target digest/map identity comes from trusted transport. Arm before SD I/O.
 int prepareExport(const Snapshot&snapshot,const Identity&target,const uint8_t*newIncarnation,ExportMarker*out){
  if(admissionBusy_)return Busy;
  if(!out)return Invalid;
  if(!ready())return Uncertain;
  if(exportPrepared())return Busy;
  if(!snapshot.valid()||snapshot.owner_!=this||!ticketLive_||!sameTicket(snapshot.ticket_,ticket_)||
     snapshot.size_!=size_||snapshot.ticket_.revision!=revision()||snapshot.ticket_.through!=sequence()||
     !sameIdentity(identity(),ticketBase_)||std::memcmp(snapshot.bytes_,bytes_,size_))return Stale;
  if(!validIdentity(target)||!target.known)return Invalid;
  const Identity base=identity();
  if(!base.known||base.generation==UINT64_MAX||target.generation!=base.generation+1||std::memcmp(base.lineage,target.lineage,16))return Conflict;
  if(headerSize()==Header||zero(bytes_+112,16)){if(!newIncarnation||zero(newIncarnation,16))return Invalid;}
  else if(newIncarnation&&std::memcmp(newIncarnation,bytes_+112,16))return Conflict;
  if(revision()==UINT64_MAX)return NoSpace;
  const uint32_t targetHeader=headerSize()==Header?HeaderV2:headerSize();
  const uint64_t total=uint64_t(size_)+targetHeader-headerSize();if(!fits(total))return NoSpace;
  const uint32_t mapBytes=word(bytes_+104),groups=size_-headerSize()-mapBytes-4;
  uint32_t at=headerSize()+mapBytes;uint64_t last=wide(bytes_+32);
  for(uint32_t i=0;i<pendingCount();++i){last=wide(bytes_+at);at+=groupSize(bytes_+at);}
  if(last!=sequence())return Conflict;
  auto*p=allocate(uint32_t(total));if(!p)return NoMemory;
  makeHeader(p,revision()+1,sequence(),wide(bytes_+32),base,word(bytes_+96),pendingCount(),mapBytes,uint32_t(total),targetHeader);
  if(headerSize()==Header||zero(bytes_+112,16))std::memcpy(p+112,newIncarnation,16);
  auto*m=p+Header;std::memcpy(m,"RTENEX01",8);putWord(m+8,ns_);putWord(m+12,1);std::memcpy(m+16,p+112,16);
  putWide(m+32,revision());putWide(m+40,sequence());putWide(m+48,wide(bytes_+32));putWord(m+56,pendingCount());putWord(m+60,groups);
  putWord(m+64,mapBytes);putWord(m+68,word(bytes_+96));putMarkerIdentity(m+72,base);putMarkerIdentity(m+136,target);
  putWord(m+200,crc(bytes_+headerSize()+mapBytes,groups));putWord(m+204,crc(bytes_+headerSize(),mapBytes));putWord(m+252,crc(m,252));
  // Includes every original group byte, not just IDs below a claimed frontier.
  std::memcpy(p+targetHeader,bytes_+headerSize(),size_-headerSize()-4);finish(p,uint32_t(total));
  const int rc=publish(p,uint32_t(total));if(rc==Ok){ticketLive_=false;std::memcpy(out->bytes_,bytes_+Header,MarkerBytes);}return rc;
 }
 int preparedExport(ExportMarker*out)const{
  if(admissionBusy_)return Busy;
  if(!out)return Invalid;
  if(!ready())return Uncertain;
  if(!exportPrepared())return Missing;
  std::memcpy(out->bytes_,bytes_+Header,MarkerBytes);return Ok;
 }
 // Caller holds fresh complete checked SD evidence for this exact target and
 // the exclusive same-medium lease/quiescence through this native transition.
 // Marker/root equality confers native authority; an inactive bank never does.
 int acknowledgeExport(const ExportMarker&expected,const Identity&verifiedTarget){
  if(admissionBusy_)return Busy;
  if(!ready())return Uncertain;
  if(!selectedMarker(expected))return Stale;
  if(!validIdentity(verifiedTarget)||!verifiedTarget.known)return Invalid;
  if(!sameIdentity(expected.target(),verifiedTarget))return Conflict;
  if(revision()==UINT64_MAX)return NoSpace;
  const uint32_t mapBytes=word(bytes_+104),removed=expected.prefixBytes(),oldHeader=headerSize();
  const uint32_t newHeader=expected.firstCreation()?HeaderV3:oldHeader,total=size_-removed-oldHeader+newHeader;
  auto*p=allocate(total);if(!p)return NoMemory;
  makeHeader(p,revision()+1,sequence(),expected.through(),verifiedTarget,word(bytes_+96),pendingCount()-expected.prefixCount(),mapBytes,total,newHeader);
  std::memset(p+Header,0,MarkerBytes);std::memcpy(p+newHeader,bytes_+oldHeader,mapBytes);
  const uint32_t groupsAt=oldHeader+mapBytes;
  std::memcpy(p+newHeader+mapBytes,bytes_+groupsAt+removed,size_-groupsAt-removed-4);finish(p,total);
  const int rc=publish(p,total);if(rc==Ok)ticketLive_=false;return rc;
 }
 // Caller freshly verified the exact previous base under the same held lease.
 // Retiring an unpublished attempt never acknowledges or rewrites local edits.
 int retireExport(const ExportMarker&expected,const Identity&verifiedBase){
  if(admissionBusy_)return Busy;
  if(!ready())return Uncertain;
  if(!selectedMarker(expected))return Stale;
  if(!validIdentity(verifiedBase)||!verifiedBase.known)return Invalid;
  if(expected.firstCreation()||!sameIdentity(expected.base(),verifiedBase))return Conflict;
  if(revision()==UINT64_MAX)return NoSpace;
  auto*p=allocate(size_);if(!p)return NoMemory;
  std::memcpy(p,bytes_,size_);putWide(p+16,revision()+1);std::memset(p+Header,0,MarkerBytes);finish(p,size_);
  const int rc=publish(p,size_);if(rc==Ok)ticketLive_=false;return rc;
 }
 int acknowledge(const Ticket&t,const Identity&confirmed){
  if(admissionBusy_)return Busy;
  if(!ready())return Uncertain;
  if(exportPrepared())return Busy;
  if(!ticketLive_||!sameTicket(t,ticket_)||!sameIdentity(identity(),ticketBase_))return Stale;
  if(!ticketBase_.known)return Conflict;
  if(!confirmed.known||!validIdentity(confirmed))return Invalid;
  if(ticketBase_.known&&std::memcmp(confirmed.lineage,ticketBase_.lineage,16))return Conflict;
  if(ticketBase_.known&&confirmed.generation<=ticketBase_.generation)return Conflict;
  const uint32_t mapBytes=word(bytes_+104);uint32_t at=headerSize()+mapBytes,kept=0,keptBytes=0;
  for(uint32_t i=0;i<pendingCount();++i){const uint32_t n=groupSize(bytes_+at);if(wide(bytes_+at)>t.through){++kept;keptBytes+=n;}at+=n;}
  const uint32_t total=headerSize()+mapBytes+keptBytes+4;auto*p=allocate(total);if(!p)return NoMemory;
  if(revision()==UINT64_MAX){drop(p);return NoSpace;}
  makeHeader(p,revision()+1,sequence(),t.through,confirmed,word(bytes_+96),kept,mapBytes,total);
  std::memcpy(p+headerSize(),bytes_+headerSize(),mapBytes);at=headerSize()+mapBytes;uint32_t out=at;
  for(uint32_t i=0;i<pendingCount();++i){const uint32_t n=groupSize(bytes_+at);if(wide(bytes_+at)>t.through){std::memcpy(p+out,bytes_+at,n);out+=n;}at+=n;}
  finish(p,total);int rc=publish(p,total);if(rc==Ok)ticketLive_=false;return rc;
 }
 // App-owned reconciliation supplies the complete resolved map and enumerates
 // EVERY pending operation it resolved, in order. These groups remain pending
 // until SD acknowledgement. This core cannot validate application semantics.
 // The revision compare prevents a newer local operation being overwritten.
 int installResolved(uint64_t expectedRevision,const Value*values,uint32_t count,const Identity&base,const uint64_t*resolved,uint32_t resolvedCount){
  if(admissionBusy_)return Busy;
  if(!ready())return Uncertain;
  if(exportPrepared())return Busy;
  if(expectedRevision!=revision())return Stale;
  if(!base.known||!validIdentity(base)||!validValues(values,count))return Invalid;
  if(!pendingOnlyAdoptionAllowed())return Conflict;
  if(identity().known&&(std::memcmp(identity().lineage,base.lineage,16)||base.generation<identity().generation||(base.generation==identity().generation&&std::memcmp(base.digest,identity().digest,32))))return Conflict;
  if(resolvedCount!=pendingCount()||(resolvedCount&&!resolved))return Conflict;
  uint32_t at=headerSize()+word(bytes_+104);for(uint32_t i=0;i<resolvedCount;++i){if(resolved[i]!=wide(bytes_+at))return Conflict;at+=groupSize(bytes_+at);}
  if(revision()==UINT64_MAX)return NoSpace;
  uint64_t mapBytes=0;for(uint32_t i=0;i<count;++i)mapBytes+=20+values[i].size;
  const uint32_t groups=size_-headerSize()-word(bytes_+104)-4;const uint64_t total=headerSize()+mapBytes+groups+4;if(!fits(total))return NoSpace;
  auto*p=allocate(uint32_t(total));if(!p)return NoMemory;
  makeHeader(p,revision()+1,sequence(),wide(bytes_+32),base,count,pendingCount(),uint32_t(mapBytes),uint32_t(total));
  at=headerSize();for(uint32_t i=0;i<count;++i)at=writeValue(p,at,values[i]);std::memcpy(p+at,bytes_+headerSize()+word(bytes_+104),groups);finish(p,uint32_t(total));
  int rc=publish(p,uint32_t(total));if(rc==Ok)ticketLive_=false;return rc;
 }
 // Copy one semantic intent, not credentials to logs. Cursor is a local op ID.
 int pending(uint64_t after,uint64_t*id,void*out,uint32_t cap,uint32_t*size)const{
  if(admissionBusy_)return Busy;
  if(size)*size=0;
  if(!id||!size||(!out&&cap))return Invalid;
  if(!ready())return Uncertain;
  uint32_t at=headerSize()+word(bytes_+104);for(uint32_t i=0;i<pendingCount();++i){const uint32_t n=groupSize(bytes_+at);
   if(wide(bytes_+at)>after){*id=wide(bytes_+at);*size=word(bytes_+at+24);if(cap<*size)return BufferSmall;std::memcpy(out,bytes_+at+GroupHeader+word(bytes_+at+28),*size);return Ok;}at+=n;}return Missing;
 }
 private:
 friend class NativeBaselineAdmission;friend class CheckedNativeReplicaStore;friend class CheckedSdNativeRestore;friend class CheckedFirstSdCheckpoint;
 // Separate first-create purpose. Only the checked creator can arm it after a
 // live vacant inventory. V4 carries the immutable captured map in every native
 // publication, so inactive-bank reuse and newer edits cannot destroy recovery.
 int prepareFirstExport(const Snapshot&s,const Identity&target,const uint8_t*incarnation,ExportMarker*out){
  if(admissionBusy_)return Busy;
  if(!ready())return Uncertain;
  if(!out||!s.valid())return Invalid;
  if(exportPrepared())return Busy;
  if(s.owner_!=this||!ticketLive_||!sameTicket(s.ticket_,ticket_)||s.size_!=size_||s.ticket_.revision!=revision()||s.ticket_.through!=sequence()||std::memcmp(s.bytes_,bytes_,size_))return Stale;
  const auto base=identity();
  if(!validIdentity(target)||!target.known)return Invalid;
  if(base.known?(base.generation==UINT64_MAX||target.generation!=base.generation+1||std::memcmp(base.lineage,target.lineage,16)):(target.generation!=1||baseline().origin()==Origin::Unclassified))return Conflict;
  if(headerSize()==Header||zero(bytes_+112,16)){if(!incarnation||zero(incarnation,16))return Invalid;}
  else if(incarnation&&std::memcmp(incarnation,bytes_+112,16))return Conflict;
  if(revision()==UINT64_MAX)return NoSpace;
  const uint32_t mapBytes=s.mapSize(),newHeader=HeaderV4+mapBytes;
  const uint64_t total=uint64_t(size_)-headerSize()+newHeader;
  if(!fits(total))return NoSpace;
  auto*p=allocate(uint32_t(total));if(!p)return NoMemory;
  makeHeader(p,revision()+1,sequence(),wide(bytes_+32),base,s.count(),pendingCount(),mapBytes,uint32_t(total),newHeader);
  if(headerSize()==Header||zero(bytes_+112,16))std::memcpy(p+112,incarnation,16);
  if(headerSize()<HeaderV3){auto*b=p+HeaderV2;std::memcpy(b,"RTENBP1",8);putWord(b+8,ns_);putWord(b+92,crc(b,92));}
  putWord(p+HeaderV3,mapBytes);if(mapBytes)std::memcpy(p+HeaderV4,s.map(),mapBytes);
  auto*m=p+Header;std::memcpy(m,"RTENEX02",8);putWord(m+8,ns_);putWord(m+12,2);std::memcpy(m+16,p+112,16);
  const uint32_t groups=size_-headerSize()-mapBytes-4;
  putWide(m+32,revision());putWide(m+40,sequence());putWide(m+48,wide(bytes_+32));putWord(m+56,pendingCount());putWord(m+60,groups);
  putWord(m+64,mapBytes);putWord(m+68,s.count());putMarkerIdentity(m+72,base);putMarkerIdentity(m+136,target);
  putWord(m+200,crc(bytes_+headerSize()+mapBytes,groups));putWord(m+204,crc(s.map(),mapBytes));putWord(m+252,crc(m,252));
  std::memcpy(p+newHeader,bytes_+headerSize(),size_-headerSize()-4);finish(p,uint32_t(total));
  const int rc=publish(p,uint32_t(total));if(!rc){ticketLive_=false;std::memcpy(out->bytes_,bytes_+Header,MarkerBytes);}return rc;
 }
 int firstExportSnapshot(const ExportMarker&m,Snapshot*out){
  if(admissionBusy_)return Busy;
  if(!ready())return Uncertain;
  if(!out||out->valid())return Invalid;
  if(!selectedMarker(m)||!m.firstCreation()||formatVersion()!=4)return Stale;
  const uint32_t total=Header+m.mapSize()+4;auto*p=allocate(total);if(!p)return NoMemory;
  makeHeader(p,m.capturedRevision(),m.through(),m.previousAck(),m.base(),m.count(),0,m.mapSize(),total,Header);
  if(m.mapSize())std::memcpy(p+Header,bytes_+HeaderV4,m.mapSize());
  finish(p,total);
  out->bytes_=p;out->size_=total;out->alloc_=alloc_;out->owner_=this;return Ok;
 }
 bool admissionBusy_=false;
 struct AdmissionScope {bool&busy;explicit AdmissionScope(bool&b):busy(b){busy=true;}~AdmissionScope(){busy=false;}};
 struct AdmissionCompletion {Initialization&admission;int&result;~AdmissionCompletion(){admission.complete_(admission.factory_,result);}};
 struct FreshScope {Backend&backend;bool started=false,imported=false;~FreshScope(){if(started){if(imported)backend.endImportedInitialization(backend.context);else backend.endFreshInitialization(backend.context);}}};
 struct SdScope {Backend&backend;bool started=false;~SdScope(){if(started)backend.endSdInitialization(backend.context);}};
 int initializeSdAdmitted(Initialization&a){
  if(admissionBusy_)return Busy;
  if(!validConfiguration()||!sdHooks()||!a.valid()||a.core_!=this||a.backendContext_!=b_.context||a.namespace_!=ns_||!a.factory_||!a.owner_||!a.revalidate_||!a.complete_||!a.sdCustody_||!a.sdIdentity_.known||!validIdentity(a.sdIdentity_)||(a.mode_!=Initialization::Mode::CreateSd&&a.mode_!=Initialization::Mode::ResumeSd))return Invalid;
  AdmissionScope scope(admissionBusy_);a.consumed_=true;int rc=Ok;AdmissionCompletion complete{a,rc};SdScope permit{b_};
  rc=a.sdCustody_(a.factory_,a,nullptr);if(rc)return rc;
  if(bytes_)return rc=Conflict;
  const uint64_t total=uint64_t(Header)+a.mapBytes_+4;if(!fits(total))return rc=NoSpace;
  auto*p=allocate(uint32_t(total));if(!p)return rc=NoMemory;
  makeSdImage(p,ns_,a.sdIdentity_,a.map_,a.mapBytes_,a.count_);
  if(!validate(p,uint32_t(total))){drop(p);return rc=Invalid;}
  rc=a.revalidate_(a.factory_,a,*this);if(rc){drop(p);return rc;}
  permit.started=true;{AdmissionScope activation(a.activating_);rc=b_.beginSdInitialization(b_.context,a);}
  if(rc){drop(p);return rc;}
  return rc=publish(p,uint32_t(total));
 }
 static void makeSdImage(uint8_t*p,uint32_t ns,const Identity&id,const uint8_t*map,uint32_t bytes,uint32_t count){
  const uint32_t total=Header+bytes+4;std::memset(p,0,Header);std::memcpy(p,"RTENR01",8);putWord(p+8,ns);putWord(p+12,1);putWide(p+16,1);putWide(p+40,id.generation);std::memcpy(p+48,id.lineage,16);std::memcpy(p+64,id.digest,32);putWord(p+96,count);putWord(p+104,bytes);putWord(p+108,total);if(bytes)std::memcpy(p+Header,map,bytes);finish(p,total);
 }
 int initializeAdmitted(Initialization&a,Origin origin){
  if(admissionBusy_)return Busy;
  if(!validConfiguration()||!a.valid()||a.core_!=this||a.backendContext_!=b_.context||a.namespace_!=ns_||!a.factory_||!a.owner_||!a.revalidate_||!a.complete_||a.baseline_.origin()!=origin)return Invalid;
  if(origin!=Origin::FreshEmpty&&origin!=Origin::ImportedLegacy)return Invalid;
  const bool fresh=origin==Origin::FreshEmpty,checkedImport=a.mode_==Initialization::Mode::CreateImport||a.mode_==Initialization::Mode::ResumeImport;
  if(fresh?(a.mapBytes_||a.count_||!freshHooks()||!a.freshCustody_||(a.mode_!=Initialization::Mode::CreateFresh&&a.mode_!=Initialization::Mode::ResumeFresh)):checkedImport?(!importHooks()||!a.importCustody_):a.mode_!=Initialization::Mode::Imported)return Invalid;
  AdmissionScope scope(admissionBusy_);a.consumed_=true;
  int rc=Ok;AdmissionCompletion complete{a,rc};FreshScope permit{b_};permit.imported=checkedImport;
  // Observe Retained before allocation/ordinary errors. Fresh allocation and
  // canonical construction precede the final full source/root revalidation.
  rc=fresh?a.freshCustody_(a.factory_,a,nullptr):checkedImport?a.importCustody_(a.factory_,a,nullptr):a.revalidate_(a.factory_,a,*this);if(rc)return rc;
  if(!fresh&&!checkedImport){uint8_t root[RootBytes]{};rc=b_.readRoot(b_.context,root,sizeof root);if(rc!=Missing)return rc=rc?rc:int(Conflict);}
  const uint64_t total=uint64_t(HeaderV3)+a.mapBytes_+4;if(!fits(total))return rc=NoSpace;
  auto*p=allocate(uint32_t(total));if(!p)return rc=NoMemory;
  if(fresh)makeFreshEmpty(p,ns_);
  else{
   makeHeader(p,1,0,0,{},a.count_,0,a.mapBytes_,uint32_t(total),HeaderV3);
   std::memset(p+112,0,HeaderV3-112);std::memcpy(p+HeaderV2,a.baseline_.bytes_,ProvenanceBytes);
   if(a.mapBytes_)std::memcpy(p+HeaderV3,a.map_,a.mapBytes_);
   finish(p,uint32_t(total));
  }
  if(!validate(p,uint32_t(total))){drop(p);return rc=Invalid;}
  if(fresh||checkedImport){
   rc=a.revalidate_(a.factory_,a,*this);if(rc){drop(p);return rc;}
   // The immutable, consumed admission is live only for this hook invocation.
   // end also runs after a failed begin: volatile cleanup never retires receipt.
   permit.started=true;
   {AdmissionScope activation(a.activating_);rc=checkedImport?b_.beginImportedInitialization(b_.context,a):b_.beginFreshInitialization(b_.context,a);}
   if(rc){drop(p);return rc;}
  }
  return rc=publish(p,uint32_t(total));
 }
 uint32_t ns_;Backend b_;Limits limits_;Allocator alloc_;uint8_t*bytes_=nullptr;uint32_t size_=0;uint64_t object_=0;bool uncertain_=false;
 Ticket ticket_{};Identity ticketBase_{};uint64_t ticketSerial_=0;bool ticketLive_=false;
 static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 static void putWord(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
 static uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
 static void putWide(uint8_t*p,uint64_t v){putWord(p,uint32_t(v));putWord(p+4,uint32_t(v>>32));}
 static uint32_t crc(const void*v,uint32_t n){auto*p=static_cast<const uint8_t*>(v);uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
 static bool validFreshReceipt(const uint8_t*p){return !std::memcmp(p,"RTENFI1",8)&&word(p+8)&&word(p+8)<=INT32_MAX&&!zero(p+12,32)&&word(p+44)==crc(p,44);}
 static bool validSdReceipt(const uint8_t*p){return !std::memcmp(p,"RTENSI1",8)&&word(p+8)&&word(p+8)<=INT32_MAX&&!zero(p+12,32)&&word(p+44)==crc(p,44);}
 static bool validImportReceipt(const uint8_t*p){return !std::memcmp(p,"RTENLI1",8)&&word(p+8)&&word(p+8)<=INT32_MAX&&!zero(p+12,32)&&word(p+44)==crc(p,44);}
 static void makeFreshEmpty(uint8_t*p,uint32_t ns){
  std::memset(p,0,HeaderV3+4);std::memcpy(p,"RTENR03",8);putWord(p+8,ns);putWide(p+16,1);putWord(p+108,HeaderV3+4);
  auto*b=p+HeaderV2;std::memcpy(b,"RTENBP1",8);putWord(b+8,ns);putWord(b+12,uint32_t(Origin::FreshEmpty));putWord(b+92,crc(b,92));finish(p,HeaderV3+4);
 }
 static uint32_t chunk(uint32_t n){return n>4096?4096:n;}
 static bool validKey(const char*s){if(!s||!*s)return false;for(unsigned i=0;i<16;++i){const char c=s[i];if(!c)return true;if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;}return false;}
 static bool zero(const uint8_t*p,uint32_t n){while(n--)if(*p++)return false;return true;}
 static bool validIdentity(const Identity&i){return i.known?!zero(i.lineage,16):!i.generation&&zero(i.lineage,16)&&zero(i.digest,32);}
 static bool sameIdentity(const Identity&a,const Identity&b){return a.known==b.known&&a.generation==b.generation&&!std::memcmp(a.lineage,b.lineage,16)&&!std::memcmp(a.digest,b.digest,32);}
 static bool sameTicket(const Ticket&a,const Ticket&b){return a.serial==b.serial&&a.revision==b.revision&&a.through==b.through&&a.mapCrc==b.mapCrc;}
 static Identity identityAt(const uint8_t*p){Identity i{};i.known=word(p+12)!=0;i.generation=wide(p+40);std::memcpy(i.lineage,p+48,16);std::memcpy(i.digest,p+64,32);return i;}
 static uint32_t imageVersion(const uint8_t*p){return !std::memcmp(p,"RTENR01",8)?1:!std::memcmp(p,"RTENR02",8)?2:!std::memcmp(p,"RTENR03",8)?3:!std::memcmp(p,"RTENR04",8)?4:0;}
 static uint32_t imageHeader(const uint8_t*p){const uint32_t v=imageVersion(p);return v==1?Header:v==2?HeaderV2:v==3?HeaderV3:v==4?HeaderV4+word(p+HeaderV3):0;}
 static Baseline baselineAt(const uint8_t*p){Baseline result;if(imageVersion(p)>=3)std::memcpy(result.bytes_,p+HeaderV2,ProvenanceBytes);return result;}
 bool validBaseline(const uint8_t*p)const{
  if(std::memcmp(p,"RTENBP1",8)||word(p+8)!=ns_||word(p+12)>uint32_t(Origin::ImportedLegacy)||word(p+92)!=crc(p,92))return false;
  if(word(p+12)!=uint32_t(Origin::ImportedLegacy))return zero(p+16,76);
  const uint32_t bytes=word(p+16),count=word(p+20);
  return !zero(p+24,32)&&!zero(p+56,32)&&word(p+88)==1&&bytes<=limits_.imageBytes-HeaderV3-4&&(!bytes)==(!count)&&uint64_t(count)*21<=bytes;
 }
 static Identity markerIdentity(const uint8_t*p){Identity i{};i.known=word(p)!=0;i.generation=wide(p+8);std::memcpy(i.lineage,p+16,16);std::memcpy(i.digest,p+32,32);return i;}
 static void putMarkerIdentity(uint8_t*p,const Identity&i){putWord(p,i.known?1:0);putWord(p+4,0);putWide(p+8,i.generation);std::memcpy(p+16,i.lineage,16);std::memcpy(p+32,i.digest,32);}
 static bool validMarkerEncoding(const uint8_t*m){
  const bool first=!std::memcmp(m,"RTENEX02",8);
  if((!first&&std::memcmp(m,"RTENEX01",8))||!word(m+8)||word(m+12)!=(first?2u:1u)||zero(m+16,16)||!wide(m+32)||wide(m+48)>wide(m+40)||
     !zero(m+208,44)||word(m+252)!=crc(m,252)||word(m+72)>1||(!first&&word(m+72)!=1)||word(m+136)!=1||!zero(m+76,4)||!zero(m+140,4))return false;
  const Identity base=markerIdentity(m+72),target=markerIdentity(m+136);
  if(!validIdentity(base)||!validIdentity(target))return false;
  if(base.known?(base.generation==UINT64_MAX||target.generation!=base.generation+1||std::memcmp(base.lineage,target.lineage,16)):(!first||target.generation!=1))return false;
  if((!word(m+56))!=(!word(m+60))||uint64_t(word(m+56))*(GroupHeader+25)>word(m+60)||
     (!word(m+68))!=(!word(m+64))||uint64_t(word(m+68))*21>word(m+64))return false;
  return true;
 }
 // Image/map/groups have already passed bounds/encoding validation here.
 bool validSelectedMarker(const uint8_t*p,uint32_t n)const{
  const auto*m=p+Header;
  const bool first=!std::memcmp(m,"RTENEX02",8);
  if(first!=(imageVersion(p)==4))return false;
  if(first&&(word(p+HeaderV3)!=word(m+64)||word(m+204)!=crc(p+HeaderV4,word(m+64))||(!markerIdentity(m+72).known&&baselineAt(p).origin()==Origin::Unclassified)))return false;
  if(!validMarkerEncoding(m)||word(m+8)!=ns_||std::memcmp(m+16,p+112,16)||wide(m+32)>=wide(p+16)||
     wide(m+40)>wide(p+24)||wide(m+48)!=wide(p+32)||!sameIdentity(markerIdentity(m+72),identityAt(p))||
     wide(p+16)-wide(m+32)-1!=wide(p+24)-wide(m+40)||word(m+56)>word(p+100)||
     !fits(uint64_t(imageHeader(p))+word(m+64)+word(m+60)+4))return false;
  const uint32_t start=imageHeader(p)+word(p+104);uint32_t at=start;uint64_t last=wide(p+32);
  for(uint32_t i=0;i<word(m+56);++i){if(wide(p+at)>wide(m+40))return false;last=wide(p+at);at+=groupSize(p+at);}
  if(at-start!=word(m+60)||last!=wide(m+40)||word(m+200)!=crc(p+start,at-start)||
     (at<n-4&&wide(p+at)<=wide(m+40)))return false;
  // Only apply may change an armed map. At the capture frontier it is still exact.
  return wide(p+24)!=wide(m+40)||(word(m+64)==word(p+104)&&word(m+68)==word(p+96)&&word(m+204)==crc(p+imageHeader(p),word(p+104)));
 }
 bool selectedMarker(const ExportMarker&m)const{return exportPrepared()&&!std::memcmp(m.bytes_,bytes_+Header,MarkerBytes)&&validSelectedMarker(bytes_,size_);}
 static bool validValues(const Value*v,uint32_t n){if(n&&!v)return false;for(uint32_t i=0;i<n;++i){if(!validKey(v[i].key)||!v[i].data||!v[i].size||v[i].size>ValueMax)return false;for(uint32_t j=0;j<i;++j)if(!std::strcmp(v[i].key,v[j].key))return false;}return true;}
 static bool validMutations(const Mutation*m,uint32_t n){if(!n||!m)return false;for(uint32_t i=0;i<n;++i){if(!validKey(m[i].key)||(m[i].kind!=Put&&m[i].kind!=Delete)||(m[i].kind==Put&&(!m[i].data||!m[i].size||m[i].size>ValueMax))||(m[i].kind==Delete&&(m[i].size||m[i].data)))return false;for(uint32_t j=0;j<i;++j)if(!std::strcmp(m[i].key,m[j].key))return false;}return true;}
 bool validConfiguration()const{return ns_&&limits_.imageBytes>=Header+4&&limits_.groupBytes>=GroupHeader+25&&limits_.intentBytes&&limits_.intentBytes<=limits_.groupBytes&&alloc_.allocate&&alloc_.release&&b_.readRoot&&b_.reserve&&b_.write&&b_.read&&b_.seal&&b_.replaceRoot;}
 bool sdHooks()const{return ns_<=INT32_MAX&&b_.context&&b_.readSdInitialization&&b_.beginSdInitialization&&b_.endSdInitialization;}
 bool importHooks()const{return ns_<=INT32_MAX&&b_.context&&b_.readImportedInitialization&&b_.beginImportedInitialization&&b_.endImportedInitialization;}
 bool freshHooks()const{return ns_<=INT32_MAX&&b_.context&&b_.readInitialization&&b_.beginFreshInitialization&&b_.endFreshInitialization;}
 bool fits(uint64_t n)const{return n<=UINT32_MAX&&n<=limits_.imageBytes&&n>=Header+4;}
 uint8_t*allocate(uint32_t n){return alloc_.allocate?static_cast<uint8_t*>(alloc_.allocate(alloc_.context,n)):nullptr;}
 void drop(void*p){if(p&&alloc_.release)alloc_.release(alloc_.context,p);}
 bool has(const char*k)const{uint32_t at=headerSize();for(uint32_t i=0;i<word(bytes_+96);++i){if(!std::strcmp(reinterpret_cast<const char*>(bytes_+at),k))return true;at+=20+word(bytes_+at+16);}return false;}
 static const Mutation*mutation(const Mutation*m,uint32_t n,const char*k){for(uint32_t i=0;i<n;++i)if(!std::strcmp(m[i].key,k))return m+i;return nullptr;}
 static uint32_t writeValue(uint8_t*p,uint32_t at,const Value&v){std::memset(p+at,0,20);std::strcpy(reinterpret_cast<char*>(p+at),v.key);putWord(p+at+16,v.size);std::memcpy(p+at+20,v.data,v.size);return at+20+v.size;}
 void makeHeader(uint8_t*p,uint64_t rev,uint64_t seq,uint64_t ack,const Identity&base,uint32_t entries,uint32_t groups,uint32_t mapBytes,uint32_t total,uint32_t header=0){
  if(!header)header=headerSize();
  std::memset(p,0,header);std::memcpy(p,header>=HeaderV4?"RTENR04":header==HeaderV3?"RTENR03":header==HeaderV2?"RTENR02":"RTENR01",7);
  if(header>=HeaderV2&&bytes_&&imageHeader(bytes_)>=HeaderV2){std::memcpy(p+112,bytes_+112,HeaderV2-112);if(header>=HeaderV3&&imageVersion(bytes_)>=3)std::memcpy(p+HeaderV2,bytes_+HeaderV2,ProvenanceBytes);if(header>=HeaderV4&&imageVersion(bytes_)==4&&header==headerSize())std::memcpy(p+HeaderV3,bytes_+HeaderV3,header-HeaderV3);}
  putWord(p+8,ns_);putWord(p+12,base.known?1:0);putWide(p+16,rev);putWide(p+24,seq);putWide(p+32,ack);putWide(p+40,base.generation);std::memcpy(p+48,base.lineage,16);std::memcpy(p+64,base.digest,32);putWord(p+96,entries);putWord(p+100,groups);putWord(p+104,mapBytes);putWord(p+108,total);}
 static void finish(uint8_t*p,uint32_t n){putWord(p+n-4,crc(p,n-4));}
 static uint32_t groupSize(const uint8_t*p){return GroupHeader+word(p+24)+word(p+28);}
 bool validate(const uint8_t*p,uint32_t n)const{
  if(!fits(n))return false;
  if(imageVersion(p)==4&&(n<HeaderV4+4||word(p+HeaderV3)>n-HeaderV4-4||!zero(p+HeaderV3+4,28)||zero(p+Header,MarkerBytes)))return false;
  const uint32_t header=imageHeader(p);
  if(!header||n<header+4||word(p+8)!=ns_||word(p+12)>1||!wide(p+16)||wide(p+32)>wide(p+24)||word(p+108)!=n||(header==Header?!zero(p+112,16):header==HeaderV2?zero(p+112,16):(!validBaseline(p+HeaderV2)||(!zero(p+Header,MarkerBytes)&&zero(p+112,16))))||word(p+n-4)!=crc(p,n-4)||!validIdentity(identityAt(p))||word(p+104)>n-header-4)return false;
  uint32_t at=header;const uint32_t mapEnd=header+word(p+104);
  for(uint32_t i=0;i<word(p+96);++i){if(at>mapEnd||mapEnd-at<20||!std::memchr(p+at,0,16)||!validKey(reinterpret_cast<const char*>(p+at)))return false;const uint32_t len=word(p+at+16);if(!len||len>ValueMax||len>mapEnd-at-20)return false;
   for(uint32_t prev=header;prev<at;prev+=20+word(p+prev+16))if(!std::strcmp(reinterpret_cast<const char*>(p+prev),reinterpret_cast<const char*>(p+at)))return false;
   at+=20+len;}
  if(at!=mapEnd)return false;
  uint64_t prior=wide(p+32);
  for(uint32_t i=0;i<word(p+100);++i){if(at>n-4||n-4-at<GroupHeader)return false;const auto*g=p+at;const uint64_t size=uint64_t(GroupHeader)+word(g+24)+word(g+28);if(size>limits_.groupBytes||size>n-4-at||!word(g+24)||word(g+24)>limits_.intentBytes||!word(g+20)||wide(g)<=prior||wide(g)>wide(p+24)||word(g+16)>1||!zero(g+80,8))return false;
   Identity base{};base.known=word(g+16)!=0;base.generation=wide(g+8);std::memcpy(base.lineage,g+32,16);std::memcpy(base.digest,g+48,32);if(!validIdentity(base))return false;
   uint32_t offset=GroupHeader;const uint32_t end=GroupHeader+word(g+28);
   for(uint32_t j=0;j<word(g+20);++j){if(offset>end||end-offset<24||!std::memchr(g+offset,0,16)||!validKey(reinterpret_cast<const char*>(g+offset)))return false;const uint32_t kind=word(g+offset+16),len=word(g+offset+20);if((kind!=Put&&kind!=Delete)||(kind==Put&&(!len||len>ValueMax))||(kind==Delete&&len)||len>end-offset-24)return false;
    for(uint32_t prev=GroupHeader;prev<offset;prev+=24+word(g+prev+20))if(!std::strcmp(reinterpret_cast<const char*>(g+prev),reinterpret_cast<const char*>(g+offset)))return false;
    offset+=24+len;}
   if(offset!=end)return false;
   prior=wide(g);at+=uint32_t(size);}
  return at==n-4&&(header==Header||zero(p+Header,MarkerBytes)||validSelectedMarker(p,n));
 }
 bool validRoot(const uint8_t*p)const{return !std::memcmp(p,"RTENROOT",8)&&word(p+8)==ns_&&fits(word(p+12))&&wide(p+16)&&wide(p+24)&&zero(p+36,8)&&word(p+44)==crc(p,44);}
 int publish(uint8_t*p,uint32_t n){
  if(!validate(p,n)){drop(p);return Invalid;}
  uint64_t object=0;int rc=b_.reserve(b_.context,n,&object);if(rc){drop(p);return rc;}if(!object||object==object_){drop(p);return Io;}
  for(uint32_t at=0;at<n;){const uint32_t size=chunk(n-at);rc=b_.write(b_.context,object,at,p+at,size);if(rc){if(b_.retire)b_.retire(b_.context,object);drop(p);return rc;}at+=size;}
  rc=b_.seal(b_.context,object);if(rc){if(b_.retire)b_.retire(b_.context,object);drop(p);return rc;}
  uint8_t check[4096]{};
  struct Wipe {uint8_t*p;size_t n;~Wipe(){NativeReplicaCore::wipe(p,n);}} scratch{check,sizeof check};
  for(uint32_t at=0;at<n;){const uint32_t size=chunk(n-at);rc=b_.read(b_.context,object,at,check,size);if(rc||std::memcmp(check,p+at,size)){if(b_.retire)b_.retire(b_.context,object);drop(p);return rc?rc:Io;}at+=size;}
  uint8_t root[RootBytes]{};std::memcpy(root,"RTENROOT",8);putWord(root+8,ns_);putWord(root+12,n);putWide(root+16,wide(p+16));putWide(root+24,object);putWord(root+32,word(p+n-4));putWord(root+44,crc(root,44));
  rc=b_.replaceRoot(b_.context,root,sizeof root);uint8_t actual[RootBytes]{};if(!rc)rc=b_.readRoot(b_.context,actual,sizeof actual);
  // Root custody may be retained as well as ambiguous; never hide that status
  // from the owning worker, while still making native reads require recovery.
  if(rc||std::memcmp(root,actual,sizeof root)){uncertain_=true;ticketLive_=false;drop(p);return rc==Retained?Retained:Uncertain;}
  const auto old=object_;drop(bytes_);bytes_=p;size_=n;object_=object;uncertain_=false;if(old&&b_.retire)b_.retire(b_.context,old);return Ok;
 }
};
}
