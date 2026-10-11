#pragma once
#include <RiscFailureEvidenceV1.h>
#include <stdint.h>
namespace RiscFailureBacktrace {
#define RISC_BT_INLINE inline __attribute__((always_inline))
RISC_BT_INLINE bool internalRange(uintptr_t address,uint32_t bytes,uint32_t alignment,uintptr_t low,uintptr_t high) {
  return alignment && !(alignment&(alignment-1)) && !(address&(alignment-1)) &&
    low<high && address>=low && address<high && bytes<=high-address;
}
RISC_BT_INLINE bool stackReadable(uint32_t sp,uintptr_t low,uintptr_t high) {
  return sp>=16 && !(sp&15) && internalRange(uintptr_t(sp)-16,16,4,low,high);
}
struct Cursor {uint32_t pc,sp,next_pc;};
// next may read exactly the SDK's two words below the *current* SP. It is never
// called before checking that range. No unbounded traversal or pointer chasing.
template<class Next,class Normalize>
RISC_BT_INLINE uint32_t collect(Cursor cursor,bool firstPc,uintptr_t low,uintptr_t high,
                                risc_failure_frame_v1 (&frames)[8],uint32_t& stop,
                                Next next,Normalize normalize) {
  stop=RISC_FAILURE_STACK_UNSUPPORTED;
  if(!stackReadable(cursor.sp,low,high))return 0;
  if(!firstPc){stop=RISC_FAILURE_STACK_INVALID;return 0;}
  uint32_t count=0;
  frames[count++]={normalize(cursor.pc),cursor.sp};
  stop=RISC_FAILURE_STACK_COMPLETE;
  while(cursor.next_pc && count<8) {
    if(!stackReadable(cursor.sp,low,high)){stop=RISC_FAILURE_STACK_UNSUPPORTED;break;}
    const uint32_t prior=cursor.sp;
    if(!next(cursor) || cursor.sp<=prior){stop=RISC_FAILURE_STACK_INVALID;break;}
    if(!stackReadable(cursor.sp,low,high)){stop=RISC_FAILURE_STACK_UNSUPPORTED;break;}
    frames[count++]={normalize(cursor.pc),cursor.sp};
  }
  if(count==8 && cursor.next_pc)stop=RISC_FAILURE_STACK_LIMIT;
  return count;
}
#undef RISC_BT_INLINE
}
