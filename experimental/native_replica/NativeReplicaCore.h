#pragma once
// Unconnected experimental core. No production Runtime routing or SDK ABI.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>
namespace RiscExperimental {
class NativeReplicaCore {
 public:
 enum Result { Ok=0, Missing=-1, Invalid=-2, NoMemory=-3, NoSpace=-4,
               Io=-5, Uncertain=-6, Conflict=-7, Stale=-8, BufferSmall=-9, Busy=-10, Retained=-11 };
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
 };
 struct Ticket { uint64_t serial=0, revision=0, through=0; uint32_t mapCrc=0; };
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
  uint32_t formatVersion()const{return valid()?(headerSize()==NativeReplicaCore::HeaderV2?2:1):0;}
  const uint8_t*map()const{return valid()?bytes_+headerSize():nullptr;}
  Identity base()const{return valid()?NativeReplicaCore::identityAt(bytes_):Identity{};}
  Ticket ticket()const{return ticket_;}
  void reset(){if(bytes_){NativeReplicaCore::wipe(bytes_,size_);alloc_.release(alloc_.context,bytes_);}bytes_=nullptr;size_=0;ticket_={};owner_=nullptr;}
  private:
  friend class NativeReplicaCore;
  uint8_t*bytes_=nullptr;uint32_t size_=0;Allocator alloc_{};Ticket ticket_{};const NativeReplicaCore*owner_=nullptr;
 };
 static constexpr uint32_t Header=128, HeaderV2=384, MarkerBytes=256, GroupHeader=88, RootBytes=48, ValueMax=2048;
 // A copied handle has authority only while these exact bytes are selected by
 // this native root. There is deliberately no decoder/import setter API.
 class ExportMarker {
  public:
  bool valid()const{return NativeReplicaCore::validMarkerEncoding(bytes_);}
  const uint8_t*encoded()const{return bytes_;}
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
 uint32_t imageCapacity()const{return limits_.imageBytes;}
 uint32_t headerSize()const{return ready()?imageHeader(bytes_):0;}
 uint32_t formatVersion()const{return ready()?(headerSize()==HeaderV2?2:1):0;}
 bool exportPrepared()const{return ready()&&headerSize()==HeaderV2&&!zero(bytes_+Header,MarkerBytes);}
 uint64_t revision()const{return ready()?wide(bytes_+16):0;}
 uint64_t sequence()const{return ready()?wide(bytes_+24):0;}
 uint32_t pendingCount()const{return ready()?word(bytes_+100):0;}
 Identity identity()const{return ready()?identityAt(bytes_):Identity{};}
 // Load never invents empty/default values on missing or corrupt storage.
 int load(){
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
 // Explicit initial migration/fresh-state creation, only when no root exists.
 // Identity remains unknown after a full erase until a remote base is adopted.
 int initialize(const Value*values,uint32_t count,const Identity&base){
  if(!validConfiguration()||!validIdentity(base)||!validValues(values,count))return Invalid;
  uint8_t root[RootBytes]{};int rc=b_.readRoot(b_.context,root,sizeof root);
  if(rc!=Missing)return rc?rc:Conflict;
  uint64_t mapBytes=0;for(uint32_t i=0;i<count;++i)mapBytes+=20+values[i].size;
  const uint64_t total=Header+mapBytes+4;if(!fits(total))return NoSpace;
  auto*p=allocate(uint32_t(total));if(!p)return NoMemory;
  makeHeader(p,1,0,0,base,count,0,uint32_t(mapBytes),uint32_t(total),Header);
  uint32_t at=Header;for(uint32_t i=0;i<count;++i)at=writeValue(p,at,values[i]);finish(p,uint32_t(total));
  return publish(p,uint32_t(total));
 }
 int get(const char*key,void*out,uint32_t capacity,uint32_t*size)const{
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
  if(!out)return Invalid;
  if(!ready())return Uncertain;
  if(ticketSerial_==UINT64_MAX)return NoSpace;
  ticket_={++ticketSerial_,revision(),sequence(),crc(bytes_+headerSize(),word(bytes_+104))};ticketBase_=identity();ticketLive_=true;*out=ticket_;return Ok;
 }
 // Call under the native serialization lease. No storage I/O; copy precedes
 // ticket replacement so an allocation failure cannot invalidate a live export.
 int captureSnapshot(Snapshot*out){
  if(!out||out->valid())return Invalid;
  if(!ready())return Uncertain;
  auto*p=allocate(size_);if(!p)return NoMemory;
  std::memcpy(p,bytes_,size_);Ticket t{};const int rc=capture(&t);
  if(rc){wipe(p,size_);drop(p);return rc;}
  out->bytes_=p;out->size_=size_;out->alloc_=alloc_;out->ticket_=t;out->owner_=this;return Ok;
 }
 // Explicit durable-export arm. The native owner supplies a fresh nonzero
 // incarnation only for the v1 upgrade, never SD lineage or clock-derived IDs.
 // Target digest/map identity comes from trusted transport. Arm before SD I/O.
 int prepareExport(const Snapshot&snapshot,const Identity&target,const uint8_t*newIncarnation,ExportMarker*out){
  if(!out)return Invalid;
  if(!ready())return Uncertain;
  if(exportPrepared())return Busy;
  if(!snapshot.valid()||snapshot.owner_!=this||!ticketLive_||!sameTicket(snapshot.ticket_,ticket_)||
     snapshot.size_!=size_||snapshot.ticket_.revision!=revision()||snapshot.ticket_.through!=sequence()||
     !sameIdentity(identity(),ticketBase_)||std::memcmp(snapshot.bytes_,bytes_,size_))return Stale;
  if(!validIdentity(target)||!target.known)return Invalid;
  const Identity base=identity();
  if(!base.known||base.generation==UINT64_MAX||target.generation!=base.generation+1||std::memcmp(base.lineage,target.lineage,16))return Conflict;
  if(headerSize()==Header){if(!newIncarnation||zero(newIncarnation,16))return Invalid;}
  else if(newIncarnation&&std::memcmp(newIncarnation,bytes_+112,16))return Conflict;
  if(revision()==UINT64_MAX)return NoSpace;
  const uint64_t total=uint64_t(size_)+HeaderV2-headerSize();if(!fits(total))return NoSpace;
  const uint32_t mapBytes=word(bytes_+104),groups=size_-headerSize()-mapBytes-4;
  uint32_t at=headerSize()+mapBytes;uint64_t last=wide(bytes_+32);
  for(uint32_t i=0;i<pendingCount();++i){last=wide(bytes_+at);at+=groupSize(bytes_+at);}
  if(last!=sequence())return Conflict;
  auto*p=allocate(uint32_t(total));if(!p)return NoMemory;
  makeHeader(p,revision()+1,sequence(),wide(bytes_+32),base,word(bytes_+96),pendingCount(),mapBytes,uint32_t(total),HeaderV2);
  if(headerSize()==Header)std::memcpy(p+112,newIncarnation,16);
  auto*m=p+Header;std::memcpy(m,"RTENEX01",8);putWord(m+8,ns_);putWord(m+12,1);std::memcpy(m+16,p+112,16);
  putWide(m+32,revision());putWide(m+40,sequence());putWide(m+48,wide(bytes_+32));putWord(m+56,pendingCount());putWord(m+60,groups);
  putWord(m+64,mapBytes);putWord(m+68,word(bytes_+96));putMarkerIdentity(m+72,base);putMarkerIdentity(m+136,target);
  putWord(m+200,crc(bytes_+headerSize()+mapBytes,groups));putWord(m+204,crc(bytes_+headerSize(),mapBytes));putWord(m+252,crc(m,252));
  // Includes every original group byte, not just IDs below a claimed frontier.
  std::memcpy(p+HeaderV2,bytes_+headerSize(),size_-headerSize()-4);finish(p,uint32_t(total));
  const int rc=publish(p,uint32_t(total));if(rc==Ok){ticketLive_=false;std::memcpy(out->bytes_,bytes_+Header,MarkerBytes);}return rc;
 }
 int preparedExport(ExportMarker*out)const{
  if(!out)return Invalid;
  if(!ready())return Uncertain;
  if(!exportPrepared())return Missing;
  std::memcpy(out->bytes_,bytes_+Header,MarkerBytes);return Ok;
 }
 // Caller holds fresh complete checked SD evidence for this exact target and
 // the exclusive same-medium lease/quiescence through this native transition.
 // Marker/root equality confers native authority; an inactive bank never does.
 int acknowledgeExport(const ExportMarker&expected,const Identity&verifiedTarget){
  if(!ready())return Uncertain;
  if(!selectedMarker(expected))return Stale;
  if(!validIdentity(verifiedTarget)||!verifiedTarget.known)return Invalid;
  if(!sameIdentity(expected.target(),verifiedTarget))return Conflict;
  if(revision()==UINT64_MAX)return NoSpace;
  const uint32_t mapBytes=word(bytes_+104),removed=expected.prefixBytes(),total=size_-removed;
  auto*p=allocate(total);if(!p)return NoMemory;
  makeHeader(p,revision()+1,sequence(),expected.through(),verifiedTarget,word(bytes_+96),pendingCount()-expected.prefixCount(),mapBytes,total);
  std::memset(p+Header,0,MarkerBytes);std::memcpy(p+HeaderV2,bytes_+HeaderV2,mapBytes);
  const uint32_t groupsAt=HeaderV2+mapBytes;
  std::memcpy(p+groupsAt,bytes_+groupsAt+removed,size_-groupsAt-removed-4);finish(p,total);
  const int rc=publish(p,total);if(rc==Ok)ticketLive_=false;return rc;
 }
 // Caller freshly verified the exact previous base under the same held lease.
 // Retiring an unpublished attempt never acknowledges or rewrites local edits.
 int retireExport(const ExportMarker&expected,const Identity&verifiedBase){
  if(!ready())return Uncertain;
  if(!selectedMarker(expected))return Stale;
  if(!validIdentity(verifiedBase)||!verifiedBase.known)return Invalid;
  if(!sameIdentity(expected.base(),verifiedBase))return Conflict;
  if(revision()==UINT64_MAX)return NoSpace;
  auto*p=allocate(size_);if(!p)return NoMemory;
  std::memcpy(p,bytes_,size_);putWide(p+16,revision()+1);std::memset(p+Header,0,MarkerBytes);finish(p,size_);
  const int rc=publish(p,size_);if(rc==Ok)ticketLive_=false;return rc;
 }
 int acknowledge(const Ticket&t,const Identity&confirmed){
  if(!ready())return Uncertain;
  if(exportPrepared())return Busy;
  if(!ticketLive_||!sameTicket(t,ticket_)||!sameIdentity(identity(),ticketBase_))return Stale;
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
  if(!ready())return Uncertain;
  if(exportPrepared())return Busy;
  if(expectedRevision!=revision())return Stale;
  if(!base.known||!validIdentity(base)||!validValues(values,count))return Invalid;
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
  if(size)*size=0;
  if(!id||!size||(!out&&cap))return Invalid;
  if(!ready())return Uncertain;
  uint32_t at=headerSize()+word(bytes_+104);for(uint32_t i=0;i<pendingCount();++i){const uint32_t n=groupSize(bytes_+at);
   if(wide(bytes_+at)>after){*id=wide(bytes_+at);*size=word(bytes_+at+24);if(cap<*size)return BufferSmall;std::memcpy(out,bytes_+at+GroupHeader+word(bytes_+at+28),*size);return Ok;}at+=n;}return Missing;
 }
 private:
 uint32_t ns_;Backend b_;Limits limits_;Allocator alloc_;uint8_t*bytes_=nullptr;uint32_t size_=0;uint64_t object_=0;bool uncertain_=false;
 Ticket ticket_{};Identity ticketBase_{};uint64_t ticketSerial_=0;bool ticketLive_=false;
 static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 static void putWord(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
 static uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
 static void putWide(uint8_t*p,uint64_t v){putWord(p,uint32_t(v));putWord(p+4,uint32_t(v>>32));}
 static uint32_t crc(const void*v,uint32_t n){auto*p=static_cast<const uint8_t*>(v);uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
 static uint32_t chunk(uint32_t n){return n>4096?4096:n;}
 static bool validKey(const char*s){if(!s||!*s)return false;for(unsigned i=0;i<16;++i){const char c=s[i];if(!c)return true;if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return false;}return false;}
 static bool zero(const uint8_t*p,uint32_t n){while(n--)if(*p++)return false;return true;}
 static bool validIdentity(const Identity&i){return i.known?!zero(i.lineage,16):!i.generation&&zero(i.lineage,16)&&zero(i.digest,32);}
 static bool sameIdentity(const Identity&a,const Identity&b){return a.known==b.known&&a.generation==b.generation&&!std::memcmp(a.lineage,b.lineage,16)&&!std::memcmp(a.digest,b.digest,32);}
 static bool sameTicket(const Ticket&a,const Ticket&b){return a.serial==b.serial&&a.revision==b.revision&&a.through==b.through&&a.mapCrc==b.mapCrc;}
 static Identity identityAt(const uint8_t*p){Identity i{};i.known=word(p+12)!=0;i.generation=wide(p+40);std::memcpy(i.lineage,p+48,16);std::memcpy(i.digest,p+64,32);return i;}
 static uint32_t imageHeader(const uint8_t*p){return !std::memcmp(p,"RTENR01",8)?Header:(!std::memcmp(p,"RTENR02",8)?HeaderV2:0);}
 static Identity markerIdentity(const uint8_t*p){Identity i{};i.known=word(p)!=0;i.generation=wide(p+8);std::memcpy(i.lineage,p+16,16);std::memcpy(i.digest,p+32,32);return i;}
 static void putMarkerIdentity(uint8_t*p,const Identity&i){putWord(p,i.known?1:0);putWord(p+4,0);putWide(p+8,i.generation);std::memcpy(p+16,i.lineage,16);std::memcpy(p+32,i.digest,32);}
 static bool validMarkerEncoding(const uint8_t*m){
  if(std::memcmp(m,"RTENEX01",8)||!word(m+8)||word(m+12)!=1||zero(m+16,16)||!wide(m+32)||wide(m+48)>wide(m+40)||
     !zero(m+208,44)||word(m+252)!=crc(m,252)||word(m+72)!=1||word(m+136)!=1||!zero(m+76,4)||!zero(m+140,4))return false;
  const Identity base=markerIdentity(m+72),target=markerIdentity(m+136);
  if(!validIdentity(base)||!validIdentity(target)||base.generation==UINT64_MAX||target.generation!=base.generation+1||std::memcmp(base.lineage,target.lineage,16))return false;
  if((!word(m+56))!=(!word(m+60))||uint64_t(word(m+56))*(GroupHeader+25)>word(m+60)||
     (!word(m+68))!=(!word(m+64))||uint64_t(word(m+68))*21>word(m+64))return false;
  return true;
 }
 // Image/map/groups have already passed bounds/encoding validation here.
 bool validSelectedMarker(const uint8_t*p,uint32_t n)const{
  const auto*m=p+Header;
  if(!validMarkerEncoding(m)||word(m+8)!=ns_||std::memcmp(m+16,p+112,16)||wide(m+32)>=wide(p+16)||
     wide(m+40)>wide(p+24)||wide(m+48)!=wide(p+32)||!sameIdentity(markerIdentity(m+72),identityAt(p))||
     wide(p+16)-wide(m+32)-1!=wide(p+24)-wide(m+40)||word(m+56)>word(p+100)||
     !fits(uint64_t(HeaderV2)+word(m+64)+word(m+60)+4))return false;
  const uint32_t start=HeaderV2+word(p+104);uint32_t at=start;uint64_t last=wide(p+32);
  for(uint32_t i=0;i<word(m+56);++i){if(wide(p+at)>wide(m+40))return false;last=wide(p+at);at+=groupSize(p+at);}
  if(at-start!=word(m+60)||last!=wide(m+40)||word(m+200)!=crc(p+start,at-start)||
     (at<n-4&&wide(p+at)<=wide(m+40)))return false;
  // Only apply may change an armed map. At the capture frontier it is still exact.
  return wide(p+24)!=wide(m+40)||(word(m+64)==word(p+104)&&word(m+68)==word(p+96)&&word(m+204)==crc(p+HeaderV2,word(p+104)));
 }
 bool selectedMarker(const ExportMarker&m)const{return exportPrepared()&&!std::memcmp(m.bytes_,bytes_+Header,MarkerBytes)&&validSelectedMarker(bytes_,size_);}
 static bool validValues(const Value*v,uint32_t n){if(n&&!v)return false;for(uint32_t i=0;i<n;++i){if(!validKey(v[i].key)||!v[i].data||!v[i].size||v[i].size>ValueMax)return false;for(uint32_t j=0;j<i;++j)if(!std::strcmp(v[i].key,v[j].key))return false;}return true;}
 static bool validMutations(const Mutation*m,uint32_t n){if(!n||!m)return false;for(uint32_t i=0;i<n;++i){if(!validKey(m[i].key)||(m[i].kind!=Put&&m[i].kind!=Delete)||(m[i].kind==Put&&(!m[i].data||!m[i].size||m[i].size>ValueMax))||(m[i].kind==Delete&&(m[i].size||m[i].data)))return false;for(uint32_t j=0;j<i;++j)if(!std::strcmp(m[i].key,m[j].key))return false;}return true;}
 bool validConfiguration()const{return ns_&&limits_.imageBytes>=Header+4&&limits_.groupBytes>=GroupHeader+25&&limits_.intentBytes&&limits_.intentBytes<=limits_.groupBytes&&alloc_.allocate&&alloc_.release&&b_.readRoot&&b_.reserve&&b_.write&&b_.read&&b_.seal&&b_.replaceRoot;}
 bool fits(uint64_t n)const{return n<=UINT32_MAX&&n<=limits_.imageBytes&&n>=Header+4;}
 uint8_t*allocate(uint32_t n){return alloc_.allocate?static_cast<uint8_t*>(alloc_.allocate(alloc_.context,n)):nullptr;}
 void drop(void*p){if(p&&alloc_.release)alloc_.release(alloc_.context,p);}
 bool has(const char*k)const{uint32_t at=headerSize();for(uint32_t i=0;i<word(bytes_+96);++i){if(!std::strcmp(reinterpret_cast<const char*>(bytes_+at),k))return true;at+=20+word(bytes_+at+16);}return false;}
 static const Mutation*mutation(const Mutation*m,uint32_t n,const char*k){for(uint32_t i=0;i<n;++i)if(!std::strcmp(m[i].key,k))return m+i;return nullptr;}
 static uint32_t writeValue(uint8_t*p,uint32_t at,const Value&v){std::memset(p+at,0,20);std::strcpy(reinterpret_cast<char*>(p+at),v.key);putWord(p+at+16,v.size);std::memcpy(p+at+20,v.data,v.size);return at+20+v.size;}
 void makeHeader(uint8_t*p,uint64_t rev,uint64_t seq,uint64_t ack,const Identity&base,uint32_t entries,uint32_t groups,uint32_t mapBytes,uint32_t total,uint32_t header=0){
  if(!header)header=headerSize();
  std::memset(p,0,header);std::memcpy(p,header==HeaderV2?"RTENR02":"RTENR01",7);
  if(header==HeaderV2&&bytes_&&imageHeader(bytes_)==HeaderV2)std::memcpy(p+112,bytes_+112,HeaderV2-112);
  putWord(p+8,ns_);putWord(p+12,base.known?1:0);putWide(p+16,rev);putWide(p+24,seq);putWide(p+32,ack);putWide(p+40,base.generation);std::memcpy(p+48,base.lineage,16);std::memcpy(p+64,base.digest,32);putWord(p+96,entries);putWord(p+100,groups);putWord(p+104,mapBytes);putWord(p+108,total);}
 static void finish(uint8_t*p,uint32_t n){putWord(p+n-4,crc(p,n-4));}
 static uint32_t groupSize(const uint8_t*p){return GroupHeader+word(p+24)+word(p+28);}
 bool validate(const uint8_t*p,uint32_t n)const{
  if(!fits(n))return false;
  const uint32_t header=imageHeader(p);
  if(!header||n<header+4||word(p+8)!=ns_||word(p+12)>1||!wide(p+16)||wide(p+32)>wide(p+24)||word(p+108)!=n||(header==Header?!zero(p+112,16):zero(p+112,16))||word(p+n-4)!=crc(p,n-4)||!validIdentity(identityAt(p))||word(p+104)>n-header-4)return false;
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
