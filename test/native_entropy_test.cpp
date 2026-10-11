#include "ports/esp32s3/NativeEntropy.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <thread>
using namespace RiscCpu;
static const auto* api=NativeEntropy::backend();
static bool owns=true,rf=true,loseInSdk=false,rfLostInSdk=false;
static unsigned ownerCalls=0,readyCalls=0,rngCalls=0,loseOwnerAt=0,loseReadyAt=0;
static unsigned unavailableAt=0,reenterOwnerAt=0,reenterReadyAt=0;
static bool reenterSdk=false,concurrentSdk=false;
static bool resourceBusy=false,resourceHeld=false;
static unsigned takeCalls=0,giveCalls=0;
static bool take(){++takeCalls;if(resourceBusy)return false;assert(!resourceHeld);resourceHeld=true;return true;}
static void give(){assert(resourceHeld);resourceHeld=false;++giveCalls;}
static void* callerOutput=nullptr;
static uint32_t callerSize=0;
static constexpr uint8_t untouched=0xa7;
using Buffer=std::array<uint8_t,RISC_ENTROPY_BYTES_MAX+2>;
static void unchanged(const Buffer& bytes){for(auto byte:bytes)assert(byte==untouched);}
static bool owner();
static bool ready();
static void refuseNested(){
 Buffer bytes;bytes.fill(untouched);
 assert(!api->idle(nullptr,0) && !api->idle(nullptr,999));
 assert(api->fill(nullptr,999,bytes.data(),8)==RISC_ENTROPY_BUSY);
 unchanged(bytes);
 assert(!NativeEntropy::configure(nullptr,nullptr,nullptr,nullptr));
}
static bool owner(){
 ++ownerCalls;
 if(ownerCalls==reenterOwnerAt)refuseNested();
 if(ownerCalls==loseOwnerAt)owns=false;
 return owns;
}
static bool ready(){
 ++readyCalls;
 if(readyCalls==reenterReadyAt)refuseNested();
 if(readyCalls==loseReadyAt)owns=false;
 return rf && readyCalls!=unavailableAt;
}
extern "C" void esp_fill_random(void* output,size_t size){
 ++rngCalls;
 assert(resourceHeld && owns && rf && size==callerSize && size>=1 && size<=32);
 assert(output!=callerOutput); // Production SDK receives private scratch.
 const auto* original=static_cast<const uint8_t*>(callerOutput);
 for(uint32_t i=0;i<callerSize;++i)assert(original[i]==untouched);
 assert(!api->idle(nullptr,0));
 if(reenterSdk)refuseNested();
 if(concurrentSdk){std::thread foreign(refuseNested);foreign.join();}
 auto* bytes=static_cast<uint8_t*>(output);
 for(size_t i=0;i<size;++i)bytes[i]=static_cast<uint8_t>(0x30+i);
 if(loseInSdk)owns=false;
 if(rfLostInSdk)rf=false;
}
static void reset(){
 // Test-only fresh-boot simulation. Production has no retention reset API.
 auto& s=NativeEntropy::state();assert(!s.busy.load());s.retained.store(0);
 resourceBusy=resourceHeld=false;takeCalls=giveCalls=0;
 owns=rf=true;loseInSdk=rfLostInSdk=reenterSdk=concurrentSdk=false;
 ownerCalls=readyCalls=rngCalls=loseOwnerAt=loseReadyAt=0;
 unavailableAt=reenterOwnerAt=reenterReadyAt=0;
 callerOutput=nullptr;callerSize=0;
 assert(NativeEntropy::configure(owner,ready,take,give));
 assert(api->idle(nullptr,0) && api->safe(nullptr));
 assert(!resourceHeld);
}
static int32_t fill(Buffer& bytes,uint32_t size=32,uint64_t activation=17){
 bytes.fill(untouched);callerOutput=bytes.data()+1;callerSize=size;
 return api->fill(nullptr,activation,callerOutput,size);
}
static void success(const Buffer& bytes,uint32_t size){
 assert(bytes.front()==untouched);
 for(uint32_t i=0;i<size;++i)assert(bytes[i+1]==0x30+i);
 for(size_t i=size+1;i<bytes.size();++i)assert(bytes[i]==untouched);
 assert(api->idle(nullptr,0) && api->safe(nullptr));
}
static void retainedRefusal(Buffer& bytes){
 assert(!api->safe(nullptr) && !api->idle(nullptr,0));
 assert(resourceHeld && !giveCalls);
 owns=true;
 const auto calls=rngCalls;
 assert(fill(bytes)==RISC_ENTROPY_RETAINED);unchanged(bytes);
 assert(rngCalls==calls);
 assert(!NativeEntropy::configure(owner,ready,take,give));
 assert(fill(bytes)==RISC_ENTROPY_RETAINED);unchanged(bytes);
 assert(rngCalls==calls);
}
int main(){
 Buffer bytes;
 // Size extremes, exact SDK lengths and caller canaries for every legal size.
 for(uint32_t size=1;size<=32;++size){
  reset();assert(fill(bytes,size)==RISC_ENTROPY_OK);success(bytes,size);
  assert(rngCalls==1 && readyCalls==2 && ownerCalls==6);
 }
 for(auto size:{0u,33u,UINT32_MAX}){
  reset();assert(fill(bytes,size)==RISC_ENTROPY_INVALID);unchanged(bytes);
  assert(rngCalls==0 && readyCalls==0);
 }
 reset();assert(fill(bytes,32,0)==RISC_ENTROPY_INVALID);unchanged(bytes);
 assert(api->fill(nullptr,17,nullptr,1)==RISC_ENTROPY_INVALID && !rngCalls);
 // Missing owner and calls from the wrong task do not poison future calls.
 reset();assert(NativeEntropy::configure(nullptr,ready,take,give));
 assert(fill(bytes)==RISC_ENTROPY_CONTEXT);unchanged(bytes);
 assert(!rngCalls && api->safe(nullptr));
 assert(NativeEntropy::configure(owner,ready,take,give));owns=false;
 assert(fill(bytes)==RISC_ENTROPY_CONTEXT);unchanged(bytes);owns=true;
 assert(fill(bytes)==RISC_ENTROPY_OK);success(bytes,32);
 // Shared worker/flash ownership is a transient refusal with no RNG/output.
 reset();resourceBusy=true;
 assert(fill(bytes)==RISC_ENTROPY_BUSY);unchanged(bytes);
 assert(takeCalls==1 && giveCalls==0 && !rngCalls && api->safe(nullptr));
 resourceBusy=false;assert(fill(bytes)==RISC_ENTROPY_OK);success(bytes,32);
 assert(takeCalls==2 && giveCalls==1 && !resourceHeld);
 reset();assert(NativeEntropy::configure(owner,ready,nullptr,nullptr));
 assert(fill(bytes)==RISC_ENTROPY_UNAVAILABLE);unchanged(bytes);assert(!rngCalls);
 // No main source means no SDK call, even if boot previously supplied a seed.
 reset();assert(NativeEntropy::configure(owner,nullptr,take,give));
 assert(fill(bytes)==RISC_ENTROPY_UNAVAILABLE);unchanged(bytes);assert(!rngCalls);
 assert(NativeEntropy::configure(owner,ready,take,give));rf=false;
 assert(fill(bytes)==RISC_ENTROPY_UNAVAILABLE);unchanged(bytes);assert(!rngCalls);
 rf=true;assert(fill(bytes)==RISC_ENTROPY_OK);success(bytes,32);
 // RF lost during SDK or its final readiness check never exposes scratch.
 reset();rfLostInSdk=true;
 assert(fill(bytes)==RISC_ENTROPY_UNAVAILABLE);unchanged(bytes);
 assert(rngCalls==1 && api->safe(nullptr));
 assert(fill(bytes)==RISC_ENTROPY_UNAVAILABLE);unchanged(bytes);assert(rngCalls==1);
 rfLostInSdk=false;rf=true;
 assert(fill(bytes)==RISC_ENTROPY_OK);success(bytes,32);
 for(unsigned at=1;at<=2;++at){
  reset();unavailableAt=at;
  assert(fill(bytes)==RISC_ENTROPY_UNAVAILABLE);unchanged(bytes);
  assert(rngCalls==at-1 && api->safe(nullptr));
  unavailableAt=0;assert(fill(bytes)==RISC_ENTROPY_OK);success(bytes,32);
 }
 // Initial owner refusal is CONTEXT; every later observed loss is sticky.
 for(unsigned at=1;at<=6;++at){
  reset();loseOwnerAt=at;
  assert(fill(bytes)==(at==1?RISC_ENTROPY_CONTEXT:RISC_ENTROPY_RETAINED));
  unchanged(bytes);assert(rngCalls==(at>=4?1u:0u));
  if(at==1){assert(api->safe(nullptr));continue;}
  retainedRefusal(bytes);
 }
 for(unsigned at=1;at<=2;++at){
  reset();loseReadyAt=at;
  assert(fill(bytes)==RISC_ENTROPY_RETAINED);unchanged(bytes);
  assert(rngCalls==at-1);retainedRefusal(bytes);
 }
 reset();loseInSdk=true;
 assert(fill(bytes)==RISC_ENTROPY_RETAINED);unchanged(bytes);
 assert(rngCalls==1);retainedRefusal(bytes);
 // Recursion is rejected even inside the initial owner predicate.
 for(unsigned at=1;at<=6;++at){
  reset();reenterOwnerAt=at;
  assert(fill(bytes)==RISC_ENTROPY_OK);success(bytes,32);assert(rngCalls==1);
 }
 for(unsigned at=1;at<=2;++at){
  reset();reenterReadyAt=at;
  assert(fill(bytes)==RISC_ENTROPY_OK);success(bytes,32);assert(rngCalls==1);
 }
 reset();reenterSdk=true;
 assert(fill(bytes)==RISC_ENTROPY_OK);success(bytes,32);assert(rngCalls==1);
 reset();concurrentSdk=true;
 assert(fill(bytes)==RISC_ENTROPY_OK);success(bytes,32);assert(rngCalls==1);
 puts("Native entropy production SDK shim: C ABI, 1..32-byte bounds, private output, RF readiness, owner loss, sticky retention, predicate/SDK reentry and concurrent refusal PASS");
}
