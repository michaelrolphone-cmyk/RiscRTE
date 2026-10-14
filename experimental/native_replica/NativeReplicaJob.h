#pragma once
#include "NativeReplicaCore.h"
#include "OwnedSettingsScratch.h"
#include "runtime/storage/StorageWorker.h"
namespace RiscExperimental {
// Disconnected native copied-worker facade. Owner submit/collect/cancel perform
// only bounded copies/atomics and explicit copied-payload allocation. Filesystem/NVS calls belong to the fixed native
// execute callback, never an app callback. Credentials never enter Worker’s
// non-wiping request/result arrays: only control fields and numeric results do.
struct NativeReplicaJobResult {int32_t status;uint32_t pending;uint64_t revision;};
template<uint32_t PayloadBytes=4096>
class NativeReplicaJobT {
 public:
 static_assert(PayloadBytes<=4096,"bounded native credential scratch");
 static constexpr uint32_t PayloadMax=PayloadBytes;
 // Native-only result; does not extend or alter the published worker ABI.
 static constexpr int NoMemory=-5;
 using Result=NativeReplicaJobResult;
 using Execute=Result(*)(void*,uint32_t,const void*,uint32_t);
 NativeReplicaJobT(Execute fn,void*context,NativeReplicaCore::Allocator allocator=NativeReplicaCore::heap())
  :fn_(fn),context_(context),allocator_(allocator){worker_.bind(run,this);}
 // The native coordinator stays permanent. Other callers must fence the worker
 // before destruction, just as with its in-object mailbox/control storage.
 ~NativeReplicaJobT()=default;
 NativeReplicaJobT(const NativeReplicaJobT&)=delete;
 NativeReplicaJobT&operator=(const NativeReplicaJobT&)=delete;
 int begin(uint32_t action,const void*payload,uint32_t size,uint64_t*out){
  if(out)*out=0;
  if(!out||(!payload&&size)||size>PayloadMax||!fn_)return RISC_STORAGE_WORK_INVALID;
  if(retained_.load())return RISC_STORAGE_WORK_RETAINED;
  if(!worker_.idle())return RISC_STORAGE_WORK_BUSY;
  if(PayloadMax&&!payload_.ensure(allocator_))return NoMemory;
  if(size)std::memcpy(payload_->bytes,payload,size);
  Request q{action,size};int rc=worker_.submit(&q,sizeof q,out);
  if(rc!=RISC_STORAGE_WORK_OK)payload_.reset();
  return rc;
 }
 int collect(uint64_t token,Result*out){
  if(!out)return RISC_STORAGE_WORK_INVALID;
  uint32_t size=0;int32_t status=0;Result result{};const int rc=worker_.collect(token,&result,sizeof result,&size,&status);
  if(rc!=RISC_STORAGE_WORK_OK)return rc;
  if(size!=sizeof result||status!=RISC_STORAGE_WORK_OK){retained_.store(true);return RISC_STORAGE_WORK_RETAINED;}
  *out=result;return RISC_STORAGE_WORK_OK;
 }
 int cancel(uint64_t token){int rc=worker_.cancel(token);if(rc==RISC_STORAGE_WORK_CANCELLED)payload_.reset();return rc;}
 bool workerStep(){return worker_.workerStep();}
 bool exitSafe()const{return worker_.idle()&&!retained_.load();}
 bool pending()const{return !worker_.idle();}
 bool scratchCleared()const{if(!worker_.idle())return false;return payload_.empty();}
 private:
 struct Request {uint32_t action,size;};
 RiscStorage::BasicWorker<sizeof(Request),sizeof(Result)> worker_;Execute fn_;void*context_;std::atomic<bool>retained_{false};
 struct Payload {uint8_t bytes[PayloadMax?PayloadMax:1];};
 NativeReplicaCore::Allocator allocator_;OwnedSettingsScratch<Payload>payload_;
 struct Release {OwnedSettingsScratch<Payload>&payload;~Release(){payload.reset();}};
 static int32_t run(void*p,const void*input,uint32_t size,void*out,uint32_t cap,uint32_t*actual){
  auto&self=*static_cast<NativeReplicaJobT*>(p);Release release{self.payload_};
  if(size!=sizeof(Request)||cap<sizeof(Result))return RISC_STORAGE_WORK_INVALID;
  const auto&q=*static_cast<const Request*>(input);if(q.size>PayloadMax)return RISC_STORAGE_WORK_INVALID;
  const auto result=self.fn_(self.context_,q.action,PayloadMax?self.payload_->bytes:nullptr,q.size);
  if(result.status==NativeReplicaCore::Retained)self.retained_.store(true);
  std::memcpy(out,&result,sizeof result);*actual=sizeof result;return RISC_STORAGE_WORK_OK;
 }
};
using NativeReplicaJob=NativeReplicaJobT<4096>;
using NativeReplicaControlJob=NativeReplicaJobT<0>;
}
