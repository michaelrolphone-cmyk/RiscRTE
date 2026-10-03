#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstdio>

// Exercise the actual graph and mapped provider fixture, not a mocked
// recoverFailedFrom implementation. A faulted class's dependency is shared
// with another live provider and global shutdown is forbidden.
int main(int argc, char** argv) {
  assert(argc == 4);
  using RuntimeProviders::GraphV2;
  using RuntimeProviders::RequirementV2;
  const RequirementV2 root[] = {{"cap.root", 1}};
  GraphV2 graph;
  assert(graph.addVerified({"fixture-root", argv[1], "cap.root", 1, nullptr, 0}));
  assert(graph.addVerified({"fixture-failed-start", argv[2],
                            "cap.failed-start", 1, root, 1}));
  assert(graph.addVerified({"fixture-other", argv[3], "cap.other", 1, root, 1}));

  const auto unrelated = graph.acquireFrom("fixture-other", "cap.other", 1);
  assert(unrelated.slot && graph.interfaceFor(unrelated));
  const void* unrelatedInterface = graph.interfaceFor(unrelated);
  const auto failed = graph.acquireFrom("fixture-failed-start", "cap.failed-start", 1);
  assert(!failed.slot && graph.liveGrants() == 1);
  assert(!graph.recoverFailedFrom("fixture-failed-start", "cap.failed-start", 1));
  assert(graph.interfaceFor(unrelated) == unrelatedInterface);
  assert(!graph.recoverFailedFrom("fixture-other", "cap.other", 1));
  assert(!graph.recoverFailedFrom("fixture-failed-start", "cap.failed-start", 2));

  // Repair only the simulated physical quiescence condition. The failed
  // provider's original dependency stays mapped until checked recovery.
  const auto rootLease = graph.acquireFrom("fixture-root", "cap.root", 1);
  assert(rootLease.slot);
  auto* gate = const_cast<int*>(static_cast<const int*>(graph.interfaceFor(rootLease)));
  assert(gate && *gate == 42);
  *gate = 43;
  assert(graph.release(rootLease));
  assert(graph.recoverFailedFrom("fixture-failed-start", "cap.failed-start", 1));
  assert(graph.recoverFailedFrom("fixture-failed-start", "cap.failed-start", 1));
  assert(graph.interfaceFor(unrelated) == unrelatedInterface);
  assert(graph.liveGrants() == 1); // No global shutdown or grant revocation.

  assert(graph.release(unrelated));
  assert(graph.shutdown());
  std::puts("Exact-provider grantless recovery retains unrelated live grants: PASS");
}
