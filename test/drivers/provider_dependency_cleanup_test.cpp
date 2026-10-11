#include "runtime/drivers/ProviderGraphV2.h"
#include "runtime/streams/ProviderQueueHost.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>

using namespace RuntimeProviders;
using namespace RuntimeStreams;
namespace {
struct Counts { unsigned starts=0, quiesces=0, stops=0, opens=0, closes=0; } counts[4];
const void* mappings[4]{};
bool repaired = false;
bool mapped(unsigned i) { Dl_info info{}; return mappings[i] && dladdr(mappings[i], &info); }
}
extern "C" void dependency_cleanup_event(unsigned slot, const char* event) {
  assert(slot < 4);
  auto& c = counts[slot];
  if (!std::strcmp(event, "start")) ++c.starts;
  else if (!std::strcmp(event, "quiesce")) ++c.quiesces;
  else if (!std::strcmp(event, "stop")) ++c.stops;
  else if (!std::strcmp(event, "open")) ++c.opens;
  else if (!std::strcmp(event, "close")) ++c.closes;
  else assert(false);
}
extern "C" void dependency_cleanup_mapping(unsigned slot, const void* mapping) {
  assert(slot < 4); mappings[slot] = mapping;
}
extern "C" bool dependency_cleanup_ready(unsigned slot) { return slot != 1 || repaired; }

static void verify(char** paths, bool recoverLeafFirst) {
  for (auto& c : counts) c = {};
  for (auto& p : mappings) p = nullptr;
  repaired = false;
  GraphV2 graph(runtimeProviderStreamHost());
  const RequirementV2 middleNeeds[] = {{"cap.cleanup-leaf", 1}};
  const RequirementV2 streamNeeds[] = {{"cap.cleanup-safe", 1}, {"cap.cleanup-middle", 1}};
  assert(graph.addVerified({"cleanup-safe", paths[0], "cap.cleanup-safe", 1, nullptr, 0}));
  assert(graph.addVerified({"cleanup-leaf", paths[1], "cap.cleanup-leaf", 1, nullptr, 0}));
  assert(graph.addVerified({"cleanup-middle", paths[2], "cap.cleanup-middle", 1, middleNeeds, 1}));
  assert(graph.addVerified({"cleanup-stream", paths[3], "cap.cleanup-stream", 1, streamNeeds, 2}));
  const auto grant = graph.acquire("cap.cleanup-stream", 1);
  assert(grant.slot && graph.interfaceFor(grant));
  uint64_t context = 0;
  const auto* adapter = graph.streamSessionsFor(grant, &context);
  assert(adapter && context);
  risc_provider_stream_session_v1 opened{};
  opened.struct_size = sizeof(opened);
  assert(graph.beginStreamCallback());
  assert(adapter->open(nullptr, 0, 1000, &opened) == RISC_STREAM_OK);
  graph.endStreamCallback();
  const uint64_t lease = (uint64_t(grant.generation) << 32) | grant.slot;
  assert(reserveEndpointPair(context, lease, 77, 123, opened.rx_endpoint, opened.tx_endpoint) == RISC_STREAM_OK);
  assert(graph.grantStream(grant, 77, opened.rx_endpoint, RISC_STREAM_READ));
  assert(graph.grantStream(grant, 77, opened.tx_endpoint, RISC_STREAM_WRITE));

  // Exactly the broker's checked sequence: revoke queues, close the physical
  // session once, retire its queues, then release the actual graph grant.
  assert(graph.revokeStreamGrants(grant));
  assert(graph.beginStreamCallback());
  assert(adapter->close(opened.session, 1000) == RISC_STREAM_OK);
  graph.endStreamCallback();
  assert(releaseEndpointPair(context, lease, 77, 123, opened.rx_endpoint, opened.tx_endpoint) == RISC_STREAM_OK);
  assert(counts[3].closes == 1);
  assert(!graph.release(grant));
  assert(graph.liveGrants() == 1 && !graph.interfaceFor(grant) && !graph.activationSafe());
  assert(std::strstr(graph.lastError(), "cleanup-leaf"));
  assert(counts[3].stops == 1 && counts[2].stops == 1);
  assert(counts[1].quiesces == 1 && counts[1].stops == 0 && mapped(1));
  assert(counts[0].quiesces == 0 && counts[0].stops == 0 && mapped(0));
  assert(!mapped(2) && !mapped(3));
  assert(!graph.shutdown()); // The failed root grant remains explicit custody.
  assert(!graph.acquire("cap.cleanup-stream", 1).slot);
  assert(!graph.acquire("cap.cleanup-middle", 1).slot);
  assert(!graph.recoverFailedFrom("cleanup-stream", "cap.cleanup-stream", 1));
  assert(counts[1].quiesces == 1 && counts[0].quiesces == 0);

  // The untouched sibling still has the original dependency pin. Acquiring
  // and releasing another consumer must not unload that provider early.
  auto sibling = graph.acquire("cap.cleanup-safe", 1);
  assert(sibling.slot && graph.release(sibling));
  assert(counts[0].quiesces == 0 && counts[0].stops == 0);
  assert(!graph.release(grant));
  assert(counts[1].quiesces == 2 && counts[1].stops == 0);
  assert(counts[2].quiesces == 1 && counts[3].quiesces == 1 && counts[3].closes == 1);
  assert(counts[0].quiesces == 0);
  repaired = true;
  GrantV2 replacementLeaf{};
  if (recoverLeafFirst) {
    assert(graph.recoverFailedFrom("cleanup-leaf", "cap.cleanup-leaf", 1));
    replacementLeaf = graph.acquire("cap.cleanup-leaf", 1);
    assert(replacementLeaf.slot && graph.interfaceFor(replacementLeaf));
  }
  assert(graph.release(grant));
  assert(!graph.interfaceFor(grant) && !graph.release(grant));
  assert(counts[3].closes == 1 && counts[3].stops == 1 && counts[2].stops == 1);
  assert(counts[0].quiesces == 1 && counts[0].stops == 1);
  assert(counts[1].quiesces == 3 && counts[1].stops == 1);
  if (recoverLeafFirst) {
    // The original pending edge was already unpinned. Retrying its parent
    // must not steal the fresh mapping's independent consumer reference.
    assert(graph.liveGrants() == 1 && graph.interfaceFor(replacementLeaf));
    assert(graph.release(replacementLeaf));
    assert(counts[1].quiesces == 4 && counts[1].stops == 2);
  }
  assert(graph.liveGrants() == 0 && graph.activationSafe() && graph.shutdown());
}
int main(int argc, char** argv) {
  assert(argc == 5);
  verify(argv + 1, false);
  verify(argv + 1, true);
  std::puts("Stream graph lower-dependency cleanup: checked failure, bounded retention, exact retry and no double unpin PASS");
}
