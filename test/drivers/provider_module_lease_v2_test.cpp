// Host-only lifecycle regression: real dlopen modules call observable hooks.
// The lease callbacks below model authority with monotonically increasing test
// tokens. Runtime's bound-storage callbacks/generation policy are tested by the
// separate provider-bound-storage integration suite.
#include "runtime/drivers/ProviderGraphV2.h"
#include "runtime/drivers/ProviderOwnedSpecV2.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

using namespace RuntimeProviders;

namespace {
struct LeaseState {
  unsigned slot = 0;
  bool live = false;
  bool failBegin = false;
  bool failStart = false;
  bool failBind = false;
  bool allowQuiesce = true;
  bool failStreamOpen = false;
  uint64_t token = 0;
  unsigned begins = 0;
  unsigned revokes = 0;
};
struct Event {
  unsigned slot;
  std::string name;
  bool live;
  uint64_t token;
};
LeaseState leases[3];
std::vector<Event> events;
uint64_t nextToken = 0;
unsigned streamSlot = 0;
#ifdef LEASE_WRAP_DLCLOSE
bool captureClose = false;
bool failClose = false;
#endif

void reset() {
  events.clear();
  for (unsigned slot = 0; slot < 3; ++slot) {
    assert(!leases[slot].live);
    leases[slot] = {};
    leases[slot].slot = slot;
  }
  streamSlot = 0;
}
void record(unsigned slot, const char* name) {
  assert(slot < 3);
  const auto& state = leases[slot];
  events.push_back({slot, name, state.live, state.token});
}
bool begin(void* context) {
  auto& state = *static_cast<LeaseState*>(context);
  assert(!state.live);
  state.token = ++nextToken;
  state.live = true;
  ++state.begins;
  record(state.slot, "begin");
  // A rejected begin may have partially initialized authority. The loader must
  // still revoke before calling any provider cleanup or diagnostic callback.
  return !state.failBegin;
}
void revoke(void* context) {
  auto& state = *static_cast<LeaseState*>(context);
  assert(state.live);
  state.live = false;
  ++state.revokes;
  record(state.slot, "revoke");
}
ModuleLeaseV2 lease(unsigned slot = 0) {
  return {&leases[slot], begin, revoke};
}
bool authorized(unsigned slot, uint64_t token) {
  assert(slot < 3);
  return token && leases[slot].live && leases[slot].token == token;
}
size_t firstEvent(unsigned slot, const char* name) {
  for (size_t i = 0; i < events.size(); ++i)
    if (events[i].slot == slot && events[i].name == name) return i;
  assert(false);
  return 0;
}
unsigned count(unsigned slot, const char* name) {
  unsigned result = 0;
  for (const auto& event : events)
    if (event.slot == slot && event.name == name) ++result;
  return result;
}
void expect(std::initializer_list<const char*> expected, unsigned slot = 0) {
  std::vector<std::string> actual;
  for (const auto& event : events)
    if (event.slot == slot) actual.push_back(event.name);
  if (actual.size() != expected.size()) {
    std::fprintf(stderr, "slot %u event count: expected %zu, got %zu\n",
                 slot, expected.size(), actual.size());
    for (const auto& name : actual) std::fprintf(stderr, "  %s\n", name.c_str());
    assert(false);
  }
  size_t i = 0;
  for (const char* name : expected) assert(actual[i++] == name);
}
void expectAuthority(const char* name, bool live, unsigned slot = 0) {
  bool seen = false;
  for (const auto& event : events) {
    if (event.slot != slot || event.name != name) continue;
    seen = true;
    assert(event.live == live);
  }
  assert(seen);
}
void expectDeadCleanup(unsigned slot = 0) {
  for (const auto& event : events) {
    if (event.slot == slot && (event.name == "diagnostics" ||
        event.name == "quiesce" || event.name == "stop" ||
        event.name == "close")) assert(!event.live);
  }
}
bool streamOpen(risc_stream_provider_v1* out) {
  record(streamSlot, "stream-open");
  if (leases[streamSlot].failStreamOpen) return false;
  *out = {};
  out->api_version = RISC_STREAM_PROVIDER_API_V1;
  out->struct_size = sizeof(*out);
  out->context = streamSlot + 1;
  return true;
}
void streamRevoke(uint64_t context) {
  assert(context >= 1 && context <= 3);
  record(static_cast<unsigned>(context - 1), "stream-revoke");
}
void streamClose(uint64_t context) {
  assert(context >= 1 && context <= 3);
  record(static_cast<unsigned>(context - 1), "stream-close");
}
const StreamHostV1 streams{streamOpen, streamRevoke, streamClose, nullptr, nullptr};

bool load(ModuleV2& module, const char* path) {
  return module.load(path, "fixture-lease-root", "cap.lease-root", 1, nullptr, 0);
}
SpecV2 rootSpec(const char* path) {
  SpecV2 spec{"fixture-lease-root", path, "cap.lease-root", 1, nullptr, 0};
  spec.lease = lease();
  return spec;
}
SpecV2 childSpec(const char* path, const RequirementV2* requirements) {
  SpecV2 spec{"fixture-lease-child", path, "cap.lease-child", 1, requirements, 1};
  spec.lease = lease(1);
  return spec;
}

void testPartialHooks(const char* path) {
  reset();
  for (unsigned mask = 1; mask < 7; ++mask) {
    ModuleLeaseV2 partial{
      (mask & 1) ? static_cast<void*>(&leases[0]) : nullptr,
      (mask & 2) ? begin : nullptr,
      (mask & 4) ? revoke : nullptr
    };
    assert(!partial.valid());
    ModuleV2 module;
    assert(!module.setLease(partial));
    assert(module.setLease({}));
    assert(module.unload());
    GraphV2 graph;
    auto spec = rootSpec(path);
    spec.lease = partial;
    assert(!graph.addVerified(spec));
    assert(graph.moduleCount() == 0 && graph.shutdown());
  }
  assert(events.empty());
  assert(ModuleLeaseV2{}.valid() && lease().valid());
}

void testLegacyAndSetter(const char* path) {
  reset();
  ModuleV2 legacy;
  assert(load(legacy, path));
  assert(legacy.unload());
  expect({"entry", "start", "quiesce", "stop"});
  expectAuthority("start", false);

  reset();
  ModuleV2 module;
  auto callerLease = lease();
  assert(module.setLease(callerLease));
  callerLease = lease(2); // Setter must copy, not retain the caller's value.
  assert(load(module, path));
  assert(!module.setLease(callerLease));
  assert(!module.setLease({}));
  assert(leases[0].begins == 1 && leases[2].begins == 0);
  assert(module.pinConsumer());
  const auto before = events.size();
  assert(!module.unload()); // A live consumer prevents even revocation.
  assert(events.size() == before && leases[0].live);
  assert(module.capability() && module.consumers() == 1);
  assert(module.unpinConsumer() && module.unload());
  assert(module.state() == ModuleV2::State::Absent);
  assert(!module.capability());
  assert(module.unload()); // No duplicate revoke, quiesce, or stop.
  expect({"entry", "begin", "start", "revoke", "quiesce", "stop"});
  expectAuthority("entry", false);
  expectAuthority("start", true);
  expectDeadCleanup();
  assert(!authorized(0, leases[0].token));
  assert(leases[0].revokes == 1 && module.setLease({}));
}

void testStreamOrdering(const char* path) {
  reset();
  ModuleV2 module;
  assert(module.setStreamHost(&streams) && module.setLease(lease()));
  assert(load(module, path));
  expect({"entry", "stream-open", "bind", "begin", "start"});
  expectAuthority("bind", false);
  expectAuthority("start", true);
  assert(module.unload());
  expect({"entry", "stream-open", "bind", "begin", "start", "revoke",
          "stream-revoke", "quiesce", "stop", "stream-close"});
  expectDeadCleanup();

  reset();
  leases[0].failBind = true;
  ModuleV2 bindFailure;
  assert(bindFailure.setStreamHost(&streams) && bindFailure.setLease(lease()));
  assert(!load(bindFailure, path));
  assert(bindFailure.unload());
  expect({"entry", "stream-open", "bind", "diagnostics", "stream-revoke",
          "quiesce", "stop", "stream-close"});
  assert(!leases[0].begins && !leases[0].revokes && !leases[0].live);
  expectDeadCleanup();

  reset();
  leases[0].failStreamOpen = true;
  ModuleV2 unavailable;
  assert(unavailable.setStreamHost(&streams) && unavailable.setLease(lease()));
  assert(!load(unavailable, path));
  assert(unavailable.unload());
  expect({"entry", "stream-open"});
  assert(!leases[0].begins && !leases[0].revokes);

  reset();
  ModuleV2 missingHost;
  assert(missingHost.setLease(lease()));
  assert(!load(missingHost, path));
  assert(missingHost.unload());
  expect({"entry"});
  assert(!leases[0].begins && !leases[0].revokes);
}

void testRejectedActivation(const char* path, const char* streamPath) {
  reset();
  leases[0].failBegin = true;
  ModuleV2 beginFailure;
  assert(beginFailure.setLease(lease()));
  assert(!load(beginFailure, path));
  assert(beginFailure.state() == ModuleV2::State::Failed);
  assert(!beginFailure.setLease({}));
  assert(beginFailure.unload() && beginFailure.unload());
  expect({"entry", "begin", "revoke", "diagnostics", "quiesce", "stop"});
  assert(!count(0, "start") && leases[0].revokes == 1);
  expectDeadCleanup();

  reset();
  leases[0].failStart = true;
  ModuleV2 startFailure;
  assert(startFailure.setStreamHost(&streams) && startFailure.setLease(lease()));
  assert(!load(startFailure, streamPath));
  assert(std::strstr(startFailure.lastError(), "fixture start rejected"));
  assert(startFailure.unload());
  expect({"entry", "stream-open", "bind", "begin", "start", "revoke",
          "diagnostics", "stream-revoke", "quiesce", "stop", "stream-close"});
  expectAuthority("start", true);
  expectDeadCleanup();
}

void testPrestartRejection(const char* path, const char* wrongAbi,
                           const char* missingEntry, const char* corrupt) {
  reset();
  const std::string missing = std::string(path) + ".missing";
  const char* rejectedPaths[] = {missing.c_str(), wrongAbi, missingEntry, corrupt};
  for (const char* rejected : rejectedPaths) {
    ModuleV2 module;
    assert(module.setLease(lease()));
    assert(!load(module, rejected));
    assert(module.unload());
  }
  const char* expectedIds[] = {"wrong-identity", "fixture-lease-root", "fixture-lease-root"};
  const char* expectedCaps[] = {"cap.lease-root", "wrong-capability", "cap.lease-root"};
  const uint32_t expectedApis[] = {1, 1, 2};
  for (unsigned i = 0; i < 3; ++i) {
    ModuleV2 module;
    assert(module.setLease(lease()));
    assert(!module.load(path, expectedIds[i], expectedCaps[i], expectedApis[i], nullptr, 0));
    assert(module.unload());
  }
  ModuleV2 invalid;
  assert(invalid.setLease(lease()));
  assert(!invalid.load(path, "fixture-lease-root", "cap.lease-root", 1, nullptr, 1));
  assert(invalid.unload());
  assert(count(0, "entry") == 4); // Wrong ABI, identity, capability, API only.
  assert(events.size() == 4 && !leases[0].live);
  assert(!leases[0].begins && !leases[0].revokes);
}

void testOwnedSnapshot(const char* path) {
  reset();
  GraphV2 graph;
  {
    auto callerSpec = rootSpec(path);
    OwnedNodeV2 snapshot;
    assert(snapshot.snapshot(callerSpec));
    assert(snapshot.spec.lease.context == &leases[0]);
    assert(snapshot.spec.lease.begin == begin && snapshot.spec.lease.revoke == revoke);
    assert(graph.addVerified(callerSpec));
    callerSpec.lease = lease(2);
    callerSpec.id = "mutated-caller";
    callerSpec.verifiedElfPath = "/not/the/admitted/image";
    assert(snapshot.spec.lease.context == &leases[0]);
  }
  auto first = graph.acquire("cap.lease-root", 1);
  auto second = graph.acquire("cap.lease-root", 1);
  assert(first.slot && second.slot && leases[0].begins == 1);
  const uint64_t saved = leases[0].token;
  assert(graph.release(first));
  assert(leases[0].live && !leases[0].revokes && graph.interfaceFor(second));
  assert(graph.release(second) && graph.shutdown());
  assert(!leases[0].live && leases[0].revokes == 1 && leases[2].begins == 0);
  assert(!authorized(0, saved));
  auto fresh = graph.acquire("cap.lease-root", 1);
  assert(fresh.slot && leases[0].token != saved && leases[0].begins == 2);
  assert(!authorized(0, saved) && authorized(0, leases[0].token));
  assert(graph.release(fresh) && graph.shutdown());
}

void testRetainedTeardown(const char* root, const char* child) {
  reset();
  const RequirementV2 required[] = {{"cap.lease-root", 1}};
  GraphV2 graph;
  assert(graph.addVerified(rootSpec(root)));
  assert(graph.addVerified(childSpec(child, required)));
  auto grant = graph.acquire("cap.lease-child", 1);
  assert(grant.slot && leases[0].live && leases[1].live);
  const auto oldRootToken = leases[0].token;
  const auto oldChildToken = leases[1].token;
  assert(oldRootToken != oldChildToken);
  leases[1].allowQuiesce = false;
  assert(!graph.release(grant));
  assert(!graph.interfaceFor(grant));
  assert(!leases[1].live && leases[1].revokes == 1);
  assert(!authorized(1, oldChildToken) && authorized(0, oldRootToken));
  assert(leases[0].live && !leases[0].revokes);
  assert(!graph.acquire("cap.lease-child", 1).slot);
  assert(!graph.recoverFailedFrom("fixture-lease-child", "cap.lease-child", 1));
  assert(!graph.release(grant));
  auto rootGrant = graph.acquire("cap.lease-root", 1);
  assert(rootGrant.slot && *static_cast<const int*>(graph.interfaceFor(rootGrant)) == 42);
  assert(graph.release(rootGrant));
  assert(leases[0].live && leases[0].begins == 1 && leases[1].begins == 1);
  assert(!graph.shutdown());
  leases[1].allowQuiesce = true;
  assert(graph.release(grant) && graph.shutdown());
  assert(!leases[0].live && !leases[1].live);
  assert(leases[0].revokes == 1 && leases[1].revokes == 1);
  assert(count(1, "quiesce") == 3 && count(1, "stop") == 1);
  assert(firstEvent(1, "stop") < firstEvent(0, "revoke"));
  assert(!graph.release(grant));
  expectDeadCleanup(0);
  expectDeadCleanup(1);
  // Successful physical cleanup is the only path to a new begin/token.
  grant = graph.acquire("cap.lease-child", 1);
  assert(grant.slot && leases[0].token != oldRootToken && leases[1].token != oldChildToken);
  assert(!authorized(0, oldRootToken) && !authorized(1, oldChildToken));
  assert(leases[0].begins == 2 && leases[1].begins == 2);
  assert(graph.release(grant) && graph.shutdown());
}

void testRetainedFailedStart(const char* root, const char* child) {
  reset();
  const RequirementV2 required[] = {{"cap.lease-root", 1}};
  GraphV2 graph;
  assert(graph.addVerified(rootSpec(root)));
  assert(graph.addVerified(childSpec(child, required)));
  leases[1].failStart = true;
  leases[1].allowQuiesce = false;
  assert(!graph.acquire("cap.lease-child", 1).slot);
  assert(std::strstr(graph.lastError(), "fixture start rejected"));
  assert(graph.liveGrants() == 0);
  expect({"entry", "begin", "start", "revoke", "diagnostics", "quiesce", "quiesce"}, 1);
  assert(leases[0].live && !leases[1].live && leases[1].revokes == 1);
  assert(!graph.acquire("cap.lease-child", 1).slot);
  assert(!graph.recoverFailedFrom("fixture-lease-child", "cap.lease-child", 1));
  assert(leases[1].begins == 1 && leases[1].revokes == 1 && !count(1, "stop"));
  auto rootGrant = graph.acquire("cap.lease-root", 1);
  assert(rootGrant.slot && *static_cast<const int*>(graph.interfaceFor(rootGrant)) == 42);
  assert(graph.release(rootGrant) && leases[0].live);
  leases[1].allowQuiesce = true;
  assert(graph.recoverFailedFrom("fixture-lease-child", "cap.lease-child", 1));
  assert(graph.shutdown() && !leases[0].live);
  assert(count(1, "stop") == 1 && leases[1].revokes == 1);
  assert(firstEvent(1, "stop") < firstEvent(0, "revoke"));
  const auto oldToken = leases[1].token;
  leases[1].failStart = false;
  auto fresh = graph.acquire("cap.lease-child", 1);
  assert(fresh.slot && leases[1].begins == 2 && leases[1].token != oldToken);
  assert(!authorized(1, oldToken) && authorized(1, leases[1].token));
  assert(graph.release(fresh) && graph.shutdown());
  expectDeadCleanup(0);
  expectDeadCleanup(1);
}

#ifdef LEASE_WRAP_DLCLOSE
void testCloseFailure(const char* path) {
  reset();
  ModuleV2 module;
  assert(module.setLease(lease()) && load(module, path));
  captureClose = true;
  failClose = true;
  assert(!module.unload());
  assert(module.state() == ModuleV2::State::Failed && !module.capability());
  assert(!leases[0].live && leases[0].revokes == 1);
  assert(!module.setLease({}) && !load(module, path));
  expect({"entry", "begin", "start", "revoke", "quiesce", "stop", "close"});
  assert(module.unload() && module.state() == ModuleV2::State::Absent);
  assert(module.unload());
  expect({"entry", "begin", "start", "revoke", "quiesce", "stop", "close", "close"});
  expectDeadCleanup();
  captureClose = false;
  assert(leases[0].revokes == 1 && leases[0].begins == 1);
}
#endif
} // namespace

extern "C" void provider_lease_event(unsigned slot, const char* event) {
  record(slot, event);
}
extern "C" int provider_lease_control(unsigned slot, unsigned control) {
  assert(slot < 3);
  if (control == 0) return leases[slot].failStart;
  if (control == 1) return leases[slot].failBind;
  assert(control == 2);
  return leases[slot].allowQuiesce;
}
extern "C" uint64_t provider_lease_token(unsigned slot) {
  assert(slot < 3);
  return leases[slot].token;
}
extern "C" int provider_lease_authorized(unsigned slot, uint64_t token) {
  assert(slot < 3);
  return authorized(slot, token);
}
#ifdef LEASE_WRAP_DLCLOSE
extern "C" int __real_dlclose(void* handle);
extern "C" int __wrap_dlclose(void* handle) {
  if (captureClose) {
    record(0, "close");
    if (failClose) { failClose = false; return 1; }
  }
  return __real_dlclose(handle);
}
#endif

int main(int argc, char** argv) {
  assert(argc == 7);
  testPartialHooks(argv[1]);
  testLegacyAndSetter(argv[1]);
  testStreamOrdering(argv[3]);
  testRejectedActivation(argv[1], argv[3]);
  testPrestartRejection(argv[1], argv[4], argv[5], argv[6]);
  testOwnedSnapshot(argv[1]);
  testRetainedTeardown(argv[1], argv[2]);
  testRetainedFailedStart(argv[1], argv[2]);
#ifdef LEASE_WRAP_DLCLOSE
  testCloseFailure(argv[1]);
#else
  std::puts("Module lease close-failure injection: unavailable on this host linker");
#endif
  std::puts("Provider module lease: admission, owned hooks, stream/start order, consumer pins, retained cleanup and fresh activation PASS");
}
