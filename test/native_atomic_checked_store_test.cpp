#include "native_replica_checked_fixture.h"
#include "experimental/native_replica/NativeAtomicKeyValue.h"
using A=RiscExperimental::NativeAtomicKeyValue;
using namespace NativeFixture;
struct NativeGate {
 Fixture&f;bool held=false;
 A::Guard api(){return {this,
 [](void*){return !onWorker;},[](void*){return onWorker&&lease;},
 [](void*p){auto&s=*static_cast<NativeGate*>(p);if(s.held)return false;s.held=true;lease=true;return true;},
 [](void*p){auto&s=*static_cast<NativeGate*>(p);assert(s.held);s.held=false;lease=false;return true;},
 [](void*p){auto&s=*static_cast<NativeGate*>(p);safe();s.f.store.finishOperation();return s.f.store.exitSafe()?int(C::Ok):int(C::Retained);}};}
};
int main(int argc,char**argv){
 assert(argc==2);rootPath=argv[1];std::filesystem::create_directories(rootPath);empty();
 {Fixture f;assert(f.core.initialize(nullptr,0,{})==0);f.store.finishOperation();NativeGate gate{f};A api(f.core,gate.api());
  onWorker=false;lease=false;const unsigned beforeNvs=nvsTotal,beforeFs=fsTotal;uint64_t token=0;
  const risc_key_value_atomic_mutation_v1 changes[]={{"wifi.profiles",1,"count",5},{"wifi.commit",1,"selector",8},{"wifi.a0",1,"chunk",5}};
  assert(api.submit(f.core.revision(),changes,3,"synthetic-semantic-intent",25,&token)==0&&token&&nvsTotal==beforeNvs&&fsTotal==beforeFs);
  risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt),0,0,0};
  for(unsigned i=0;i<1000;++i)assert(api.collect(token,&receipt)==RISC_ATOMIC_PENDING);
  assert(nvsTotal==beforeNvs&&fsTotal==beforeFs&&!api.workerStep());
  onWorker=true;assert(api.workerStep());onWorker=false;
  assert(f.store.exitSafe()&&api.collect(token,&receipt)==0&&receipt.status==0&&receipt.operation_id==1&&api.exitSafe()&&api.scratchCleared()&&nvsTotal>beforeNvs&&fsTotal>beforeFs);
  assert(value(f.core,"wifi.profiles")=="count"&&value(f.core,"wifi.commit")=="selector"&&value(f.core,"wifi.a0")=="chunk");
 }
 onWorker=lease=true;{Fixture reboot;assert(reboot.core.load()==0&&reboot.core.pendingCount()==1&&value(reboot.core,"wifi.commit")=="selector");uint64_t id=0;uint32_t n=0;char intent[32];assert(reboot.core.pending(0,&id,intent,sizeof intent,&n)==0&&n==25&&!memcmp(intent,"synthetic-semantic-intent",25));reboot.store.finishOperation();}
 assert(handles.empty());
 puts("Native atomic endpoint + checked AppDataFiles/NvsReplicaMetadata: 1000 owner polls without backend I/O, worker publication/cleanup, complete map+intent reboot PASS");
}
