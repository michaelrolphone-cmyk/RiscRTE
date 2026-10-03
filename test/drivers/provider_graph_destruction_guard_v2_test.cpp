#include "runtime/drivers/ProviderGraphV2.h"
#include <cassert>
#include <csignal>
#include <cstdio>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char** argv) {
  assert(argc == 2);
  // A deliberately non-quiescing ELF must remain mapped. The manager should
  // retain/quarantine the graph. If it instead destroys it, fail stop rather
  // than return to execution with dangling retained dependency/interface data.
  rlimit noCore{};
  assert(setrlimit(RLIMIT_CORE, &noCore) == 0);
  const pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    {
      RuntimeProviders::GraphV2 graph;
      assert(graph.addVerified({"fixture-stuck", argv[1], "cap.stuck", 1,
                                nullptr, 0}));
      auto grant = graph.acquire("cap.stuck", 1);
      assert(grant.slot && graph.interfaceFor(grant));
      assert(!graph.release(grant));
      assert(!graph.shutdown());
      // Scope exit MUST abort: retaining the ELF while freeing the graph's
      // own dependency table is a use-after-lifetime safety violation.
    }
    _exit(100); // A silently returning destructor makes the parent fail.
  }
  int status = 0;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  std::puts("Provider graph destruction: failed quiescence fails stop, never dangling-table return PASS");
}
