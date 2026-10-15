#pragma once
#include "NativeReplicaJob.h"
#include <RiscKeyValueAtomicV1.h>
namespace RiscExperimental {
// Disconnected native-only materializer. Broker admission must supply a shared
// namespace gate respected by EVERY user of this core (including SD jobs).
// The native gate is held from submission through terminal owner collection.
// No application callbacks, native I/O or borrowed app pointers reach workerStep.
// Fence the scheduler before destruction; keep this object constructed between
// operations. Cancellation never invokes StorageWorker::cancel.
class NativeAtomicKeyValue final {
 public:
 using C=NativeReplicaCore;using Result=NativeReplicaJob::Result;
 static constexpr uint32_t MaxMutations=16,MaxIntent=1024,MaxGroup=4096;
 struct Guard {
  void*context;
  bool(*owner)(void*);bool(*worker)(void*);
  bool(*acquire)(void*);bool(*release)(void*);
  // Worker-only terminal cleanup/check of the bound native store.
  int(*finish)(void*);
  // Optional shared native namespace retention, never an app callback.
  bool(*retained)(void*)=nullptr;
 };
 NativeAtomicKeyValue(C&core,const Guard&guard):core_(core),guard_(guard),job_(execute,this,core.payloadAllocator()){}
 NativeAtomicKeyValue(const NativeAtomicKeyValue&)=delete;NativeAtomicKeyValue&operator=(const NativeAtomicKeyValue&)=delete;
 risc_key_value_atomic_v1 api(){if(!controlInternal())return {};return {1,sizeof(risc_key_value_atomic_v1),this,
  [](void*p,risc_key_value_atomic_state_v1*s)->int32_t{return static_cast<NativeAtomicKeyValue*>(p)->state(s);},
  [](void*p,uint64_t r,const char*k,void*b,uint32_t n,uint32_t*z)->int32_t{return static_cast<NativeAtomicKeyValue*>(p)->get(r,k,b,n,z);},
  [](void*p,uint64_t r,const risc_key_value_atomic_mutation_v1*m,uint32_t n,const void*i,uint32_t z,uint64_t*t)->int32_t{return static_cast<NativeAtomicKeyValue*>(p)->submit(r,m,n,i,z,t);},
  [](void*p,uint64_t t,risc_key_value_atomic_receipt_v1*r)->int32_t{return static_cast<NativeAtomicKeyValue*>(p)->collect(t,r);},
  [](void*p,uint64_t t)->int32_t{return static_cast<NativeAtomicKeyValue*>(p)->cancel(t);}};}
 int state(risc_key_value_atomic_state_v1*out){
  if(!controlInternal())return RISC_ATOMIC_RETAINED;
  if(!out||out->struct_size<sizeof(*out)||!owner())return RISC_ATOMIC_INVALID;
  int rc=lock();if(rc)return rc;
  if(!core_.ready())rc=RISC_ATOMIC_UNCERTAIN;
  else *out={sizeof(*out),RISC_KEY_VALUE_ATOMIC_NATIVE_DURABLE|RISC_KEY_VALUE_ATOMIC_COPIED_ASYNC,core_.namespaceId(),MaxMutations,C::ValueMax,MaxGroup,MaxIntent,0,core_.revision()};
  return unlock(rc);
 }
 int get(uint64_t revision,const char*key,void*out,uint32_t cap,uint32_t*size){
  if(!controlInternal())return RISC_ATOMIC_RETAINED;
  if(size)*size=0;
  if(!owner()||!size||!revision||!key||(!out&&cap))return RISC_ATOMIC_INVALID;
  int rc=lock();if(rc)return rc;
  rc=!core_.ready()?RISC_ATOMIC_UNCERTAIN:revision!=core_.revision()?RISC_ATOMIC_STALE:translate(core_.get(key,out,cap,size));
  return unlock(rc);
 }
 int submit(uint64_t revision,const risc_key_value_atomic_mutation_v1*changes,uint32_t count,const void*intent,uint32_t intentSize,uint64_t*token){
  if(token)*token=0;
  if(!controlInternal())return RISC_ATOMIC_RETAINED;
  if(!owner()||!token||!revision||(count&&!changes)||count>MaxMutations||!intent||!intentSize||intentSize>MaxIntent)return RISC_ATOMIC_INVALID;
  if(nativeRetained()){retained_=true;return RISC_ATOMIC_RETAINED;}
  if(token_||!job_.exitSafe())return RISC_ATOMIC_BUSY;
  uint8_t payload[NativeReplicaJob::PayloadMax]{};Wipe wipe{payload,sizeof payload};
  uint32_t total=16;put64(payload,revision);put32(payload+8,count);put32(payload+12,intentSize);
  for(uint32_t i=0;i<count;++i){
   const auto&m=changes[i];uint32_t length=0;if(!m.key)return RISC_ATOMIC_INVALID;
   while(length<16&&m.key[length]){const char c=m.key[length];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'))return RISC_ATOMIC_INVALID;++length;}
   if(!length||length==16||(m.kind!=RISC_ATOMIC_PUT&&m.kind!=RISC_ATOMIC_DELETE)||(m.kind==RISC_ATOMIC_PUT&&(!m.value||!m.size||m.size>C::ValueMax))||(m.kind==RISC_ATOMIC_DELETE&&(m.value||m.size)))return RISC_ATOMIC_INVALID;
   for(uint32_t n=0;n<i;++n)if(!std::strcmp(m.key,changes[n].key))return RISC_ATOMIC_INVALID;
   if(uint64_t(total)+24+m.size+intentSize>sizeof payload||uint64_t(total)-16+24+m.size+intentSize+C::GroupHeader>MaxGroup)return RISC_ATOMIC_CAPACITY;
   std::memcpy(payload+total,m.key,length+1);put32(payload+total+16,m.kind);put32(payload+total+20,m.size);if(m.size)std::memcpy(payload+total+24,m.value,m.size);total+=24+m.size;
  }
  if(uint64_t(total)+intentSize>sizeof payload||uint64_t(total)-16+intentSize+C::GroupHeader>MaxGroup)return RISC_ATOMIC_CAPACITY;
  std::memcpy(payload+total,intent,intentSize);total+=intentSize;
  int rc=lock();if(rc)return rc;
  if(!core_.ready())return unlock(RISC_ATOMIC_UNCERTAIN);
  if(revision!=core_.revision())return unlock(RISC_ATOMIC_STALE);
  phase_.store(Phase::Queued,std::memory_order_release);operation_=0;wasCancelled_=false;
  uint64_t id=0;rc=job_.begin(1,payload,total,&id);
  if(rc!=RISC_STORAGE_WORK_OK){phase_.store(Phase::Idle);return unlock(rc==NativeReplicaJob::NoMemory?RISC_ATOMIC_CAPACITY:rc==RISC_STORAGE_WORK_BUSY?RISC_ATOMIC_BUSY:RISC_ATOMIC_RETAINED);}
  token_=id;*token=id;return RISC_ATOMIC_OK;
 }
 int cancel(uint64_t token){
  if(!controlInternal())return RISC_ATOMIC_RETAINED;
  if(!owner()||!token||token!=token_)return RISC_ATOMIC_INVALID;
  if(nativeRetained()){retained_=true;return RISC_ATOMIC_RETAINED;}
  Phase expected=Phase::Queued;phase_.compare_exchange_strong(expected,Phase::Revoked,std::memory_order_acq_rel);
  return RISC_ATOMIC_PENDING;
 }
 int collect(uint64_t token,risc_key_value_atomic_receipt_v1*out){
  if(!controlInternal())return RISC_ATOMIC_RETAINED;
  if(!owner()||!out||out->struct_size<sizeof(*out)||!token||token!=token_)return RISC_ATOMIC_INVALID;
  if(nativeRetained()){retained_=true;return RISC_ATOMIC_RETAINED;}
  Result result{};int rc=job_.collect(token,&result);
  if(rc==RISC_STORAGE_WORK_PENDING)return RISC_ATOMIC_PENDING;
  if(rc!=RISC_STORAGE_WORK_OK||phase_.load(std::memory_order_acquire)!=Phase::Complete){retained_=true;return RISC_ATOMIC_RETAINED;}
  if(!wasCancelled_&&translate(result.status)==RISC_ATOMIC_RETAINED){retained_=true;return RISC_ATOMIC_RETAINED;}
  risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt),wasCancelled_?RISC_ATOMIC_CANCELLED:translate(result.status),result.revision,operation_};
  if(!guard_.release(guard_.context)){retained_=true;return RISC_ATOMIC_RETAINED;}
  held_=false;token_=0;phase_.store(Phase::Idle,std::memory_order_release);*out=receipt;return RISC_ATOMIC_OK;
 }
 bool workerStep(){return controlInternal()&&guard_.worker&&guard_.worker(guard_.context)&&job_.workerStep();}
 // Native-only immutable admission identity and terminal custody status.
 uint32_t namespaceId()const{return controlInternal()?core_.namespaceId():0;}
 bool retained()const{return nativeRetained();}
 bool exitSafe()const{return controlInternal()&&!held_&&!token_&&!nativeRetained()&&job_.exitSafe();}
 bool scratchCleared()const{return controlInternal()&&job_.scratchCleared();}
 // Native owner-only release predicate. Held/token intentionally remain live
 // until guard.release returns; mailbox completion has already been collected.
 bool nativeReleaseSafe()const{return !nativeRetained()&&job_.exitSafe();}
 bool nativePending()const{return !controlInternal()||job_.pending();}
 bool nativeRetained()const{return !controlInternal()||retained_||(guard_.retained&&guard_.retained(guard_.context));}
 private:
 bool controlInternal()const{
#if RISC_NATIVE_ESP_IDF
  return RiscCpu::NativeInternalAtomics::internal(*this);
#else
  return true;
#endif
 }
 enum class Phase:uint32_t {Idle,Queued,Revoked,Committing,Complete};
 C&core_;Guard guard_;NativeReplicaJob job_;std::atomic<Phase>phase_{Phase::Idle};
 bool held_=false,retained_=false,wasCancelled_=false;uint64_t token_=0,operation_=0;
 struct Wipe{void*p;size_t n;~Wipe(){C::wipe(p,n);}};
 bool owner()const{return controlInternal()&&guard_.owner&&guard_.worker&&guard_.acquire&&guard_.release&&guard_.finish&&guard_.owner(guard_.context);}
 int lock(){if(nativeRetained()){retained_=true;return RISC_ATOMIC_RETAINED;}if(held_||token_||!job_.exitSafe())return RISC_ATOMIC_BUSY;if(!guard_.acquire(guard_.context))return RISC_ATOMIC_BUSY;held_=true;return 0;}
 int unlock(int result){if(!guard_.release(guard_.context)){retained_=true;return RISC_ATOMIC_RETAINED;}held_=false;return result;}
 static void put32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
 static void put64(uint8_t*p,uint64_t v){for(unsigned i=0;i<8;++i)p[i]=uint8_t(v>>(8*i));}
 static uint32_t word(const uint8_t*p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(8*i);return v;}
 static uint64_t wide(const uint8_t*p){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(p[i])<<(8*i);return v;}
 static int translate(int r){switch(r){case C::Ok:return RISC_ATOMIC_OK;case C::Missing:return RISC_ATOMIC_NOT_FOUND;case C::Invalid:return RISC_ATOMIC_INVALID;case C::NoMemory:case C::NoSpace:return RISC_ATOMIC_CAPACITY;case C::Io:return RISC_ATOMIC_IO;case C::Uncertain:return RISC_ATOMIC_UNCERTAIN;case C::Conflict:return RISC_ATOMIC_CONFLICT;case C::Stale:return RISC_ATOMIC_STALE;case C::BufferSmall:return RISC_ATOMIC_BUFFER_SMALL;case C::Busy:return RISC_ATOMIC_BUSY;default:return RISC_ATOMIC_RETAINED;}}
 static Result execute(void*p,uint32_t action,const void*input,uint32_t size){
  auto&self=*static_cast<NativeAtomicKeyValue*>(p);
  if(!self.controlInternal()||!self.guard_.worker(self.guard_.context))return {C::Retained,0,0};
  Phase expected=Phase::Queued;
  if(!self.phase_.compare_exchange_strong(expected,Phase::Committing,std::memory_order_acq_rel)){
   if(expected!=Phase::Revoked)return {C::Retained,0,0};
   self.wasCancelled_=true;self.phase_.store(Phase::Complete,std::memory_order_release);return {C::Ok,0,0};
  }
  int rc=C::Invalid;const auto*bytes=static_cast<const uint8_t*>(input);
  C::Mutation changes[MaxMutations]{};Wipe wipe{changes,sizeof changes};
  if(action==1&&size>=16){
   const uint32_t count=word(bytes+8),intent=word(bytes+12);uint32_t at=16;bool valid=count<=MaxMutations&&intent&&intent<=MaxIntent;
   for(uint32_t i=0;valid&&i<count;++i){
    if(at>size||size-at<24){valid=false;break;}
    const uint32_t n=word(bytes+at+20);if(n>size-at-24){valid=false;break;}
    changes[i]={reinterpret_cast<const char*>(bytes+at),C::Kind(word(bytes+at+16)),n?bytes+at+24:nullptr,n};at+=24+n;
   }
   if(valid&&intent==size-at)rc=self.core_.apply(wide(bytes),changes,count,bytes+at,intent);
  }
  self.operation_=rc==C::Ok?self.core_.sequence():0;
  if(self.guard_.finish(self.guard_.context)!=C::Ok)rc=C::Retained;
  const uint64_t revision=self.core_.revision();const uint32_t pending=self.core_.pendingCount();
  self.phase_.store(Phase::Complete,std::memory_order_release);return {rc,pending,revision};
 }
};
}
