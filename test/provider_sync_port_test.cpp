#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include <cassert>
#include <cstdio>
using namespace RiscCpu;
static bool owned=true;
int main(){
 Hardware hw{};hw.owner=[](){return owned;};
 Port p(hw);p.syncCount_=2;
 auto& a=p.syncs_[0];a.port=&p;a.instance=1;
 auto& b=p.syncs_[1];b.port=&p;b.instance=2;
 assert(Port::syncOwner(&a) && !Port::syncOwner(nullptr));
 uint64_t first=99,second=99,foreign=99;
 owned=false;assert(!Port::syncCreate(&a,&first) && !first);owned=true;
 assert(!Port::syncCreate(&a,nullptr));
 assert(Port::syncCreate(&a,&first) && first);
 assert(Port::syncCreate(&a,&second) && second>first);
 uint64_t overflow=99;assert(!Port::syncCreate(&a,&overflow) && !overflow);
 assert(Port::syncCreate(&b,&foreign) && foreign>second);
 assert(!Port::syncTryLock(&a,0) && !Port::syncTryLock(&a,foreign));
 assert(!Port::syncDestroy(&b,first) && !Port::syncUnlock(&b,first));
 assert(!p.quiescent() && p.providerStorageSafe() && p.appExitSafe());
 assert(Port::syncTryLock(&a,first));
 assert(!Port::syncTryLock(&a,first) && !Port::syncDestroy(&a,first));
 assert(!p.providerStorageSafe() && !p.appExitSafe() && !p.restartResourcesSafe());
 // Attempts from another task do not read or mutate a held provider lock.
 owned=false;
 assert(!Port::syncOwner(&a) && !Port::syncUnlock(&a,first));
 assert(!Port::syncDestroy(&a,first) && !Port::syncTryLock(&a,second));
 assert(a.locks[0].held);owned=true;
 // A fenced port rejects new work but permits owner-only bounded cleanup.
 p.poisoned_=true;
 assert(!Port::syncOwner(&a) && !Port::syncTryLock(&a,second));
 assert(Port::syncUnlock(&a,first) && !Port::syncUnlock(&a,first));
 assert(Port::syncDestroy(&a,first));p.poisoned_=false;
 uint64_t replacement=0;assert(Port::syncCreate(&a,&replacement) && replacement>foreign);
 assert(!Port::syncTryLock(&a,first) && !Port::syncDestroy(&a,first));
 assert(Port::syncTryLock(&a,replacement));
 p.sleeping_=true;assert(!Port::syncUnlock(&a,replacement));p.sleeping_=false;
 p.sleepRetained_=true;assert(!Port::syncTryLock(&a,second));
 assert(Port::syncUnlock(&a,replacement) && Port::syncDestroy(&a,replacement));p.sleepRetained_=false;
 assert(Port::syncDestroy(&a,second) && Port::syncDestroy(&b,foreign));
 assert(p.quiescent() && p.appExitSafe());
 p.serial_=UINT64_MAX;overflow=99;
 assert(!Port::syncCreate(&a,&overflow) && !overflow && p.quiescent());
 // Every inherited provider slot is reachable, including the new last slot.
 // Tokens remain scoped to their exact table; the last lock fences the port.
 Port full(hw);full.syncCount_=RuntimeProviders::GraphV2::kMaxModules;
 uint64_t tokens[RuntimeProviders::GraphV2::kMaxModules]{};
 for(size_t i=0;i<full.syncCount_;++i) {
  auto& sync=full.syncs_[i];sync.port=&full;sync.instance=i+1;
  assert(Port::syncCreate(&sync,&tokens[i]) && tokens[i]);
 }
 auto& last=full.syncs_[full.syncCount_-1];
 assert(!Port::syncTryLock(&last,tokens[0]));
 assert(Port::syncTryLock(&last,tokens[full.syncCount_-1]));
 assert(!full.appExitSafe() && !full.providerStorageSafe());
 assert(Port::syncUnlock(&last,tokens[full.syncCount_-1]));
 for(size_t i=0;i<full.syncCount_;++i)assert(Port::syncDestroy(&full.syncs_[i],tokens[i]));
 assert(full.quiescent() && full.appExitSafe());
 puts("Provider sync: fixed capacity, owner-only, nonrecursive, scoped/stale tokens, retained cleanup and lifecycle barriers PASS");
}
