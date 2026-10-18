// Private access is confined to fault injection and bookkeeping assertions.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <esp_dlfcn.h>
#define private public
#include "runtime/drivers/ProviderGraphV2.h"
#undef private

using namespace RuntimeProviders;
namespace {
constexpr size_t Fixtures = 40;
struct Control {
  bool start = true;
  bool quiesce = true;
  unsigned events[4]{};
} controls[Fixtures];
GraphV2* callbackGraph = nullptr;
size_t callbackChecks = 0;
bool streamSafe = true;
unsigned streamOpens = 0, streamRevokes = 0, streamCloses = 0;
const char* fixtureDirectory;

// Check byte-for-byte graph stability and every observable provider/stream
// lifecycle callback for both accepted and rejected validation requests.
void expect(const GraphV2& graph, const GrantV2* keep, size_t count, bool accepted) {
  std::vector<unsigned char> before(sizeof(graph));
  std::memcpy(before.data(), &graph, sizeof(graph));
  Control oldControls[Fixtures];
  std::memcpy(oldControls, controls, sizeof(controls));
  const unsigned opens = streamOpens, revokes = streamRevokes, closes = streamCloses;
  assert(graph.validateKeepSet(keep, count) == accepted);
  assert(!std::memcmp(before.data(), &graph, sizeof(graph)));
  assert(!std::memcmp(oldControls, controls, sizeof(controls)));
  assert(streamOpens == opens && streamRevokes == revokes && streamCloses == closes);
}
void expect(const GraphV2& graph, GrantV2 keep, bool accepted) {
  expect(graph, &keep, 1, accepted);
}
SpecV2 spec(size_t index, const RequirementV2* needs = nullptr, size_t count = 0) {
  // addVerified snapshots these caller-owned strings immediately.
  static char id[32], cap[32], path[512];
  std::snprintf(id, sizeof(id), "keep-%zu", index);
  std::snprintf(cap, sizeof(cap), "cap.keep-%zu", index);
  std::snprintf(path, sizeof(path), "%s/provider-%zu.so", fixtureDirectory, index);
  return {id, path, cap, 1, needs, count};
}
GrantV2 acquire(GraphV2& graph, size_t index) {
  char capability[32];
  std::snprintf(capability, sizeof(capability), "cap.keep-%zu", index);
  const auto grant = graph.acquire(capability, 1);
  assert(grant.slot && graph.interfaceFor(grant));
  return grant;
}
void diamond(GraphV2& graph) {
  const RequirementV2 base[] = {{"cap.keep-0", 1}};
  const RequirementV2 branches[] = {{"cap.keep-1", 1}, {"cap.keep-2", 1}};
  assert(graph.addVerified(spec(0)));
  assert(graph.addVerified(spec(1, base, 1)));
  assert(graph.addVerified(spec(2, base, 1)));
  assert(graph.addVerified(spec(3, branches, 2)));
  assert(graph.addVerified(spec(4)));
}
uint32_t now() { return 0; }
void testExactClosure() {
  GraphV2 graph;
  diamond(graph);
  expect(graph, nullptr, 0, true);
  callbackGraph = &graph;
  auto root = acquire(graph, 3);
  callbackGraph = nullptr;
  assert(callbackChecks >= 4);
  assert(graph.nodes_[0].module.consumers() == 2);
  expect(graph, root, true);
  expect(graph, nullptr, 0, false);
  assert(!graph.shutdown()); // The existing shutdown contract still rejects roots.

  auto same = acquire(graph, 3);
  auto base = acquire(graph, 0);
  GrantV2 keep[] = {same, base, root};
  expect(graph, keep, 3, true);
  expect(graph, keep, 2, false);
  expect(graph, root, false); // Extra root on the same provider is still foreign.
  assert(graph.release(same) && graph.release(base));
  expect(graph, root, true);
  auto unrelated = acquire(graph, 4);
  expect(graph, root, false);
  GrantV2 both[] = {root, unrelated};
  expect(graph, both, 2, true);
  assert(graph.release(unrelated));
  expect(graph, root, true);

  // A consumer leak within the otherwise correct closure cannot be forgiven.
  assert(graph.nodes_[1].module.pinConsumer());
  expect(graph, root, false);
  assert(graph.nodes_[1].module.unpinConsumer());
  assert(graph.nodes_[0].module.unpinConsumer());
  expect(graph, root, false);
  assert(graph.nodes_[0].module.pinConsumer());
  expect(graph, root, true);

  assert(graph.beginStreamCallback());
  expect(graph, root, false);
  graph.endStreamCallback();
  callbackGraph = &graph;
  const auto checks = callbackChecks;
  graph.poll(now, nullptr);
  assert(callbackChecks > checks);
  callbackGraph = nullptr;
  expect(graph, root, true);
  callbackGraph = &graph;
  assert(graph.release(root));
  callbackGraph = nullptr;
  expect(graph, nullptr, 0, true);
  assert(graph.shutdown());
}
void testInvalidGrants() {
  GraphV2 graph;
  assert(graph.addVerified(spec(0)));
  auto root = acquire(graph, 0);
  expect(graph, nullptr, 1, false);
  expect(graph, &root, GraphV2::kMaxGrants + 1, false);
  expect(graph, GrantV2{}, false);
  expect(graph, GrantV2{root.slot, 0}, false);
  expect(graph, GrantV2{uint32_t(GraphV2::kMaxGrants + 1), root.generation}, false);
  expect(graph, GrantV2{root.slot, root.generation + 1}, false);
  expect(graph, GrantV2{uint32_t(GraphV2::kMaxGrants), root.generation}, false);
  const GrantV2 duplicate[] = {root, root};
  expect(graph, duplicate, 2, false);
  assert(graph.release(root));
  const auto fresh = acquire(graph, 0);
  assert(root.slot == fresh.slot && root.generation != fresh.generation);
  expect(graph, root, false);
  expect(graph, fresh, true);
  assert(graph.release(fresh));
  expect(graph, fresh, false);
  expect(graph, nullptr, 0, true);
}
void testRetainedFailure() {
  for (const size_t fail : {size_t(0), size_t(3)}) {
    GraphV2 graph;
    diamond(graph);
    const auto root = acquire(graph, 3);
    controls[fail].quiesce = false;
    assert(!graph.release(root));
    assert(graph.grants_[root.slot - 1].pendingRelease);
    if (fail == 0) {
      assert(graph.nodes_[1].dependencyReleasePending);
      assert(graph.nodes_[3].visit == GraphV2::Visit::Releasing);
    }
    expect(graph, root, false);
    expect(graph, nullptr, 0, false);
    assert(!graph.shutdown());
    controls[fail].quiesce = true;
    assert(graph.release(root) && graph.shutdown());
    expect(graph, nullptr, 0, true);
  }
  GraphV2 failed;
  diamond(failed);
  controls[3].start = false;
  controls[3].quiesce = false;
  assert(!failed.acquire("cap.keep-3", 1).slot);
  assert(!failed.liveGrants());
  expect(failed, nullptr, 0, false);
  controls[3].quiesce = true;
  controls[3].start = true;
  assert(failed.recoverFailedFrom("keep-3", "cap.keep-3", 1));
  expect(failed, nullptr, 0, true);
}
bool leaseBegin(void*) { return true; }
void leaseRevoke(void*) {}
bool leaseSafe(void* state) { return *static_cast<const bool*>(state); }
bool streamOpen(risc_stream_provider_v1* out) {
  *out = {};
  out->api_version = 1;
  out->struct_size = sizeof(*out);
  out->context = 51;
  ++streamOpens;
  return true;
}
void streamRevoke(uint64_t) { ++streamRevokes; }
void streamClose(uint64_t) { ++streamCloses; }
bool streamSafety(uint64_t context) { return context == 51 && streamSafe; }
const StreamHostV1 streamHost = {
  streamOpen, streamRevoke, streamClose, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, streamSafety
};
void testSafety() {
  bool safe = true;
  GraphV2 graph;
  auto leased = spec(0);
  leased.lease = {&safe, leaseBegin, leaseRevoke, leaseSafe};
  assert(graph.addVerified(leased));
  const auto root = acquire(graph, 0);
  expect(graph, root, true);
  safe = false;
  expect(graph, root, false);
  safe = true;
  assert(graph.release(root));
  // An absent module's sticky host lease remains part of the safety fence.
  safe = false;
  expect(graph, nullptr, 0, false);
  safe = true;
  expect(graph, nullptr, 0, true);

  GraphV2 streams(&streamHost);
  assert(streams.addVerified(spec(5)));
  const auto stream = acquire(streams, 5);
  expect(streams, stream, true);
  streamSafe = false;
  expect(streams, stream, false);
  streamSafe = true;
  expect(streams, stream, true);
  assert(streams.release(stream));
  assert(streamOpens == streamCloses && streamRevokes == streamCloses);
}
void testInconsistentNodes() {
  GraphV2 graph;
  diamond(graph);
  const auto root = acquire(graph, 3);
  auto& live = graph.nodes_[0];
  for (const auto visit : {GraphV2::Visit::Idle, GraphV2::Visit::Visiting, GraphV2::Visit::Releasing}) {
    live.visit = visit;
    expect(graph, root, false);
  }
  live.visit = GraphV2::Visit::Active;
  for (const auto state : {ModuleV2::State::Absent, ModuleV2::State::Failed}) {
    live.module.state_ = state;
    expect(graph, root, false);
  }
  live.module.state_ = ModuleV2::State::Active;
  live.dependencyReleasePending = true;
  expect(graph, root, false);
  live.dependencyReleasePending = false;

  auto& outside = graph.nodes_[4];
  outside.module.consumers_ = 1;
  expect(graph, root, false);
  outside.module.consumers_ = 0;
  outside.acquired = 1;
  expect(graph, root, false);
  outside.acquired = 0;
  outside.module.state_ = ModuleV2::State::Active;
  expect(graph, root, false);
  outside.module.state_ = ModuleV2::State::Absent;
  for (const auto visit : {GraphV2::Visit::Active, GraphV2::Visit::Visiting, GraphV2::Visit::Releasing}) {
    outside.visit = visit;
    expect(graph, root, false);
  }
  outside.visit = GraphV2::Visit::Idle;
  outside.dependencyReleasePending = true;
  expect(graph, root, false);
  outside.dependencyReleasePending = false;

  const size_t acquired = graph.nodes_[3].acquired;
  graph.nodes_[3].acquired = GraphV2::kMaxRequirements + 2;
  expect(graph, root, false);
  graph.nodes_[3].acquired = acquired;
  const uint8_t edge = graph.nodes_[3].dependencies[0];
  graph.nodes_[3].dependencies[0] = uint8_t(graph.moduleCount());
  expect(graph, root, false);
  graph.nodes_[3].dependencies[0] = edge;
  const uint8_t node = graph.grants_[root.slot - 1].node;
  graph.grants_[root.slot - 1].node = uint8_t(graph.moduleCount());
  expect(graph, root, false);
  graph.grants_[root.slot - 1].node = node;
  const size_t count = graph.count_;
  graph.count_ = GraphV2::kMaxModules + 1;
  expect(graph, root, false);
  graph.count_ = count;
  expect(graph, root, true);
  assert(graph.release(root));
}
void* openImage(void*, const char* path, const void* source) {
  assert(source && *static_cast<const uint32_t*>(source) == 1);
  return esp_dlopen_instance(path);
}
void testActualEdges() {
  GraphV2 graph;
  const auto imagePath = std::string(fixtureDirectory) + "/image.so";
  assert(graph.addVerified({"keep-0", imagePath.c_str(), "storage.files", 1, nullptr, 0}));
  static unsigned trusted = 1;
  const RequirementV2 needs[] = {{"storage.files", 1}, {"trusted.raw", 1, nullptr, 0, &trusted}};
  auto child = spec(1, needs, 2);
  child.imageProvider = "keep-0";
  child.imageCapability = "storage.files";
  child.openImage = openImage;
  child.imageContext = &trusted;
  assert(graph.addVerified(child));
  const auto root = acquire(graph, 1);
  assert(graph.nodes_[0].module.consumers() == 2);
  assert(graph.nodes_[1].acquired == 2); // Requirement and image-source pins.
  expect(graph, root, true); // trusted.raw is not a provider edge.
  assert(graph.release(root));
}
void testCapacities() {
  GraphV2 graph(&streamHost);
  // Chain reaches the last selected module slot and exercises a bounded
  // traversal without relying on registration order matching activation order.
  for (size_t i = 0; i < GraphV2::kMaxModules; ++i) {
    char cap[32];
    std::snprintf(cap, sizeof(cap), "cap.keep-%zu", i + 1);
    const RequirementV2 next[] = {{cap, 1}};
    assert(graph.addVerified(spec(i, i + 1 < GraphV2::kMaxModules ? next : nullptr,
                                  i + 1 < GraphV2::kMaxModules ? 1 : 0)));
  }
  GrantV2 roots[GraphV2::kMaxGrants]{};
  for (auto& root : roots) root = acquire(graph, 0);
  expect(graph, roots, GraphV2::kMaxGrants, true);
  expect(graph, roots, GraphV2::kMaxGrants - 1, false);
  assert(!graph.acquire("cap.keep-0", 1).slot);
  expect(graph, roots, GraphV2::kMaxGrants, true);
  for (const auto root : roots) assert(graph.release(root));
  expect(graph, nullptr, 0, true);
  assert(graph.shutdown());
  std::printf("Exact graph keep set: %zu providers / %zu grants PASS\n",
              GraphV2::kMaxModules, GraphV2::kMaxGrants);
}
} // namespace
extern "C" void keep_set_event(unsigned slot, unsigned event) {
  assert(slot < Fixtures && event < 4);
  ++controls[slot].events[event];
  if (callbackGraph) {
    assert(callbackGraph->lifecycleBusy());
    expect(*callbackGraph, nullptr, 0, false);
    ++callbackChecks;
  }
}
extern "C" bool keep_set_control(unsigned slot, bool starting) {
  assert(slot < Fixtures);
  return starting ? controls[slot].start : controls[slot].quiesce;
}
int main(int argc, char** argv) {
  assert(argc == 2);
  fixtureDirectory = argv[1];
  testExactClosure();
  testInvalidGrants();
  testRetainedFailure();
  testSafety();
  testInconsistentNodes();
  testActualEdges();
  testCapacities();
}
