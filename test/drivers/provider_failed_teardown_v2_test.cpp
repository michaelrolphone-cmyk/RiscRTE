#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstdio>

int main(int argc, char **argv) {
  assert(argc == 2);
  RuntimeProviders::GraphV2 graph;
  assert(graph.addVerified({"fixture-retry", argv[1], "cap.retry", 1,
                            nullptr, 0}));
  const auto grant = graph.acquire("cap.retry", 1);
  assert(grant.slot && graph.interfaceFor(grant));
  // First quiesce fails after the consumer is revoked. Preserve this grant's
  // generation for retry, but never expose an ELF pointer through it again.
  assert(!graph.release(grant));
  assert(!graph.interfaceFor(grant) && graph.liveGrants() == 1);
  assert(!graph.shutdown()); // A caller MUST reconcile a failed release.
  assert(!graph.acquire("cap.retry", 1).slot); // No regrant while quarantined.
  assert(!graph.addVerified({"unsafe", argv[1], "cap.other", 1, nullptr, 0}));
  assert(graph.release(grant)); // Second quiesce succeeds; unload only NOW.
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
