#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main native_core_fixture_main
#include "native_replica_core_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "experimental/native_replica/NativeAtomicKeyValue.h"
#include <thread>
#include <mutex>
#include <condition_variable>
using A=RiscExperimental::NativeAtomicKeyValue;
struct Gate {
 std::thread::id ownerThread=std::this_thread::get_id(),workerThread{};
 bool held=false,deny=false;int finishResult=0;
 A::Guard api(){return {this,
 [](void*p){return std::this_thread::get_id()==static_cast<Gate*>(p)->ownerThread;},
 [](void*p){return std::this_thread::get_id()==static_cast<Gate*>(p)->workerThread;},
 [](void*p){auto&g=*static_cast<Gate*>(p);if(g.held||g.deny)return false;g.held=true;return true;},
 [](void*p){auto&g=*static_cast<Gate*>(p);assert(g.held);g.held=false;return true;},[](void*p){return static_cast<Gate*>(p)->finishResult;}};}
 void run(A&a){workerThread=ownerThread;assert(a.workerStep());workerThread={};}
};
uint64_t submit(A&a,C&c,const char*value="new") {
 const risc_key_value_atomic_mutation_v1 m[]={{"local",RISC_ATOMIC_PUT,value,uint32_t(strlen(value))},{"paired",RISC_ATOMIC_PUT,value,uint32_t(strlen(value))}};
 uint64_t token=0;assert(a.submit(c.revision(),m,2,"intent-save",11,&token)==0&&token);return token;
}
risc_key_value_atomic_receipt_v1 collect(A&a,uint64_t token){risc_key_value_atomic_receipt_v1 out{sizeof(out),0,0,0};assert(a.collect(token,&out)==0);return out;}
void endpointBasics(){
 Disk d=initial();Alloc allocation;C c(6,d.backend(),limits(),allocation.api());assert(c.load()==0);Gate gate;A a(c,gate.api());auto api=a.api();
 risc_key_value_atomic_state_v1 state{};state.struct_size=sizeof(state);d.calls=0;
 assert(api.state(api.context,&state)==0&&state.namespace_id==6&&state.revision==c.revision()&&!d.calls&&!gate.held);
 const auto before=d.root;char v[]="new",intent[]="intent-save",key[]="local";
 risc_key_value_atomic_mutation_v1 m{key,RISC_ATOMIC_PUT,v,3};uint64_t token=0;
 assert(api.submit(api.context,state.revision,&m,1,intent,11,&token)==0&&token&&!a.exitSafe()&&gate.held&&!d.calls&&d.root==before);
 memset(v,'x',3);memset(intent,'x',11);memset(key,'x',5);
 char out[8];uint32_t size=0;assert(api.get(api.context,state.revision,"local",out,sizeof out,&size)==RISC_ATOMIC_BUSY&&size==0);assert(api.state(api.context,&state)==RISC_ATOMIC_BUSY);
 uint64_t refused=99;assert(api.submit(api.context,state.revision,nullptr,0,"intent",6,&refused)==RISC_ATOMIC_BUSY&&!refused);
 risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt),0,0,0};assert(api.collect(api.context,token,&receipt)==RISC_ATOMIC_PENDING);assert(!a.workerStep()&&!d.calls);
 gate.run(a);assert(!a.exitSafe());receipt=collect(a,token);assert(a.scratchCleared());assert(receipt.status==0&&receipt.operation_id==1&&receipt.revision==c.revision()&&a.exitSafe()&&!gate.held&&get(c,"local")=="new");
 uint64_t id=0;uint32_t n=0;char kept[32];assert(c.pending(0,&id,kept,sizeof kept,&n)==0&&id==1&&n==11&&!memcmp(kept,"intent-save",11));
 assert(a.collect(token,&receipt)==RISC_ATOMIC_INVALID);
 auto again=submit(a,c);gate.run(a);auto second=collect(a,again);assert(second.status==0&&second.operation_id==2&&c.pendingCount()==2);
 uint64_t stale=88;assert(a.submit(state.revision,nullptr,0,"explicit",8,&stale)==RISC_ATOMIC_STALE&&!stale);
 puts("atomic endpoint: copied inputs, owner/worker boundary, busy reads, receipt lifetime, same-value intent, stale refusal PASS");
}
void endpointCancellation(){
 Disk d=initial();Alloc allocation;C c(6,d.backend(),limits(),allocation.api());assert(!c.load());Gate gate;A a(c,gate.api());
 auto token=submit(a,c);d.calls=0;assert(a.cancel(token)==RISC_ATOMIC_PENDING);gate.run(a);auto first=collect(a,token);assert(first.status==RISC_ATOMIC_CANCELLED&&!first.operation_id&&!d.calls&&c.pendingCount()==0&&a.exitSafe());
 token=submit(a,c);gate.run(a);assert(a.cancel(token)==RISC_ATOMIC_PENDING);auto second=collect(a,token);assert(second.status==0&&second.operation_id==1&&get(c,"local")=="new"&&a.exitSafe());
 puts("atomic endpoint: cancel-before-claim zero mutation; cancel-after-commit preserves committed receipt PASS");
}
void endpointCancelRace(){
 Disk d=initial();Alloc allocation;auto backend=d.backend();
 struct Pause {C::Backend source;std::mutex mutex;std::condition_variable ready;bool paused=false,resume=false;};
 Pause pause{backend,{},{},false,false};backend.context=&pause;
 backend.readRoot=[](void*p,void*b,uint32_t n){auto&s=*static_cast<Pause*>(p);return s.source.readRoot(s.source.context,b,n);};
 backend.reserve=[](void*p,uint32_t n,uint64_t*i){auto&s=*static_cast<Pause*>(p);return s.source.reserve(s.source.context,n,i);};
 backend.write=[](void*p,uint64_t i,uint32_t a,const void*b,uint32_t n){auto&s=*static_cast<Pause*>(p);return s.source.write(s.source.context,i,a,b,n);};
 backend.read=[](void*p,uint64_t i,uint32_t a,void*b,uint32_t n){auto&s=*static_cast<Pause*>(p);return s.source.read(s.source.context,i,a,b,n);};
 backend.seal=[](void*p,uint64_t i){auto&s=*static_cast<Pause*>(p);{std::unique_lock<std::mutex> lock(s.mutex);s.paused=true;s.ready.notify_all();s.ready.wait(lock,[&]{return s.resume;});}return s.source.seal(s.source.context,i);};
 backend.replaceRoot=[](void*p,const void*b,uint32_t n){auto&s=*static_cast<Pause*>(p);return s.source.replaceRoot(s.source.context,b,n);};
 backend.retire=[](void*p,uint64_t i){auto&s=*static_cast<Pause*>(p);s.source.retire(s.source.context,i);};
 C c(6,backend,limits(),allocation.api());assert(!c.load());Gate gate;A a(c,gate.api());uint64_t token=submit(a,c);
 std::thread worker([&]{gate.workerThread=std::this_thread::get_id();assert(a.workerStep());});
 {std::unique_lock<std::mutex> lock(pause.mutex);pause.ready.wait(lock,[&]{return pause.paused;});}
 assert(a.cancel(token)==RISC_ATOMIC_PENDING);risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt),0,0,0};assert(a.collect(token,&receipt)==RISC_ATOMIC_PENDING);
 {std::lock_guard<std::mutex> lock(pause.mutex);pause.resume=true;}pause.ready.notify_all();worker.join();
 receipt=collect(a,token);assert(receipt.status==0&&receipt.operation_id==1&&c.pendingCount()==1&&get(c,"paired")=="new");
 puts("atomic endpoint: cancellation during native publication cannot discard committed receipt PASS");
}
void endpointRefusals(){
 Disk d=initial();Alloc allocation;C c(6,d.backend(),limits(),allocation.api());assert(!c.load());Gate gate;A a(c,gate.api());uint64_t token=99;const auto root=d.root;d.calls=0;
 risc_key_value_atomic_mutation_v1 dupe[]={{"a",1,"v",1},{"a",1,"x",1}};assert(a.submit(c.revision(),dupe,2,"intent",6,&token)==RISC_ATOMIC_INVALID&&!token);
 risc_key_value_atomic_mutation_v1 invalid{"Invalid",1,"v",1};assert(a.submit(c.revision(),&invalid,1,"intent",6,&token)==RISC_ATOMIC_INVALID);
 std::string big(2048,'x');risc_key_value_atomic_mutation_v1 huge[]={{"a",1,big.data(),2048},{"b",1,big.data(),2048}};assert(a.submit(c.revision(),huge,2,"intent",6,&token)==RISC_ATOMIC_CAPACITY);
 gate.deny=true;assert(a.submit(c.revision(),nullptr,0,"intent",6,&token)==RISC_ATOMIC_BUSY);gate.deny=false;
 assert(!d.calls&&d.root==root&&a.exitSafe());
 // A DELETE and an absent DELETE stay explicit in one operation.
 risc_key_value_atomic_mutation_v1 deletion[]={{"key0000",2,nullptr,0},{"absent",2,nullptr,0}};
 assert(!a.submit(c.revision(),deletion,2,"explicit-forget",15,&token));gate.run(a);assert(collect(a,token).status==0);missing(c,"key0000");
 puts("atomic endpoint: duplicate/bad keys, capacity and custody refusals do not mutate; explicit DELETE group PASS");
}
void endpointRetention(){
 Disk d=initial();Alloc allocation;C c(6,d.backend(),limits(),allocation.api());assert(!c.load());Gate gate;A a(c,gate.api());gate.finishResult=C::Retained;
 auto token=submit(a,c);gate.run(a);risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt),0,0,0};
 assert(a.collect(token,&receipt)==RISC_ATOMIC_RETAINED&&gate.held&&!a.exitSafe());
 assert(a.cancel(token)==RISC_ATOMIC_RETAINED&&get(c,"local")=="new");
 risc_key_value_atomic_state_v1 state{};state.struct_size=sizeof(state);assert(a.state(&state)==RISC_ATOMIC_RETAINED);
 puts("atomic endpoint: failed native terminal cleanup retains namespace gate and suppresses unsafe success PASS");
}
void endpointFaults(){
 const Disk source=initial();unsigned calls=0;
 {Disk d=source;Alloc allocation;C c(6,d.backend(),limits(),allocation.api());assert(!c.load());Gate gate;A a(c,gate.api());auto token=submit(a,c);d.calls=0;gate.run(a);assert(collect(a,token).status==0);calls=d.calls;}
 unsigned cases=0,uncertain=0;
 for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=calls;++cut){
  Disk d=source;Alloc allocation;
  {C c(6,d.backend(),limits(),allocation.api());assert(!c.load());Gate gate;A a(c,gate.api());auto token=submit(a,c);d.fault(cut,Disk::Mode(mode));gate.run(a);auto receipt=collect(a,token);assert(receipt.status==0||receipt.status==RISC_ATOMIC_IO||receipt.status==RISC_ATOMIC_UNCERTAIN);if(receipt.status==RISC_ATOMIC_UNCERTAIN){++uncertain;assert(!c.ready());}assert(a.exitSafe()&&a.scratchCleared());}
  d.fault(0,Disk::Before);C reload(6,d.backend(),limits(),allocation.api());assert(!reload.load());
  if(reload.pendingCount()){assert(reload.pendingCount()==1&&get(reload,"local")=="new"&&get(reload,"paired")=="new");uint64_t id=0;char intent[32];uint32_t n=0;assert(reload.pending(0,&id,intent,sizeof intent,&n)==0&&n==11&&!memcmp(intent,"intent-save",11));}
  else {missing(reload,"local");missing(reload,"paired");}++cases;
 }
 assert(uncertain);printf("atomic endpoint: %u before/after/torn native backend failures recover complete old or new map+intent; %u uncertain receipts PASS\n",cases,uncertain);
}
#ifndef NATIVE_ATOMIC_ENDPOINT_FIXTURE_ONLY
int main(){endpointBasics();endpointCancellation();endpointCancelRace();endpointRefusals();endpointRetention();endpointFaults();}
#endif
