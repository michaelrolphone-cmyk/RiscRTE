#include "runtime/resources/AppAllocationLedger.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <unordered_set>
using RuntimeResources::AppAllocationLedger;
static std::unordered_set<void*> live;
static bool failAllocation=false;
static unsigned yields=0;
void* alloc(size_t n,uint32_t) { if(failAllocation) return nullptr; auto p=std::malloc(n); assert(p); live.insert(p); return p; }
void* resize(void* p,size_t n,uint32_t) {
  if(failAllocation) return nullptr;
  assert(live.erase(p)==1); auto q=std::realloc(p,n); assert(q); live.insert(q); return q;
}
void release(void* p) { assert(live.erase(p)==1); std::free(p); }
void yield() { ++yields; }
int main() {
  AppAllocationLedger ledger;
  AppAllocationLedger::Entry entries[128];
  assert(!ledger.allocate(1));
  // Simulate changing between three heavy apps, plus leaky small app visits.
  for(unsigned round=0;round<100;++round) {
    ledger.begin(entries,128,{alloc,resize,release,yield});
    auto p=ledger.allocate(1024*1024,1); assert(p);
    auto z=static_cast<unsigned char*>(ledger.calloc(16,8)); assert(z);
    for(unsigned i=0;i<128;++i) assert(z[i]==0);
    failAllocation=true;
    assert(!ledger.resize(p,2*1024*1024)); assert(ledger.count()==2);
    assert(!ledger.allocate(55)); failAllocation=false;
    p=ledger.resize(p,4096); assert(p);
    assert(ledger.release(p)); assert(!ledger.release(p));
    assert(!ledger.calloc(SIZE_MAX,2));
    for(unsigned i=1;i<128;++i) assert(ledger.allocate(7));
    assert(!ledger.allocate(7));
    assert(ledger.bytes()==128+127*7);
    // Forgotten app allocations must all be reclaimed automatically.
    ledger.end(); ledger.end();
    assert(live.empty() && ledger.count()==0 && ledger.bytes()==0);
  }
  assert(yields==200);
  puts("app allocation ownership: repeated exits, failed realloc, overflow, capacity, idempotence PASS");
}
