#include "runtime/memory/PsramCache.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <utility>
#include <limits>
static bool task=true,supported=true,mapped=true,fail=false;
static int64_t now=0,step=0;static unsigned yields=0;static bool rollback=false;
static std::vector<std::pair<uintptr_t,size_t>> calls;
static RiscMemory::CacheBackend backend{[](){return task;},[](){return supported;},
 [](uintptr_t,size_t){return mapped;},[](){return now;},
 [](uintptr_t p,size_t n){calls.emplace_back(p,n);now+=step;if(rollback)now=-1;return !fail;},
 [](){++yields;now+=1000;},0x3c000000,0x3e000000};
static void reset(){task=supported=mapped=true;fail=rollback=false;now=step=0;yields=0;calls.clear();}
static int32_t run(size_t n=4096,uintptr_t p=0x3c000000){return RiscMemory::writeback(backend,p,n);}
int main(){
 reset();assert(run()==0&&calls.size()==1&&!yields);
 reset();assert(run(8193)==0&&calls.size()==3&&calls[2].second==1&&calls[2].first==0x3c002000);
 reset();assert(run(1024*1024)==0&&calls.size()==256&&yields==31);
 reset();assert(run(1024*1024+1)==-1&&calls.empty());assert(run(0)==-1);assert(run(1,0x3bffffff)==-1);assert(run(1,0x3e000000)==-1);
 assert(run(2,0x3dffffff)==-1);assert(run(4096,std::numeric_limits<uintptr_t>::max()-4)==-1);assert(calls.empty());
 reset();assert(run(1,0x3dffffff)==0);
 reset();mapped=false;assert(run()==-1&&calls.empty());
 reset();task=false;assert(run()==-2&&calls.empty());
 reset();supported=false;assert(run()==-3&&calls.empty());
 reset();fail=true;assert(run(8192)==-4&&calls.size()==1);
 reset();step=100000;assert(run(8192)==-5&&calls.size()==1); // no second SDK call
 reset();step=2000;assert(run(8192)==0&&calls.size()==2&&yields==1);
 reset();step=99000;assert(run(8192)==-5&&calls.size()==1&&yields==1); // yield reaches deadline
 reset();rollback=true;assert(run()==-5&&calls.size()==1);
 reset();now=-1;assert(run()==-5&&calls.empty());
 reset();step=200000;assert(run()==-5&&now==200000); // actual SDK stall is NOT preempted
 puts("15 PSRAM range/chunk/cooperative-time controls passed; no DMA visibility claim");
}
