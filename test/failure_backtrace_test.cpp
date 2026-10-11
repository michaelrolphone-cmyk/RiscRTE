#include "runtime/diagnostics/FailureBacktrace.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace RiscFailureBacktrace;
int main(){
 constexpr uint32_t low=0x3fc88000,high=0x3fcf8000;
 assert(internalRange(low,112,4,low,high));
 assert(!internalRange(high-4,112,4,low,high));
 assert(!internalRange(UINTPTR_MAX-3,112,4,low,high));
 assert(!internalRange(low+1,112,4,low,high));
 assert(!internalRange(low,112,3,low,high));
 risc_failure_frame_v1 frames[8];uint32_t stop=0,calls=0;
 auto normalize=[](uint32_t pc){return pc-3;};
 auto next=[&](Cursor& c){++calls;c.pc=c.next_pc;c.sp+=16;c.next_pc+=4;return true;};
 for(uint32_t sp:{0u,15u,low,low+17,0x3d000010u,0x600fe010u,high+16}){
  calls=0;assert(collect({42,sp,44},true,low,high,frames,stop,next,normalize)==0);assert(!calls && stop==RISC_FAILURE_STACK_UNSUPPORTED);
 }
 calls=0;assert(collect({42,low+16,44},false,low,high,frames,stop,next,normalize)==0);assert(!calls && stop==RISC_FAILURE_STACK_INVALID);
 calls=0;assert(collect({42,low+16,44},true,low,high,frames,stop,next,normalize)==8);assert(calls==7 && stop==RISC_FAILURE_STACK_LIMIT && frames[0].pc==39);
 calls=0;assert(collect({42,low+16,0},true,low,high,frames,stop,next,normalize)==1);assert(!calls && stop==RISC_FAILURE_STACK_COMPLETE);
 calls=0;auto terminal=[&](Cursor& c){++calls;c.pc=c.next_pc;c.sp+=16;c.next_pc=0;return true;};
 assert(collect({42,low+16,44},true,low,high,frames,stop,terminal,normalize)==2);assert(calls==1 && stop==RISC_FAILURE_STACK_COMPLETE);
 calls=0;auto backward=[&](Cursor& c){++calls;c.sp-=16;return true;};
 assert(collect({42,low+32,44},true,low,high,frames,stop,backward,normalize)==1);assert(calls==1 && stop==RISC_FAILURE_STACK_INVALID);
 calls=0;auto cyclic=[&](Cursor&){++calls;return true;};
 assert(collect({42,low+32,44},true,low,high,frames,stop,cyclic,normalize)==1);assert(calls==1 && stop==RISC_FAILURE_STACK_INVALID);
 calls=0;auto unsupported=[&](Cursor& c){++calls;c.sp=0x600fe010;return true;};
 assert(collect({42,low+32,44},true,low,high,frames,stop,unsupported,normalize)==1);assert(calls==1 && stop==RISC_FAILURE_STACK_UNSUPPORTED);
 calls=0;auto invalid=[&](Cursor&){++calls;return false;};
 assert(collect({42,low+32,44},true,low,high,frames,stop,invalid,normalize)==1);assert(calls==1 && stop==RISC_FAILURE_STACK_INVALID);
 std::puts("Failure backtrace guarded traversal PASS");
}
