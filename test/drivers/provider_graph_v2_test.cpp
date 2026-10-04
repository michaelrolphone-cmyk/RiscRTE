#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstring>

using RuntimeProviders::GraphV2;
using RuntimeProviders::RequirementV2;
using RuntimeProviders::SpecV2;

int main(int argc, char** argv) {
  assert(argc == 5);
  const RequirementV2 needsRoot[] = {{"cap.root", 1}};
  const SpecV2 root = {"fixture-root", argv[1], "cap.root", 1, nullptr, 0};
  const SpecV2 child = {"fixture-child", argv[2], "cap.child", 1, needsRoot, 1};
  const SpecV2 other = {"fixture-other", argv[3], "cap.other", 1, needsRoot, 1};
  const SpecV2 alternate = {"fixture-root-alt", argv[4], "cap.root", 1, nullptr, 0};

  GraphV2 graph;
  assert(graph.addVerified(root) && graph.addVerified(child));
  assert(graph.moduleCount() == 2);
  assert(!graph.addVerified(root));
  assert(!graph.acquire("cap.unknown", 1).slot);
  assert(!graph.acquire("cap.child", 2).slot);
  auto childGrant = graph.acquire("cap.child", 1);
  assert(childGrant.slot && graph.interfaceFor(childGrant));
  assert(*static_cast<const int*>(graph.interfaceFor(childGrant)) == 42);
  // Lazy installed-provider admission must be append-only and safe while the
  // already loaded dependency chain and its consumer grant remain live.
  assert(graph.addVerified(other));
  assert(graph.moduleCount() == 3);
  auto otherGrant = graph.acquire("cap.other", 1);
  assert(otherGrant.slot && graph.liveGrants() == 2);
  assert(!graph.shutdown());
  assert(graph.release(childGrant));
  assert(!graph.release(childGrant) && !graph.interfaceFor(childGrant));
  auto replacement = graph.acquire("cap.child", 1);
  assert(replacement.slot && replacement.generation != childGrant.generation);
  assert(!graph.interfaceFor(childGrant));
  assert(graph.release(otherGrant) && graph.interfaceFor(replacement));
  assert(graph.release(replacement) && graph.shutdown());
  assert(graph.liveGrants() == 0);
  auto again = graph.acquire("cap.child", 1);
  assert(again.slot && graph.release(again) && graph.shutdown());

  GraphV2 capacity;
  assert(capacity.addVerified(root));
  RuntimeProviders::GrantV2 grants[GraphV2::kMaxGrants];
  for (size_t i = 0; i < GraphV2::kMaxGrants; ++i) {
    grants[i] = capacity.acquire("cap.root", 1);
    assert(grants[i].slot);
  }
  assert(!capacity.acquire("cap.root", 1).slot);
  assert(std::strstr(capacity.lastError(), "Grant table full"));
  for (auto grant : grants) assert(capacity.release(grant));
  assert(capacity.shutdown());

  // Multiple providers of the same capability MUST NOT be rejected at
  // installation or silently selected based on their install order.
  GraphV2 competing;
  assert(competing.addVerified(root));
  assert(competing.addVerified(alternate));
  assert(competing.addVerified(child));
  assert(competing.moduleCount() == 3);
  // Enumeration must inspect admitted metadata only. No fixture ELF is
  // opened and no capability is granted before explicit acquireFrom().
  assert(competing.liveGrants() == 0);
  assert(std::strcmp(competing.matchingProviderId(0, "cap.root", 1),
                     "fixture-root") == 0);
  assert(std::strcmp(competing.matchingProviderId(1, "cap.root", 1),
                     "fixture-root-alt") == 0);
  assert(!competing.matchingProviderId(2, "cap.root", 1));
  assert(!competing.matchingProviderId(GraphV2::kMaxModules, "cap.root", 1));
  assert(!competing.matchingProviderId(0, "cap.root", 2));
  assert(!competing.matchingProviderId(0, nullptr, 1));
  assert(!competing.matchingProviderId(0, "cap.root", 0));
  assert(std::strcmp(competing.matchingProviderId(2, "cap.child", 1),
                     "fixture-child") == 0);
  assert(competing.liveGrants() == 0);
  assert(!competing.acquire("cap.root", 1).slot);
  assert(!competing.acquire("cap.child", 1).slot); // Ambiguous dependency.
  assert(!competing.acquireFrom("missing", "cap.root", 1).slot);
  assert(!competing.acquireFrom("fixture-root", "cap.root", 2).slot);
  auto firstProvider = competing.acquireFrom("fixture-root", "cap.root", 1);
  auto secondProvider = competing.acquireFrom("fixture-root-alt", "cap.root", 1);
  assert(firstProvider.slot && secondProvider.slot);
  assert(*static_cast<const int*>(competing.interfaceFor(firstProvider)) == 42);
  assert(*static_cast<const int*>(competing.interfaceFor(secondProvider)) == 42);
  assert(!competing.shutdown());
  assert(competing.release(firstProvider));
  assert(competing.release(secondProvider));
  assert(competing.shutdown());

  GraphV2 missing;
  assert(missing.addVerified(child));
  assert(!missing.acquire("cap.child", 1).slot && missing.shutdown());
  assert(std::strstr(missing.lastError(), "cap.root"));

  const RequirementV2 needsB[] = {{"cycle.b", 1}};
  const RequirementV2 needsA[] = {{"cycle.a", 1}};
  GraphV2 cycle;
  assert(cycle.addVerified({"cycle-a", argv[1], "cycle.a", 1, needsB, 1}));
  assert(cycle.addVerified({"cycle-b", argv[2], "cycle.b", 1, needsA, 1}));
  assert(!cycle.acquire("cycle.a", 1).slot && cycle.shutdown());
  assert(std::strstr(cycle.lastError(), "Dependency cycle"));

  GraphV2 mismatch;
  assert(mismatch.addVerified({"not-the-ELF-id", argv[1], "cap.root", 1, nullptr, 0}));
  assert(!mismatch.acquire("cap.root", 1).slot);
  assert(std::strstr(mismatch.lastError(), "not-the-ELF-id: elf-interface-or-identity"));
  assert(mismatch.shutdown());

  const RequirementV2 duplicateRequirements[] = {{"cap.root", 1}, {"cap.root", 2}};
  GraphV2 invalid;
  assert(!invalid.addVerified({"invalid", argv[1], "cap.invalid", 1,
                               duplicateRequirements, 2}));
  assert(!invalid.addVerified({"invalid", "relative/path", "cap.invalid", 1,
                               nullptr, 0}));
  std::puts("Generic graph: metadata enumeration, dependencies, competing provider choice, cycle detection and grants PASS");
}
