#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <new>
#include <sys/mman.h>
#include <unistd.h>
// Make only our class members inspectable to establish constructor bind state;
// standard library headers are already included before this test-only access.
#include "experimental/native_replica/NativeReplicaCore.h"
#include "experimental/native_replica/OwnedSettingsScratch.h"
#include "runtime/storage/StorageWorker.h"
#include "ports/esp32s3/NativeInternalAtomics.h"
#include <RiscKeyValueAtomicV1.h>
#define private public
#include "experimental/native_replica/NativeAtomicKeyValue.h"
#undef private
using J=RiscExperimental::NativeReplicaJob;
using A=RiscExperimental::NativeAtomicKeyValue;
using C=RiscExperimental::NativeReplicaCore;
static uintptr_t deniedFirst=0,deniedLast=0;
static unsigned checks=0,callbacks=0;
bool esp_ptr_in_dram(const void*p){++checks;auto n=reinterpret_cast<uintptr_t>(p);return !(n>=deniedFirst&&n<=deniedLast);}
static void deny(void*p,size_t n){deniedFirst=uintptr_t(p);deniedLast=deniedFirst+n-1;}
static void allow(){deniedFirst=deniedLast=0;}
static J::Result execute(void*,uint32_t,const void*,uint32_t){++callbacks;return {C::Ok,0,0};}
static A::Guard guard(){return {nullptr,[](void*){++callbacks;return true;},[](void*){++callbacks;return true;},[](void*){++callbacks;return true;},[](void*){++callbacks;return true;},[](void*){++callbacks;return int(C::Ok);}};}
int main(){
 C core(6,{},{},C::heap()); // No backend is invoked by these placement-refusal tests.
 alignas(J) unsigned char jobBytes[sizeof(J)];deny(jobBytes,sizeof jobBytes);
 auto*job=new(jobBytes)J(execute,nullptr);assert(!job->worker_.bound());
 uint64_t token=99;J::Result result{};assert(job->begin(1,nullptr,0,&token)==RISC_STORAGE_WORK_RETAINED&&!token);
 assert(job->collect(1,&result)==RISC_STORAGE_WORK_RETAINED&&job->cancel(1)==RISC_STORAGE_WORK_RETAINED);
 assert(!job->workerStep()&&!job->exitSafe()&&job->pending()&&!job->scratchCleared());
 assert(!callbacks);allow();job->~J();
 // Only the last byte is outside internal DRAM: a start-address-only check
 // would incorrectly bind this straddling object.
 deny(jobBytes+sizeof jobBytes-1,1);job=new(jobBytes)J(execute,nullptr);
 assert(!job->worker_.bound()&&!job->workerStep()&&!job->exitSafe());allow();job->~J();
 alignas(A) unsigned char apiBytes[sizeof(A)];deny(apiBytes,sizeof apiBytes);
 auto*endpoint=new(apiBytes)A(core,guard());assert(!endpoint->job_.worker_.bound());
 risc_key_value_atomic_state_v1 state{sizeof(state)};risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt)};uint32_t n=0;
 assert(endpoint->state(&state)==RISC_ATOMIC_RETAINED&&endpoint->get(1,"x",nullptr,0,&n)==RISC_ATOMIC_RETAINED);
 token=99;assert(endpoint->submit(1,nullptr,0,"x",1,&token)==RISC_ATOMIC_RETAINED&&!token);
 assert(endpoint->collect(1,&receipt)==RISC_ATOMIC_RETAINED&&endpoint->cancel(1)==RISC_ATOMIC_RETAINED);
 assert(!endpoint->namespaceId()&&!endpoint->api().state);
 assert(!endpoint->workerStep()&&endpoint->retained()&&!endpoint->exitSafe()&&!endpoint->scratchCleared()&&!endpoint->nativeReleaseSafe()&&endpoint->nativePending());
 assert(!callbacks);allow();endpoint->~A();
 // Denied controls behind PROT_NONE prove no job/phase atomic or other member
 // read occurs before the full-span check on public operation/status methods.
 size_t page=size_t(sysconf(_SC_PAGESIZE));assert(sizeof(A)<page&&sizeof(J)<page);
 void*memory=mmap(nullptr,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(memory!=MAP_FAILED);
 endpoint=new(memory)A(core,guard());deny(memory,page);assert(!mprotect(memory,page,PROT_NONE));
 token=99;assert(endpoint->submit(1,nullptr,0,"x",1,&token)==RISC_ATOMIC_RETAINED&&!token);
 assert(endpoint->state(&state)==RISC_ATOMIC_RETAINED&&endpoint->get(1,"x",nullptr,0,&n)==RISC_ATOMIC_RETAINED);
 assert(endpoint->collect(1,&receipt)==RISC_ATOMIC_RETAINED&&endpoint->cancel(1)==RISC_ATOMIC_RETAINED);
 assert(!endpoint->namespaceId()&&!endpoint->api().state);
 assert(!endpoint->workerStep()&&!endpoint->exitSafe()&&!endpoint->scratchCleared()&&endpoint->nativePending()&&endpoint->retained()&&!endpoint->nativeReleaseSafe());
 assert(!mprotect(memory,page,PROT_READ|PROT_WRITE));allow();endpoint->~A();
 job=new(memory)J(execute,nullptr);deny(memory,page);assert(!mprotect(memory,page,PROT_NONE));
 assert(job->begin(1,nullptr,0,&token)==RISC_STORAGE_WORK_RETAINED&&job->collect(1,&result)==RISC_STORAGE_WORK_RETAINED&&job->cancel(1)==RISC_STORAGE_WORK_RETAINED);
 assert(!job->workerStep()&&!job->exitSafe()&&job->pending()&&!job->scratchCleared());
 assert(!mprotect(memory,page,PROT_READ|PROT_WRITE));allow();job->~J();assert(!munmap(memory,page));
 assert(checks&&!callbacks);puts("PASS: denied job/atomic controls never bind; PROT_NONE operations/status refuse before any member/atomic access");
}
