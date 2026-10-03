#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
  assert(argc == 3);
  const RuntimeProviders::RequirementV2 required[] = {{"cap.root", 1}};
  RuntimeProviders::GraphV2 graph;
  assert(graph.addVerified({"fixture-root", argv[1], "cap.root", 1, nullptr, 0}));
  assert(graph.addVerified({"fixture-failed-start", argv[2],
                            "cap.failed-start", 1, required, 1}));
  // The failed start acquired a simulated hardware resource. Its first two
  // quiescence attempts fail: load() and activate() cleanup both retain it.
  const auto failed = graph.acquire("cap.failed-start", 1);
  assert(!failed.slot && !graph.interfaceFor(failed));
  assert(graph.liveGrants() == 0);
  assert(std::strcmp(graph.lastError(),
      "fixture-failed-start: usb-host-install rc=259 (0x103)") == 0);
  assert(!graph.acquire("cap.failed-start", 1).slot);
  assert(std::strstr(graph.lastError(), "usb-host-install rc=259 (0x103)"));
  assert(!graph.shutdown()); // Cannot unmap or release the dependency yet.
  assert(!graph.addVerified({"unsafe-edit", argv[1], "cap.edit", 1, nullptr, 0}));

  // The lower provider is still pinned and has NOT been silently unloaded.
  auto root = graph.acquire("cap.root", 1);
  assert(root.slot);
  auto *gate = const_cast<int *>(static_cast<const int *>(graph.interfaceFor(root)));
  assert(gate && *gate == 42);
  assert(graph.release(root));
  assert(!graph.interfaceFor(root));
  assert(!graph.shutdown()); // Repeated failed recovery leaves dependency live.

  // Simulate explicit repair of the physical condition using the still-pinned
  // dependency. No failed provider API was ever handed to an app.
  root = graph.acquire("cap.root", 1);
  assert(root.slot && graph.interfaceFor(root) == gate);
  *gate = 43;
  assert(graph.release(root));
  assert(graph.shutdown()); // Retried quiesce, stop, dlclose, then unpin root.
  assert(graph.liveGrants() == 0);
  assert(graph.shutdown()); // Idempotent without double stop or double unpin.

  // A genuinely unloaded ELF reinitializes its static data on reload. The
  // test must NOT expect the artificial repair flag to persist after unmap.
  root = graph.acquire("cap.root", 1);
  assert(root.slot && *static_cast<const int *>(graph.interfaceFor(root)) == 42);
  assert(graph.release(root) && graph.shutdown());
  std::puts("Failed-start hardware recovery: retry, dependency pin and no stale grants PASS");
}
