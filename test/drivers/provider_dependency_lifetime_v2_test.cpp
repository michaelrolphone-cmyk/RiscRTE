#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace RuntimeProviders;

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void churnStack(unsigned depth) {
  volatile unsigned char overwrite[1024];
  for (size_t i = 0; i < sizeof(overwrite); ++i)
    overwrite[i] = static_cast<unsigned char>(i + depth);
  if (depth) churnStack(depth - 1);
  if (overwrite[depth & 1023u] == 255) std::puts("stack churn");
}

int main(int argc, char** argv) {
  assert(argc == 3);
  const RequirementV2 needsRoot[] = {{"cap.root", 1}};
  const SpecV2 root{"fixture-root", argv[1], "cap.root", 1, nullptr, 0};
  const SpecV2 child{"fixture-retaining", argv[2], "cap.retaining", 1,
                     needsRoot, 1};
  GraphV2 graph;
  assert(graph.addVerified(root) && graph.addVerified(child));
  auto grant = graph.acquire("cap.retaining", 1);
  assert(grant.slot && graph.interfaceFor(grant));
  // Inspect the exact instance granted by the graph. A separate dlopen of
  // the source path would inspect another image with unrelated static state.
  struct RetainingApi { int (*dependency_is_valid)(); };
  const auto* api = static_cast<const RetainingApi*>(graph.interfaceFor(grant));
  assert(api && api->dependency_is_valid);
  churnStack(12);
  assert(api->dependency_is_valid()); // ASan stack-use-after-return for local deps[].
  assert(graph.release(grant)); // quiesce() and stop() both re-read the table.
  assert(graph.shutdown());
  std::puts("Provider dependency table survives activation stack, quiesce and stop PASS");
}
