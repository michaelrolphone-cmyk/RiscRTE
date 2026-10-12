#include "experimental/native_replica/NativeReplicaJob.h"
#include OLD_WORKER_HEADER
#include <cassert>
#include <atomic>
#include <thread>
#include <cstring>
#include <cstdio>
#include <map>
using namespace RiscExperimental;
static_assert(sizeof(RiscStorage::Worker)==sizeof(BaselineStorage::Worker),"default hardware mailbox layout must stay unchanged");
static_assert(alignof(RiscStorage::Worker)==alignof(BaselineStorage::Worker),"default alignment");
static_assert(sizeof(NativeReplicaControlJob)<256,"numeric control must not allocate credential/media buffers");
static_assert(sizeof(NativeReplicaJob)<256,"copied credential bytes must not live inside the atomic control object");
struct Calls {std::atomic<unsigned> count{0};bool retained=false;};
static NativeReplicaJobResult execute(void*p,uint32_t action,const void*data,uint32_t n){
 auto&calls=*static_cast<Calls*>(p);++calls.count;const auto*b=static_cast<const unsigned char*>(data);uint64_t sum=action;for(uint32_t i=0;i<n;++i)sum+=b[i];return {calls.retained?NativeReplicaCore::Retained:NativeReplicaCore::Ok,n,sum};
}
struct PayloadAlloc {
 bool fail=false;unsigned calls=0,releases=0;std::map<void*,size_t>live;
 NativeReplicaCore::Allocator api(){return {this,[](void*p,size_t n)->void*{auto&a=*static_cast<PayloadAlloc*>(p);++a.calls;if(a.fail)return nullptr;auto*b=malloc(n);assert(b);memset(b,0xa5,n);a.live[b]=n;return b;},[](void*p,void*b){auto&a=*static_cast<PayloadAlloc*>(p);auto i=a.live.find(b);assert(i!=a.live.end());for(size_t k=0;k<i->second;++k)assert(!static_cast<uint8_t*>(b)[k]);a.live.erase(i);++a.releases;free(b);}};}
 ~PayloadAlloc(){assert(live.empty());}
};
void payloadAllocation(){
 PayloadAlloc allocation;allocation.fail=true;Calls calls;
 {NativeReplicaControlJob control(execute,&calls,allocation.api());uint64_t token=0;assert(!control.begin(1,nullptr,0,&token)&&control.workerStep());NativeReplicaJobResult result{};assert(!control.collect(token,&result)&&!allocation.calls);}
 calls.count=0;
 NativeReplicaJob job(execute,&calls,allocation.api());uint8_t bytes[4096];memset(bytes,7,sizeof bytes);uint64_t token=123;
 assert(job.begin(2,bytes,sizeof bytes,&token)==NativeReplicaJob::NoMemory&&!token&&job.exitSafe()&&job.scratchCleared()&&!calls.count&&allocation.live.empty());
 allocation.fail=false;assert(!job.begin(2,bytes,sizeof bytes,&token)&&allocation.live.size()==1);
 assert(job.cancel(token)==RISC_STORAGE_WORK_CANCELLED&&allocation.live.empty()&&allocation.releases==1&&!calls.count);
 assert(!job.begin(3,bytes,sizeof bytes,&token)&&allocation.live.size()==1);calls.retained=true;assert(job.workerStep());
 assert(allocation.live.empty()&&allocation.releases==2);NativeReplicaJobResult result{};assert(!job.collect(token,&result)&&result.status==NativeReplicaCore::Retained&&!job.exitSafe());
 const auto count=allocation.calls;assert(job.begin(4,bytes,sizeof bytes,&token)==RISC_STORAGE_WORK_RETAINED&&!token&&allocation.calls==count);
 puts("copied job payload: zero-payload no allocation, OOM before mailbox acceptance, queued cancel wipe/free, returning Retained wipe/free with permanent retained control PASS");
}
void blockedPayload(){
 PayloadAlloc allocation;struct State {std::atomic<bool>entered{false},release{false};}state;
 NativeReplicaJob job([](void*p,uint32_t,const void*bytes,uint32_t n){auto&s=*static_cast<State*>(p);s.entered.store(true);while(!s.release.load())std::this_thread::yield();assert(n==4096&&static_cast<const uint8_t*>(bytes)[0]==9);return NativeReplicaJobResult{0,0,0};},&state,allocation.api());
 uint8_t bytes[4096];memset(bytes,9,sizeof bytes);uint64_t token=0;assert(!job.begin(1,bytes,sizeof bytes,&token));
 std::thread worker([&]{assert(job.workerStep());});while(!state.entered.load())std::this_thread::yield();
 assert(allocation.live.size()==1&&!allocation.releases&&job.cancel(token)==RISC_STORAGE_WORK_PENDING&&allocation.live.size()==1);
 state.release.store(true);worker.join();assert(allocation.live.empty()&&allocation.releases==1);NativeReplicaJobResult result{};assert(job.collect(token,&result)==RISC_STORAGE_WORK_CANCELLED&&job.exitSafe()&&job.scratchCleared());
 puts("copied job payload: blocked worker and pending cancellation retain the allocation until callback return, then wipe/free before completion publication PASS");
}
int main(){payloadAllocation();blockedPayload();
 {Calls calls;NativeReplicaControlJob job(execute,&calls);uint64_t token=99;unsigned char rejected=0x7f;
  assert(job.begin(7,&rejected,1,&token)==RISC_STORAGE_WORK_INVALID&&!token&&job.exitSafe());
  assert(!job.begin(7,nullptr,0,&token)&&token);assert(job.pending()&&!job.exitSafe());assert(job.workerStep());NativeReplicaJob::Result result{};assert(!job.collect(token,&result)&&result.status==0&&result.pending==0&&result.revision==7);assert(calls.count==1&&job.exitSafe()&&job.scratchCleared());
 }
 {Calls calls;NativeReplicaJob job(execute,&calls);unsigned char bytes[4097];memset(bytes,3,sizeof bytes);uint64_t token=0;
  assert(job.begin(1,bytes,sizeof bytes,&token)==RISC_STORAGE_WORK_INVALID&&!token&&job.scratchCleared());assert(!job.begin(1,bytes,4096,&token));memset(bytes,9,sizeof bytes);assert(job.workerStep());NativeReplicaJobResult result{};assert(!job.collect(token,&result)&&result.pending==4096&&result.revision==12289&&job.scratchCleared());
  assert(!job.begin(2,bytes,4096,&token));assert(job.cancel(token)==RISC_STORAGE_WORK_CANCELLED&&!job.workerStep()&&job.scratchCleared()&&calls.count==1);
 }
 {Calls calls;NativeReplicaControlJob job(execute,&calls);std::atomic<bool>stop{false};std::thread worker([&]{while(!stop.load(std::memory_order_acquire)){job.workerStep();std::this_thread::yield();}});
  for(uint32_t i=1;i<=10000;++i){uint64_t token=0;assert(!job.begin(i,nullptr,0,&token));NativeReplicaJobResult result{};int rc;while((rc=job.collect(token,&result))==RISC_STORAGE_WORK_PENDING)std::this_thread::yield();assert(!rc&&result.revision==i&&job.exitSafe());}
  stop.store(true,std::memory_order_release);worker.join();assert(calls.count==10000);
 }
 {Calls calls;calls.retained=true;NativeReplicaControlJob job(execute,&calls);uint64_t token=0;assert(!job.begin(1,nullptr,0,&token)&&job.workerStep());NativeReplicaJobResult result{};assert(!job.collect(token,&result)&&result.status==NativeReplicaCore::Retained&&!job.exitSafe());assert(job.begin(2,nullptr,0,&token)==RISC_STORAGE_WORK_RETAINED&&!token);}
 {RiscStorage::BasicWorker<8,16> worker;assert(!worker.bind([](void*,const void*,uint32_t n,void*out,uint32_t cap,uint32_t*actual)->int32_t{assert(n==8&&cap==16);memset(out,0x55,16);*actual=17;return 7;},nullptr));char bytes[9]{};uint64_t token=0;assert(worker.submit(bytes,9,&token)==RISC_STORAGE_WORK_INVALID);assert(!worker.submit(bytes,8,&token)&&worker.workerStep());uint32_t n=0;int32_t status=0;assert(!worker.collect(token,bytes,sizeof bytes,&n,&status)&&!n&&status==RISC_STORAGE_WORK_INVALID);}
 printf("compact native jobs: zero/full payload bounds, copy/wipe, 10000 threaded generations, retention and result bounds PASS; default Worker=%zu, payload job=%zu, control job=%zu\n",sizeof(RiscStorage::Worker),sizeof(NativeReplicaJob),sizeof(NativeReplicaControlJob));
}
