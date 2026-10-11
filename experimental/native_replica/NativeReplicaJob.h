#pragma once
#include "NativeReplicaCore.h"
#include "runtime/storage/StorageWorker.h"
namespace RiscExperimental {
// Disconnected native copied-worker facade. Owner submit/collect/cancel perform
// only bounded copies/atomics. Filesystem/NVS calls belong to the fixed native
// execute callback, never an app callback. Credentials never enter Worker’s
// non-wiping request/result arrays: only control fields and numeric results do.
class NativeReplicaJob {
 public:
 static constexpr uint32_t PayloadMax=4096;
 struct Result {int32_t status;uint32_t pending;uint64_t revision;};
 using Execute=Result(*)(void*,uint32_t,const void*,uint32_t);
 NativeReplicaJob(Execute fn,void*context):fn_(fn),context_(context){worker_.bind(run,this);}
 ~NativeReplicaJob(){NativeReplicaCore::wipe(payload_,sizeof payload_);}
 NativeReplicaJob(const NativeReplicaJob&)=delete;
 NativeReplicaJob&operator=(const NativeReplicaJob&)=delete;
 int begin(uint32_t action,const void*payload,uint32_t size,uint64_t*out){
  if(out)*out=0;
  if(!out||(!payload&&size)||size>sizeof payload_||!fn_)return RISC_STORAGE_WORK_INVALID;
  if(retained_.load())return RISC_STORAGE_WORK_RETAINED;
  if(!worker_.idle())return RISC_STORAGE_WORK_BUSY;
  if(size)std::memcpy(payload_,payload,size);
  Request q{action,size};int rc=worker_.submit(&q,sizeof q,out);
  if(rc!=RISC_STORAGE_WORK_OK)NativeReplicaCore::wipe(payload_,sizeof payload_);
  return rc;
 }
 int collect(uint64_t token,Result*out){
  if(!out)return RISC_STORAGE_WORK_INVALID;
  uint32_t size=0;int32_t status=0;Result result{};const int rc=worker_.collect(token,&result,sizeof result,&size,&status);
  if(rc!=RISC_STORAGE_WORK_OK)return rc;
  if(size!=sizeof result||status!=RISC_STORAGE_WORK_OK){retained_.store(true);return RISC_STORAGE_WORK_RETAINED;}
  *out=result;return RISC_STORAGE_WORK_OK;
 }
 int cancel(uint64_t token){int rc=worker_.cancel(token);if(rc==RISC_STORAGE_WORK_CANCELLED)NativeReplicaCore::wipe(payload_,sizeof payload_);return rc;}
 bool workerStep(){return worker_.workerStep();}
 bool exitSafe()const{return worker_.idle()&&!retained_.load();}
 bool pending()const{return !worker_.idle();}
 bool scratchCleared()const{if(!worker_.idle())return false;for(auto v:payload_)if(v)return false;return true;}
 private:
 struct Request {uint32_t action,size;};
 RiscStorage::Worker worker_;Execute fn_;void*context_;std::atomic<bool>retained_{false};uint8_t payload_[PayloadMax]{};
 struct Wipe {uint8_t*p;size_t n;~Wipe(){NativeReplicaCore::wipe(p,n);}};
 static int32_t run(void*p,const void*input,uint32_t size,void*out,uint32_t cap,uint32_t*actual){
  auto&self=*static_cast<NativeReplicaJob*>(p);Wipe wipe{self.payload_,sizeof self.payload_};
  if(size!=sizeof(Request)||cap<sizeof(Result))return RISC_STORAGE_WORK_INVALID;
  const auto&q=*static_cast<const Request*>(input);if(q.size>sizeof self.payload_)return RISC_STORAGE_WORK_INVALID;
  const auto result=self.fn_(self.context_,q.action,self.payload_,q.size);
  if(result.status==NativeReplicaCore::Retained)self.retained_.store(true);
  std::memcpy(out,&result,sizeof result);*actual=sizeof result;return RISC_STORAGE_WORK_OK;
 }
};
}
