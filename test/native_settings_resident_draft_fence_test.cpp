#include "bootstrap/Runtime.h"
#include "experimental/native_replica/NativeAtomicKeyValue.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using Runtime = RiscBoot::Runtime;
using Core = RiscExperimental::NativeReplicaCore;
using Atomic = RiscExperimental::NativeAtomicKeyValue;
using Bytes = std::vector<uint8_t>;

// This backend is memory-only. The test loads real host shared modules but
// performs no native filesystem storage, SD access, or product integration.
struct MemoryStore {
  Bytes root;
  std::map<uint64_t, Bytes> objects;
  uint64_t next = 1;
  Core::Backend backend() {
    return {this,
      [](void* p, void* out, uint32_t size) -> int {
        auto& store = *static_cast<MemoryStore*>(p);
        if (store.root.empty()) return Core::Missing;
        if (store.root.size() != size) return Core::Io;
        std::memcpy(out, store.root.data(), size); return Core::Ok;
      },
      [](void* p, uint32_t size, uint64_t* id) -> int {
        auto& store = *static_cast<MemoryStore*>(p);
        *id = store.next++; store.objects[*id] = Bytes(size); return Core::Ok;
      },
      [](void* p, uint64_t id, uint32_t offset, const void* bytes, uint32_t size) -> int {
        auto& objects = static_cast<MemoryStore*>(p)->objects;
        const auto found = objects.find(id);
        if (found == objects.end() || offset > found->second.size() || size > found->second.size() - offset) return Core::Io;
        std::memcpy(found->second.data() + offset, bytes, size); return Core::Ok;
      },
      [](void* p, uint64_t id, uint32_t offset, void* bytes, uint32_t size) -> int {
        const auto& objects = static_cast<MemoryStore*>(p)->objects;
        const auto found = objects.find(id);
        if (found == objects.end() || offset > found->second.size() || size > found->second.size() - offset) return Core::Io;
        std::memcpy(bytes, found->second.data() + offset, size); return Core::Ok;
      },
      [](void* p, uint64_t id) -> int { return static_cast<MemoryStore*>(p)->objects.count(id) ? Core::Ok : Core::Io; },
      [](void* p, const void* bytes, uint32_t size) -> int {
        const auto* source = static_cast<const uint8_t*>(bytes);
        static_cast<MemoryStore*>(p)->root.assign(source, source + size); return Core::Ok;
      },
      [](void* p, uint64_t id) { static_cast<MemoryStore*>(p)->objects.erase(id); }
    };
  }
};

namespace {
Runtime* runtime;
Atomic* endpoint;
const risc_runtime_api_v1* api;
risc_resident_client_v1 clients[2]{};
std::string mode;
bool inNative = false, serviceLease = false, acquiringAtomic = false;
unsigned liveDrafts = 0, atomicHooks = 0, acquisitionDrafts = 0;
unsigned checkpoints = 0, allowed = 0, fenced = 0, pendingDrain = 0, fencedDrains = 0;
unsigned serviceCalls = 0, serviceBegins = 0, serviceEnds = 0, starts = 0, stops = 0;
unsigned entries[2]{}, finishes[2]{}, unmaps[2]{}, dispatches = 0;
unsigned abandoned[2]{}, revokedBeforeUnmap = 0;
unsigned finiServices[2]{}, finiLeases[2]{};
int revokingRole = -1;

bool owner() { return true; }
bool checkpoint(bool admitNew) {
  assert(!inNative && !serviceLease && !runtime->active());
  inNative = true;
  // An atomic acquire must drain once BEFORE it publishes its first grant.
  // Its ordinary post-publication checkpoint must then see the new draft.
  const bool expected = acquiringAtomic ? atomicHooks == 0 && acquisitionDrafts == 0 : liveDrafts == 0;
  assert(admitNew == expected);
  if (acquiringAtomic) ++atomicHooks;
  ++checkpoints;
  if (admitNew) ++allowed; else ++fenced;
  // A false admission flag still reaches the callback, which can finish an
  // already-accepted operation. It is not a blanket callback suppression.
  if (pendingDrain) {
    if (!admitNew) fencedDrains += pendingDrain;
    pendingDrain = 0;
  }
  if (revokingRole >= 0) {
    assert(admitNew && finishes[revokingRole] == 1 && !unmaps[revokingRole]);
    ++revokedBeforeUnmap;
    revokingRole = -1;
  }
  assert(!risc_runtime_get_api(1));
  inNative = false;
  return true;
}

risc_runtime_capability_v1 atomicGrant() {
  acquiringAtomic = true; acquisitionDrafts = liveDrafts; atomicHooks = 0;
  risc_runtime_capability_v1 grant{sizeof(grant)};
  assert(api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY, 1, 6, &grant));
  assert(atomicHooks >= 2); // pre-grant drain and post-grant fenced callback
  acquiringAtomic = false; ++liveDrafts;
  return grant;
}

uint64_t readDraft(const risc_runtime_capability_v1& grant) {
  const auto& atomic = *static_cast<const risc_key_value_atomic_v1*>(grant.api);
  risc_key_value_atomic_state_v1 state{sizeof(state)};
  assert(atomic.state(atomic.context, &state) == RISC_ATOMIC_OK && state.namespace_id == 6);
  char value[16]{}; uint32_t size = 0;
  assert(atomic.get(atomic.context, state.revision, "sample", value, sizeof(value), &size) == RISC_ATOMIC_OK);
  assert(size == 6 && !std::memcmp(value, "stable", 6));
  return state.revision;
}

void releaseAtomic(risc_runtime_capability_v1& grant) {
  assert(liveDrafts); --liveDrafts;
  const unsigned before = checkpoints, serviced = serviceCalls, leases = serviceBegins;
  assert(api->release(&grant));
  assert(checkpoints > before); // settled release opens admission immediately
  assert(serviceCalls == serviced && serviceBegins == leases); // native only
}

void ordinaryGrant() {
  const unsigned before = checkpoints, serviced = serviceCalls, drained = fencedDrains;
  const bool shouldFence = liveDrafts != 0;
  ++pendingDrain;
  risc_runtime_capability_v1 grant{sizeof(grant)};
  assert(api->acquire("test.settings-fence-service", 1, 0, &grant));
  assert(checkpoints >= before + 2 && !pendingDrain);
  assert(serviceCalls >= serviced + 2 && serviceBegins == serviceEnds && !serviceLease);
  if (shouldFence) assert(fencedDrains == drained + 1);
  assert(api->release(&grant));
}

int32_t dispatch(void*, const risc_resident_request_v1* request, risc_resident_reply_v1* reply) {
  assert(request->reason == RISC_RESIDENT_CHECKPOINT_CONTROLS);
  risc_resident_client_v1 current{1, sizeof(current)};
  assert(api->resident_shell(&current) && current.role == RISC_RESIDENT_ROLE_HOST);
  ++dispatches;
  ordinaryGrant(); // foreground atomic grants must fence this HOST acquire
  reply->flags = 0;
  return RISC_RESIDENT_OK;
}

void callHost() {
  risc_resident_request_v1 request{sizeof(request), RISC_RESIDENT_CHECKPOINT_CONTROLS, 0, 0};
  risc_resident_reply_v1 reply{sizeof(reply), 0};
  const unsigned before = dispatches;
  assert(clients[1].checkpoint(clients[1].invocation, &request, &reply) == RISC_RESIDENT_OK);
  assert(dispatches == before + 1);
}

void childEntry() {
  assert(api->resident_shell(&clients[1]) && clients[1].role == RISC_RESIDENT_ROLE_FOREGROUND);
  if (mode == "host" || mode == "host-return") {
    assert(liveDrafts == 2);
    ordinaryGrant(); // HOST atomic grants must fence this foreground acquire
    return;
  }
  auto first = atomicGrant(); auto second = atomicGrant();
  const uint64_t revision = readDraft(first);
  callHost(); assert(readDraft(first) == revision && readDraft(second) == revision);
  if (mode == "foreground-return") { abandoned[1] = 2; return; }
  releaseAtomic(first);
  callHost(); assert(readDraft(second) == revision); // one draft still fences
  releaseAtomic(second);
  callHost(); // admission resumes while the same foreground remains live
}

void hostEntry() {
  assert(api->resident_shell(&clients[0]) && clients[0].role == RISC_RESIDENT_ROLE_HOST);
  risc_resident_callbacks_v1 callbacks{1, sizeof(callbacks), nullptr, dispatch, nullptr, nullptr};
  assert(clients[0].register_shell(clients[0].invocation, &callbacks) == RISC_RESIDENT_OK);
  const bool hostDraft = mode == "host" || mode == "host-return";
  risc_runtime_capability_v1 first{}, second{};
  uint64_t revision = 0;
  if (hostDraft) { first = atomicGrant(); second = atomicGrant(); revision = readDraft(first); }
  risc_resident_result_v1 result{sizeof(result)};
  assert(clients[0].run_foreground(clients[0].invocation, "child.elf", &result) == RISC_RESIDENT_OK);
  assert(result.status == RISC_RESIDENT_OK && entries[1] == 1 && unmaps[1] == 1);
  if (hostDraft) {
    assert(readDraft(first) == revision && readDraft(second) == revision);
    if (mode == "host-return") { abandoned[0] = 2; return; }
    releaseAtomic(first); ordinaryGrant(); assert(readDraft(second) == revision);
    releaseAtomic(second);
  }
  ordinaryGrant(); // both explicit release and child teardown restore admission
}

bool bind(Runtime& target) {
  return target.registerAtomicKeyValue("settings-fence-host", 6, *endpoint) &&
         target.registerAtomicKeyValue("settings-fence-child", 6, *endpoint);
}
void write(const std::string& root, const char* name, const std::string& value) {
  std::ofstream output(root + "/" + name); output << value; assert(output.good());
}
void configuration(const std::string& root) {
  write(root, "board.json", R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
  write(root, "provider.json", R"({"type":"driver","id":"settings-fence-service","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"provider.elf","requires":[],"provides":[{"capability":"test.settings-fence-service","api":1}]})");
  for (const char* name : {"host", "child"}) {
    write(root, (std::string(name) + ".json").c_str(), std::string("{\"type\":\"application\",\"id\":\"settings-fence-") + name +
      "\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"file_name\":\"" + name +
      ".elf\",\"entry\":\"app_main\",\"requires\":[{\"capability\":\"storage.key-value.atomic\",\"api\":1},{\"capability\":\"test.settings-fence-service\",\"api\":1}]}");
  }
  write(root, "boot.json", R"({"board":"board.json","default_app":"host.elf","drivers":[{"manifest":"provider.json"}],"resident_shell":{"api":1,"host":"host.elf","foreground":["child.elf"]},"app_capabilities":[{"manifest":"host.json","grants":[{"capability":"storage.key-value.atomic","api":1,"instance_id":6},{"capability":"test.settings-fence-service","api":1,"instance_id":0}]},{"manifest":"child.json","grants":[{"capability":"storage.key-value.atomic","api":1,"instance_id":6},{"capability":"test.settings-fence-service","api":1,"instance_id":0}]}]})");
}
} // namespace

extern "C" void test_settings_fence_provider(unsigned event, uint32_t budget) {
  assert(!inNative);
  if (event == 0) { assert(!serviceLease); ++starts; }
  else if (event == 1) { assert(serviceLease && budget == RISC_DRIVER_SERVICE_MAX_MS); ++serviceCalls; }
  else if (event == 2) assert(!serviceLease);
  else { assert(event == 3 && !serviceLease); ++stops; }
}

extern "C" void test_settings_fence_app(unsigned event, unsigned role) {
  assert(role < 2 && !inNative && !serviceLease);
  if (event == 0) return;
  if (event == 1) {
    ++entries[role]; api = risc_runtime_get_api(1); assert(api);
    clients[role].struct_size = sizeof(clients[role]);
    if (role) childEntry(); else hostEntry();
  } else if (event == 2) {
    ++finishes[role];
    finiServices[role] = serviceCalls; finiLeases[role] = serviceBegins;
    if (abandoned[role]) {
      assert(revokingRole < 0 && liveDrafts == abandoned[role]);
      liveDrafts -= abandoned[role]; revokingRole = int(role);
    }
  } else {
    assert(event == 3 && finishes[role] == 1);
    assert(serviceCalls == finiServices[role] && serviceBegins == finiLeases[role]);
    if (abandoned[role]) assert(revokingRole < 0 && revokedBeforeUnmap == 1);
    ++unmaps[role];
  }
}

int main(int argc, char** argv) {
  assert(argc == 3); const std::string root = argv[1]; mode = argv[2];
  assert(mode == "host" || mode == "foreground" || mode == "host-return" || mode == "foreground-return");
  MemoryStore memory;
  Core core(6, memory.backend(), {}, {nullptr, [](void*, size_t size) { return std::malloc(size); }, [](void*, void* bytes) { std::free(bytes); }});
  const Core::Value initial{"sample", "stable", 6};
  assert(core.initialize(&initial, 1, {}) == Core::Ok);
  bool held = false;
  Atomic atomic(core, {&held, [](void*) { return true; }, [](void*) { return false; },
    [](void* p) { auto& value = *static_cast<bool*>(p); assert(!value); value = true; return true; },
    [](void* p) { auto& value = *static_cast<bool*>(p); assert(value); value = false; return true; },
    [](void*) { return int(Core::Ok); }});
  endpoint = &atomic;
  configuration(root);
  RiscBoot::Port port{owner, [](risc_runtime_health_v1*) { return true; }, [](uint32_t) {}, [](const char*) { return true; }, bind};
  port.nativeOwnerCheckpoint = checkpoint;
  port.providerServicesReady = [] { assert(!inNative && !serviceLease); return true; };
  port.providerServiceBegin = [] { assert(!inNative && !serviceLease); serviceLease = true; ++serviceBegins; return true; };
  port.providerServiceEnd = [] { assert(!inNative && serviceLease); serviceLease = false; ++serviceEnds; };
  Runtime actual(port); runtime = &actual;
  if (!actual.prepare(root.c_str())) { std::fprintf(stderr, "prepare: %s\n", actual.error()); return 2; }
  assert(actual.run() && !actual.retained() && !liveDrafts && !held);
  assert(starts == 1 && stops == 1 && serviceCalls && serviceBegins == serviceEnds);
  assert(entries[0] == 1 && entries[1] == 1 && finishes[0] == 1 && finishes[1] == 1 && unmaps[0] == 1 && unmaps[1] == 1);
  assert(allowed && fenced && fencedDrains && !pendingDrain && revokingRole < 0);
  assert(revokedBeforeUnmap == (mode.find("return") != std::string::npos ? 1u : 0u));
  std::printf("Resident settings draft fence %s: cross-invocation admission, accepted drain, ordinary provider service, pre-grant/release/revoke boundaries PASS\n", mode.c_str());
}
