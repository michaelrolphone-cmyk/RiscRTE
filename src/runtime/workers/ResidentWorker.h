#pragma once
#include <RiscResidentWorkerV1.h>
#include <atomic>
#include <cstddef>
namespace RiscWorkers {
/* Resident internal-RAM authority, never embedded in provider PSRAM. The owner
 * serializes start/cancel/join/release. Only dispatch and its borrowed control
 * callbacks run on workers. Mirrors existing StorageWorker release/acquire
 * completion and Reader resident trampoline without exporting private imports. */
class ResidentWorker final {
 public:
 struct Backend {
  bool (*owner)();uint64_t (*now)();void (*delay)(uint32_t);
  // False guarantees dispatch can never occur. On true it may already finish.
  bool (*create)(void(*)(void*),void*,uint32_t,uint32_t,int32_t);
  uint32_t cores,maxPriority;
 };
 explicit ResidentWorker(const Backend&b):backend_(b){}
 ResidentWorker(const ResidentWorker&)=delete;
 ResidentWorker&operator=(const ResidentWorker&)=delete;
 bool owner()const{return backend_.owner&&backend_.owner();}
 bool idle()const{for(const auto&s:slots_)if(s.token)return false;return true;}
 int32_t start(uint64_t scope,risc_worker_entry_v1 fn,void*arg,uint32_t stack,uint32_t priority,int32_t core,uint64_t*out){
  if(out)*out=0;
  if(!owner())return RISC_WORKER_CONTEXT;
  if(!out||!scope||!fn||!backend_.now||!backend_.delay||!backend_.create||stack<4096||stack>32768||(stack&3)||!priority||priority>5||priority>=backend_.maxPriority||core< -1||(core>=0&&uint32_t(core)>=backend_.cores))return RISC_WORKER_INVALID;
  if(serial_==UINT64_MAX)return RISC_WORKER_CAPACITY;
  Slot*s=nullptr;for(auto&slot:slots_)if(!slot.token){s=&slot;break;}
  if(!s)return RISC_WORKER_CAPACITY;
  s->authority=this;s->scope=scope;s->token=++serial_;s->entry=fn;s->argument=arg;s->joined=false;
  s->cancelled.store(false,std::memory_order_relaxed);s->complete.store(false,std::memory_order_relaxed);
  s->control={s,[](void*c){return static_cast<Slot*>(c)->cancelled.load(std::memory_order_acquire);},[](void*c,uint32_t ms){auto&s=*static_cast<Slot*>(c);s.authority->backend_.delay(ms? (ms>1000?1000:ms):1);}};
  if(!backend_.create(dispatch,s,stack,priority,core)){retire(*s);return RISC_WORKER_ALLOCATION;}
  *out=s->token;return RISC_WORKER_OK;
 }
 int32_t cancel(uint64_t scope,uint64_t token){
  if(!owner())return RISC_WORKER_CONTEXT;
  Slot*s=find(scope,token);if(!s)return RISC_WORKER_STALE;
  s->cancelled.store(true,std::memory_order_release);return RISC_WORKER_OK;
 }
 int32_t join(uint64_t scope,uint64_t token,uint32_t timeout){
  if(!owner())return RISC_WORKER_CONTEXT;
  if(timeout>RISC_RESIDENT_WORKER_MAX_JOIN_MS)return RISC_WORKER_INVALID;
  Slot*s=find(scope,token);if(!s)return RISC_WORKER_STALE;
  const uint64_t began=backend_.now();
  for(uint32_t polls=0;;++polls){
   if(s->complete.load(std::memory_order_acquire)){s->joined=true;return RISC_WORKER_OK;}
   const uint64_t now=backend_.now();
   if(!timeout||polls>=RISC_RESIDENT_WORKER_MAX_JOIN_MS||now<began||now-began>=timeout)return RISC_WORKER_TIMEOUT;
   backend_.delay(1);
  }
 }
 int32_t release(uint64_t scope,uint64_t token){
  if(!owner())return RISC_WORKER_CONTEXT;
  Slot*s=find(scope,token);if(!s)return RISC_WORKER_STALE;
  if(!s->joined||!s->complete.load(std::memory_order_acquire))return RISC_WORKER_BUSY;
  retire(*s);return RISC_WORKER_OK;
 }
 private:
 struct Slot {ResidentWorker*authority=nullptr;uint64_t scope=0,token=0;risc_worker_entry_v1 entry=nullptr;void*argument=nullptr;risc_worker_control_v1 control{};bool joined=false;std::atomic<bool>cancelled{false},complete{false};};
 static void dispatch(void*context){
  auto&s=*static_cast<Slot*>(context);auto fn=s.entry;void*arg=s.argument;
  fn(arg,&s.control);s.entry=nullptr;s.argument=nullptr;
  // LAST slot access. Remaining return and OS cleanup execute resident code.
  s.complete.store(true,std::memory_order_release);
 }
 Slot*find(uint64_t scope,uint64_t token){if(scope&&token)for(auto&s:slots_)if(s.scope==scope&&s.token==token)return &s;return nullptr;}
 static void retire(Slot&s){s.token=0;s.scope=0;s.entry=nullptr;s.argument=nullptr;s.joined=false;}
 Backend backend_;Slot slots_[RISC_RESIDENT_WORKER_SLOTS];uint64_t serial_=0;
};
}
