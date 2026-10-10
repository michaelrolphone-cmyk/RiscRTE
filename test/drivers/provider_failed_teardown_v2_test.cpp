#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstdio>

int main(int argc, char **argv) {
  assert(argc == 2);
  RuntimeProviders::GraphV2 graph;
  char names[RuntimeProviders::GraphV2::kMaxModules-1][32]{};
  for(size_t i=0;i+1<RuntimeProviders::GraphV2::kMaxModules;++i){
    std::snprintf(names[i],sizeof(names[i]),"unused-%zu",i);
    assert(graph.addVerified({names[i],argv[1],"cap.unused",1,nullptr,0}));
  }
  assert(graph.addVerified({"fixture-retry", argv[1], "cap.retry", 1,
                            nullptr, 0}));
  assert(graph.moduleCount()==RuntimeProviders::GraphV2::kMaxModules);
  RuntimeProviders::GrantV2 full[RuntimeProviders::GraphV2::kMaxGrants]{};
  for(auto& token:full){token=graph.acquire("cap.retry",1);assert(token.slot);}
  assert(!graph.acquire("cap.retry",1).slot);
  for(size_t i=0;i+1<RuntimeProviders::GraphV2::kMaxGrants;++i)assert(graph.release(full[i]));
  const auto grant=full[RuntimeProviders::GraphV2::kMaxGrants-1];
  assert(grant.slot==RuntimeProviders::GraphV2::kMaxGrants);
  assert(grant.slot && graph.interfaceFor(grant));
  // First quiesce fails after the consumer is revoked. Preserve this grant's
  // generation for retry, but never expose an ELF pointer through it again.
  assert(!graph.release(grant));
  assert(graph.liveGrants()==1 && graph.peakLiveGrants()==RuntimeProviders::GraphV2::kMaxGrants);
  assert(graph.holdsGrant(grant)); // Pending release still occupies capacity.
  assert(!graph.holdsGrant({grant.slot,grant.generation+1}));
  assert(!graph.interfaceFor(grant) && graph.liveGrants() == 1);
  assert(!graph.shutdown()); // A caller MUST reconcile a failed release.
  assert(!graph.acquire("cap.retry", 1).slot); // No regrant while quarantined.
  assert(!graph.addVerified({"unsafe", argv[1], "cap.other", 1, nullptr, 0}));
  assert(graph.release(grant)); // Second quiesce succeeds; unload only NOW.
  assert(graph.liveGrants()==0 && graph.peakLiveGrants()==RuntimeProviders::GraphV2::kMaxGrants);
  assert(!graph.holdsGrant(grant)); // A historical token is not occupancy.
  assert(!graph.release(grant)); // Idempotence must not double-unpin a consumer.
  assert(graph.liveGrants() == 0 && graph.shutdown());
  assert(graph.shutdown());
  const auto again = graph.acquire("cap.retry", 1);
  assert(again.slot && again.generation != grant.generation);
  assert(!graph.interfaceFor(grant));
  // Reloading the test ELF resets the fail-once quiesce state.
  assert(!graph.release(again));
  assert(!graph.interfaceFor(again) && graph.liveGrants() == 1);
  assert(!graph.shutdown());
  assert(graph.release(again));
  assert(graph.shutdown());
  std::puts("Active teardown quarantine: retryable generation, no regrant, recovery PASS");
}
