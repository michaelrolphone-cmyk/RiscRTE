#include "runtime/resources/ScopedBufferWipe.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <type_traits>
static_assert(!std::is_copy_constructible<RiscRuntime::ScopedBufferWipe>::value,"unique scope owner");
static_assert(!std::is_copy_assignable<RiscRuntime::ScopedBufferWipe>::value,"unique scope owner");
static int leave(uint8_t (&bytes)[64],int path){
 RiscRuntime::ScopedBufferWipe wipe(bytes);
 memset(bytes,0xa5,sizeof(bytes));
 // Model each transport exit: missing data, malformed/error, short destination,
 // readback mismatch, or success. All scratch bytes, not just copied bytes, wipe.
 if(path==0)return -1;
 if(path==1)return -2;
 if(path==2)return -3;
 if(path==3)return -4;
 return 0;
}
int main(){
 struct Guarded {uint8_t before=0x12,bytes[64]{},after=0x34;} guarded;
 for(int path=0;path<5;++path){
  (void)leave(guarded.bytes,path);
  for(uint8_t byte:guarded.bytes)assert(!byte);
  assert(guarded.before==0x12 && guarded.after==0x34);
 }
 uint8_t one[1]={0x55};{RiscRuntime::ScopedBufferWipe wipe(one);}assert(!one[0]);
 puts("Volatile scratch wipe: full fixed buffer, all early/success exits, bounds and unique scope ownership PASS");
}
