#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>

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
  // Holding a separate host dlopen reference allows inspection of the REAL
  // loaded fixture without peeking into private GraphV2 internals.
  void* probe = dlopen(argv[2], RTLD_NOW);
  assert(probe);
  auto valid = reinterpret_cast<int (*)()>(dlsym(probe, "retained_dependency_is_valid"));
  assert(valid);
  churnStack(12);
  assert(valid()); // ASan stack-use-after-return if graph passed local deps[].
  assert(graph.release(grant)); // quiesce() and stop() both re-read the table.
  assert(graph.shutdown());
  assert(dlclose(probe) == 0);
  std::puts("Provider dependency table survives activation stack, quiesce and stop PASS");
}
