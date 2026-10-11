#pragma once
// Disconnected native-only receiver. No app schema, SDK ABI or live routing.
#include <experimental/native_replica/NativeReplicaCore.h>
namespace RiscExperimental {
class NativeRebaseCandidate {
 public:
 using Core=NativeReplicaCore;
 // nativeImageBytes must not exceed the bound core/backend image limit.
 // A stricter receiver budget is permitted; core admission remains authoritative.
 // These are byte budgets, not key/profile/operation-count ceilings.
 struct Limits {uint32_t nativeImageBytes=65512,pendingBytes=65512,batchBytes=4096,intentBytes=1024;};
 struct Remote {
  uint32_t namespaceId,count,mapBytes;
  const uint8_t*map;
  Core::Identity identity;
 };
 struct Batch {
  uint32_t namespaceId=0;
  uint64_t viewRevision=0,pendingFrontier=0,originalIntentId=0;
  const void*originalIntent=nullptr;uint32_t intentBytes=0;
  const Core::Mutation*mutations=nullptr;uint32_t count=0;
 };
 // Only trusted native binding code constructs this object for an authorized
 // complete namespace. Caller-supplied Batch/Remote namespaces confer no rights.
 // All calls, core operations, allocator and backend callbacks share one native
 // serialization lease. Rebase ownership excludes in-flight export/ack tickets
 // until finish/abandon; captureSnapshot replaces the core's one live ticket.
 // No callbacks may reenter or retain request pointers.
 NativeRebaseCandidate(Core&core,uint32_t authorizedNamespace,const Limits&limits,const Core::Allocator&allocator)
  :core_(core),namespace_(authorizedNamespace),limits_(limits),alloc_(allocator){}
 ~NativeRebaseCandidate(){clear();}
 NativeRebaseCandidate(const NativeRebaseCandidate&)=delete;
 NativeRebaseCandidate&operator=(const NativeRebaseCandidate&)=delete;
 bool held()const{return state_.p!=nullptr;}
 uint64_t viewRevision()const{return held()?viewRevision_:0;}
 uint64_t frontier()const{return held()?nativeFrontier_:0;}
 uint32_t pendingCount()const{return held()?pendingCount_:0;}
 uint32_t resolvedCount()const{return held()?word(state_.p+8):0;}
 uint32_t count()const{return held()?word(state_.p+4):0;}
 int outcome()const{return outcome_;}
 // Copies a previously checked remote map plus every native pending intent.
 // Remote identity/content verification and the SD lease belong to transport.
 // No root write, acknowledgement or native map replacement occurs here.
 int begin(const Remote&remote){
  if(busy_)return Core::Busy;
  Guard guard(busy_);
  if(outcome_==Core::Retained)return Core::Retained;
  if(held()||core_.exportPrepared())return Core::Busy;
  if(!configuration()||remote.namespaceId!=namespace_)return Core::Invalid;
  if(!validIdentity(remote.identity))return Core::Invalid;
  if(limits_.nativeImageBytes<core_.headerSize()+4)return Core::NoSpace;
  if(remote.mapBytes>limits_.nativeImageBytes-core_.headerSize()-4)return Core::NoSpace;
  if(!validMap(remote))return Core::Invalid;
  if(serial_==UINT64_MAX)return Core::NoSpace;
  Core::Snapshot captured;int rc=core_.captureSnapshot(&captured);if(rc)return rc;
  if(captured.namespaceId()!=namespace_)return Core::Conflict;
  const auto ticket=captured.ticket();
  if(!compatible(captured.base(),remote.identity))return Core::Conflict;
  const uint32_t groups=captured.imageSize()-captured.headerSize()-captured.mapSize()-4;
  if(uint64_t(captured.headerSize())+remote.mapBytes+groups+4>limits_.nativeImageBytes)return Core::NoSpace;
  const uint32_t pendingCount=core_.pendingCount();
  uint64_t after=0,bytes=0;
  for(uint32_t i=0;i<pendingCount;++i){uint64_t id=0;uint32_t size=0;
   rc=core_.pending(after,&id,nullptr,0,&size);
   if(rc!=Core::BufferSmall)return rc==Core::Missing?Core::Stale:rc;
   if(!size||size>limits_.intentBytes||id<=after||id>ticket.through)return Core::Invalid;
   bytes+=12u+uint64_t(size);if(bytes>limits_.pendingBytes)return Core::NoSpace;after=id;
  }
  Owned pending(alloc_);if(bytes&&!pending.make(uint32_t(bytes)))return Core::NoMemory;
  after=0;uint32_t at=0;
  for(uint32_t i=0;i<pendingCount;++i){uint64_t id=0;uint32_t size=0;
   rc=core_.pending(after,&id,nullptr,0,&size);
   if(rc!=Core::BufferSmall||id<=after||id>ticket.through||!size||size>limits_.intentBytes||at>pending.n||pending.n-at<12||size>pending.n-at-12)return Core::Stale;
   putWide(pending.p+at,id);putWord(pending.p+at+8,size);
   uint64_t copiedId=0;uint32_t copiedSize=0;
   rc=core_.pending(after,&copiedId,pending.p+at+12,size,&copiedSize);
   if(rc||copiedId!=id||copiedSize!=size)return Core::Stale;
   at+=12+size;after=id;
  }
  if(at!=pending.n)return Core::Stale;
  uint64_t extraId=0;uint32_t extraSize=0;
  if(core_.pending(after,&extraId,nullptr,0,&extraSize)!=Core::Missing)return Core::Stale;
  Owned state(alloc_);if(!state.make(StateHeader+remote.mapBytes))return Core::NoMemory;
  putWord(state.p,remote.mapBytes);putWord(state.p+4,remote.count);putWord(state.p+8,0);putWord(state.p+12,0);
  if(remote.mapBytes)std::memcpy(state.p+StateHeader,remote.map,remote.mapBytes);
  if(!core_.ready())return Core::Uncertain;
  if(core_.revision()!=ticket.revision||core_.sequence()!=ticket.through||core_.pendingCount()!=pendingCount)return Core::Stale;
  pending_.swap(pending);state_.swap(state);pendingCount_=pendingCount;pendingAt_=0;
  nativeRevision_=ticket.revision;nativeFrontier_=ticket.through;groupBytes_=groups;headerBytes_=captured.headerSize();remote_=remote.identity;
  viewRevision_=++serial_;outcome_=Core::Busy;return Core::Ok;
 }
 // Explicit view revision makes any earlier read capability stale after a
 // successful batch, abandonment or replacement of this candidate.
 int get(uint64_t expectedView,const char*key,void*out,uint32_t capacity,uint32_t*size)const{
  if(size)*size=0;
  if(!size||!validKey(key)||(!out&&capacity))return Core::Invalid;
  if(busy_)return Core::Busy;
  int rc=current(expectedView,nativeFrontier_);if(rc)return rc;
  const uint8_t*row=find(key);if(!row)return Core::Missing;
  *size=word(row+16);if(capacity<*size)return Core::BufferSmall;
  std::memcpy(out,row+20,*size);return Core::Ok;
 }
 // Enumerate copied intents only. An ID merely below frontier is not membership.
 int pending(uint64_t expectedView,uint64_t after,uint64_t*id,void*out,uint32_t capacity,uint32_t*size)const{
  if(size)*size=0;
  if(!id||!size||(!out&&capacity))return Core::Invalid;
  if(busy_)return Core::Busy;
  int rc=current(expectedView,nativeFrontier_);if(rc)return rc;
  for(uint32_t at=0;at<pending_.n;at+=12+word(pending_.p+at+8)){
   const auto*p=pending_.p+at;if(wide(p)>after){*id=wide(p);*size=word(p+8);if(capacity<*size)return Core::BufferSmall;std::memcpy(out,p+12,*size);return Core::Ok;}
  }
  return Core::Missing;
 }
 // The success marker and full changed map swap together in RAM. Even a
 // zero-mutation resolution advances the marker/revision, not the native root.
 int stage(const Batch&batch){
  if(busy_)return Core::Busy;
  Guard guard(busy_);
  if(outcome_==Core::Retained)return Core::Retained;
  if(batch.namespaceId!=namespace_)return Core::Conflict;
  int rc=current(batch.viewRevision,batch.pendingFrontier);if(rc)return rc;
  if(resolvedCount()>=pendingCount_||pendingAt_>=pending_.n)return Core::Conflict;
  const auto*original=pending_.p+pendingAt_;const uint32_t intentBytes=word(original+8);
  if(batch.originalIntentId!=wide(original)||!batch.originalIntent||batch.intentBytes!=intentBytes||std::memcmp(batch.originalIntent,original+12,intentBytes))return Core::Conflict;
  if(32u+uint64_t(batch.intentBytes)>limits_.batchBytes||batch.count>(limits_.batchBytes-32u-batch.intentBytes)/24u)return Core::NoSpace;
  if(!validBatch(batch))return Core::Invalid;
  uint64_t batchBytes=32u+uint64_t(batch.intentBytes);
  for(uint32_t i=0;i<batch.count;++i)batchBytes+=24u+uint64_t(batch.mutations[i].size);
  if(batchBytes>limits_.batchBytes)return Core::NoSpace;
  if(serial_==UINT64_MAX)return Core::NoSpace;
  uint64_t mapBytes=0;uint32_t entries=0;
  for(uint32_t at=StateHeader;at<state_.n;){const auto*p=state_.p+at;const uint32_t n=word(p+16);const auto*m=mutation(batch,reinterpret_cast<const char*>(p));
   if(!m){mapBytes+=20u+uint64_t(n);++entries;}else if(m->kind==Core::Put){mapBytes+=20u+uint64_t(m->size);++entries;}at+=20+n;
  }
  for(uint32_t i=0;i<batch.count;++i)if(batch.mutations[i].kind==Core::Put&&!find(batch.mutations[i].key)){mapBytes+=20u+uint64_t(batch.mutations[i].size);++entries;}
  if(uint64_t(headerBytes_)+mapBytes+groupBytes_+4>limits_.nativeImageBytes)return Core::NoSpace;
  Owned next(alloc_);if(!next.make(StateHeader+uint32_t(mapBytes)))return Core::NoMemory;
  putWord(next.p,uint32_t(mapBytes));putWord(next.p+4,entries);putWord(next.p+8,resolvedCount()+1);putWord(next.p+12,0);
  uint32_t out=StateHeader;
  for(uint32_t at=StateHeader;at<state_.n;){const auto*p=state_.p+at;const uint32_t n=word(p+16);const auto*m=mutation(batch,reinterpret_cast<const char*>(p));
   if(!m){std::memcpy(next.p+out,p,20+n);out+=20+n;}else if(m->kind==Core::Put)out=write(next.p,out,*m);at+=20+n;
  }
  for(uint32_t i=0;i<batch.count;++i)if(batch.mutations[i].kind==Core::Put&&!find(batch.mutations[i].key))out=write(next.p,out,batch.mutations[i]);
  if(out!=next.n)return Core::Invalid;
  rc=current(batch.viewRevision,batch.pendingFrontier);if(rc)return rc;
  state_.swap(next);pendingAt_+=12+intentBytes;viewRevision_=++serial_;return Core::Ok;
 }
 // Only this call can publish. Core preserves ALL original groups and performs
 // its own revision/ordered-ID checks and old-or-new root publication.
 int finish(uint64_t expectedView,uint64_t expectedFrontier){
  if(busy_)return Core::Busy;
  Guard guard(busy_);
  int rc=current(expectedView,expectedFrontier);if(rc)return rc;
  if(resolvedCount()!=pendingCount_||pendingAt_!=pending_.n)return Core::Conflict;
  const uint64_t valueBytes=uint64_t(count())*sizeof(Core::Value),idBytes=uint64_t(pendingCount_)*sizeof(uint64_t);
  if(valueBytes>UINT32_MAX||idBytes>UINT32_MAX)return Core::NoSpace;
  Owned values(alloc_),ids(alloc_);
  if(valueBytes&&!values.make(uint32_t(valueBytes)))return Core::NoMemory;
  if(idBytes&&!ids.make(uint32_t(idBytes)))return Core::NoMemory;
  auto*rows=reinterpret_cast<Core::Value*>(values.p);auto*resolved=reinterpret_cast<uint64_t*>(ids.p);
  uint32_t at=StateHeader;
  for(uint32_t i=0;i<count();++i){const auto*p=state_.p+at;rows[i]={reinterpret_cast<const char*>(p),p+20,word(p+16)};at+=20+word(p+16);}
  at=0;for(uint32_t i=0;i<pendingCount_;++i){resolved[i]=wide(pending_.p+at);at+=12+word(pending_.p+at+8);}
  rc=current(expectedView,expectedFrontier);if(rc)return rc;
  rc=core_.installResolved(nativeRevision_,rows,count(),remote_,resolved,pendingCount_);
  // An ambiguous root switch cannot be replayed or claimed cancelled. Drop all
  // volatile resolution evidence; caller must reload and reacquire the remote.
  // Retained is terminal for this receiver lifetime. Independent backend/job
  // resource custody must remain alive; clearing candidate RAM cannot clear it.
  if(rc==Core::Ok||rc==Core::Uncertain||rc==Core::Retained){clear();outcome_=rc;}
  return rc;
 }
 int abandon(){
  if(busy_)return Core::Busy;
  if(!held())return outcome_;
  Guard guard(busy_);clear();outcome_=Core::Missing;return Core::Ok;
 }
 private:
 static constexpr uint32_t StateHeader=16;
 struct Guard{bool&busy;explicit Guard(bool&b):busy(b){busy=true;}~Guard(){busy=false;}};
 struct Owned{
  Core::Allocator a{};uint8_t*p=nullptr;uint32_t n=0;
  Owned()=default;explicit Owned(const Core::Allocator&allocator):a(allocator){}
  ~Owned(){reset();}Owned(const Owned&)=delete;Owned&operator=(const Owned&)=delete;
  bool make(uint32_t size){if(!size||!a.allocate)return false;p=static_cast<uint8_t*>(a.allocate(a.context,size));if(p)n=size;return p!=nullptr;}
  void reset(){if(p){Core::wipe(p,n);a.release(a.context,p);}p=nullptr;n=0;}
  void swap(Owned&o){std::swap(a,o.a);std::swap(p,o.p);std::swap(n,o.n);}
 };
 Core&core_;uint32_t namespace_;Limits limits_;Core::Allocator alloc_;Owned state_,pending_;
 uint64_t serial_=0,viewRevision_=0,nativeRevision_=0,nativeFrontier_=0;
 uint32_t pendingCount_=0,pendingAt_=0,groupBytes_=0,headerBytes_=Core::Header;Core::Identity remote_{};
 bool busy_=false;int outcome_=Core::Missing;
 static uint32_t word(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
 static uint64_t wide(const uint8_t*p){return word(p)|(uint64_t(word(p+4))<<32);}
 static void putWord(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(i*8));}
 static void putWide(uint8_t*p,uint64_t v){putWord(p,uint32_t(v));putWord(p+4,uint32_t(v>>32));}
 static bool validKey(const char*p){if(!p||!*p)return false;for(unsigned i=0;i<16;++i){const char c=p[i];if(!c)return true;if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'))return false;}return false;}
 static bool validIdentity(const Core::Identity&i){if(!i.known)return false;for(auto b:i.lineage)if(b)return true;return false;}
 static bool compatible(const Core::Identity&native,const Core::Identity&remote){return !native.known||(!std::memcmp(native.lineage,remote.lineage,16)&&remote.generation>=native.generation&&(remote.generation!=native.generation||!std::memcmp(native.digest,remote.digest,32)));}
 bool configuration()const{return namespace_&&limits_.nativeImageBytes>=Core::Header+4&&limits_.nativeImageBytes<=UINT32_MAX-StateHeader&&limits_.pendingBytes&&limits_.intentBytes&&limits_.batchBytes>=32&&alloc_.allocate&&alloc_.release;}
 static bool validMap(const Remote&r){
  if((r.mapBytes&&!r.map)||r.count>r.mapBytes/21)return false;
  uint32_t at=0;
  for(uint32_t i=0;i<r.count;++i){if(at>r.mapBytes||r.mapBytes-at<20||!validKey(reinterpret_cast<const char*>(r.map+at)))return false;
   const uint32_t size=word(r.map+at+16);if(!size||size>Core::ValueMax||size>r.mapBytes-at-20)return false;
   for(uint32_t prior=0;prior<at;prior+=20+word(r.map+prior+16))if(!std::strcmp(reinterpret_cast<const char*>(r.map+prior),reinterpret_cast<const char*>(r.map+at)))return false;
   at+=20+size;
  }
  return at==r.mapBytes;
 }
 static bool validBatch(const Batch&b){
  if(b.count&&!b.mutations)return false;
  // Every mutation has at least 24 bytes in the admitted canonical wire form.
  if(b.count>UINT32_MAX/24)return false;
  for(uint32_t i=0;i<b.count;++i){const auto&m=b.mutations[i];if(!validKey(m.key)||(m.kind!=Core::Put&&m.kind!=Core::Delete)||(m.kind==Core::Put&&(!m.data||!m.size||m.size>Core::ValueMax))||(m.kind==Core::Delete&&(m.data||m.size)))return false;
   for(uint32_t j=0;j<i;++j)if(!std::strcmp(m.key,b.mutations[j].key))return false;
  }
  return true;
 }
 int current(uint64_t view,uint64_t frontier)const{
  if(outcome_==Core::Retained)return Core::Retained;
  if(!held()||view!=viewRevision_||frontier!=nativeFrontier_)return Core::Stale;
  if(!core_.ready())return Core::Uncertain;
  if(core_.revision()!=nativeRevision_||core_.sequence()!=nativeFrontier_||core_.pendingCount()!=pendingCount_)return Core::Stale;
  return Core::Ok;
 }
 const uint8_t*find(const char*key)const{for(uint32_t at=StateHeader;at<state_.n;at+=20+word(state_.p+at+16))if(!std::strcmp(reinterpret_cast<const char*>(state_.p+at),key))return state_.p+at;return nullptr;}
 static const Core::Mutation*mutation(const Batch&b,const char*key){for(uint32_t i=0;i<b.count;++i)if(!std::strcmp(key,b.mutations[i].key))return b.mutations+i;return nullptr;}
 static uint32_t write(uint8_t*p,uint32_t at,const Core::Mutation&m){std::memset(p+at,0,20);std::strcpy(reinterpret_cast<char*>(p+at),m.key);putWord(p+at+16,m.size);std::memcpy(p+at+20,m.data,m.size);return at+20+m.size;}
 void clear(){state_.reset();pending_.reset();pendingCount_=pendingAt_=groupBytes_=0;headerBytes_=Core::Header;nativeRevision_=nativeFrontier_=viewRevision_=0;Core::wipe(&remote_,sizeof remote_);}
};
}
