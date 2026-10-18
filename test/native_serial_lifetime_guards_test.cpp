// Host-only invariant checks. Private access below is limited to admission
// fault injection and inspecting/injecting leaked graph custody. Runtime
// preparation, app execution, provider loading, parking and stop are real.
#ifdef NATIVE_LIFETIME_GUARD_PROVIDER
#include <RiscProviderV2.h>
extern "C" const void* lifetime_guard_backend(unsigned);
extern "C" bool lifetime_guard_backend_safe(unsigned);
extern "C" void lifetime_guard_provider_event(unsigned, unsigned);
namespace {
constexpr unsigned Slot = NATIVE_LIFETIME_GUARD_PROVIDER;
bool start(const risc_provider_dependency_v1*, size_t count) {
  lifetime_guard_provider_event(Slot, 0); return count == 0;
}
bool quiesce() {
  lifetime_guard_provider_event(Slot, 1); return lifetime_guard_backend_safe(Slot);
}
void stop() { lifetime_guard_provider_event(Slot, 2); }
risc_driver_v2 driver = {2, sizeof(driver), Slot ? "guard-unrelated" : "guard-files",
                        "storage.files", 1, nullptr, start, stop, quiesce};
}
extern "C" __attribute__((visibility("default")))
const risc_driver_v2* t5_driver_get(uint32_t abi) {
  if (abi != 2) return nullptr;
  driver.capability = lifetime_guard_backend(Slot); return &driver;
}
#else
#include <ArduinoJson.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#define private public
#include "bootstrap/Runtime.h"
#undef private
#include "RuntimeFileFixture.h"
#include "runtime/storage/PosixFiles.h"
#include <unistd.h>

using RiscBoot::Runtime;
using Lifetime = Runtime::NativeBindingLifetime;
namespace {
std::string root;
Runtime* runtime = nullptr;
RiscBoot::RuntimeFiles client;
std::unique_ptr<RiscStorage::PosixFiles> backends[2];
unsigned providerEvents[2][3]{};
unsigned dispatches = 0, controls = 0, shutdowns = 0, stops = 0, appHooks[3]{};
bool nativeReady = true, storageReady = true;
uint32_t openHandle = 0;
unsigned reads = 0;
bool leaveOldHandle = false;
std::vector<unsigned char> oldSessionGraph;
constexpr const char* EmptyBoard = R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
bool owner() { return true; }
bool safe() { return true; }
bool providerSafe() { return storageReady; }
bool checkpoint() {
  assert(runtime && !runtime->active() && !risc_runtime_get_api(1));
  ++dispatches;
  if (openHandle) {
    char byte = 0; uint32_t actual = 0;
    assert(client.read(openHandle, &byte, 1, &actual) == RISC_FILES_OK);
    assert(actual == 1 && byte == char('a' + reads)); ++reads;
  }
  return true;
}
bool control() {
  assert(runtime && !runtime->active() && !risc_runtime_get_api(1));
  ++controls; return true;
}
bool shutdown() {
  ++shutdowns;
  if (leaveOldHandle && shutdowns == 2) {
    // Inject an endpoint that incorrectly reports successful old-session drain.
    // Runtime must independently reject the still-open native client handle.
    oldSessionGraph.resize(sizeof(runtime->graph_));
    std::memcpy(oldSessionGraph.data(), &runtime->graph_, oldSessionGraph.size());
  }
  return true;
}
bool stop() { ++stops; return true; }
RiscBoot::Port port() {
  RiscBoot::Port p{owner, [](risc_runtime_health_v1*) { return true; },
    [](uint32_t) {}, [](const char*) { return true; }};
  p.appExitSafe = safe; p.providerStorageSafe = providerSafe;
  p.serialFilesReady = [] { return nativeReady; };
  p.serialFilesCheckpoint = checkpoint; p.serialFilesControlCheckpoint = control;
  p.serialFilesShutdown = shutdown; p.serialFilesStop = stop;
  return p;
}
void write(const char* name, const std::string& bytes) {
  std::ofstream file(root + "/" + name, std::ios::binary); file << bytes;
  file.close(); assert(file);
}
std::string manifest(const char* id, const char* capability, const char* needs = "[]",
                     const char* elf = "provider.elf") {
  return std::string("{\"type\":\"driver\",\"id\":\"") + id +
    "\",\"version\":\"1.0.0\",\"architecture\":\"xtensa-esp32s3\",\"driver_abi\":2,\"file_name\":\"" +
    elf + "\",\"requires\":" + needs + ",\"provides\":[{\"capability\":\"" + capability + "\",\"api\":1}]}";
}
void inputs(bool admission) {
  write("board.json", EmptyBoard);
  write("provider.json", manifest("guard-files", "storage.files", admission ?
    R"([{"capability":"guard.middle","api":1}])" : "[]"));
  write("middle.json", manifest("guard-middle", "guard.middle",
    R"([{"capability":"guard.leaf","api":1}])"));
  write("leaf.json", manifest("guard-leaf", "guard.leaf"));
  write("unrelated.json", manifest("guard-unrelated", "storage.files", "[]", "unrelated.elf"));
  write("app.json", R"({"type":"application","id":"lifetime-guards","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[]})");
  const std::string drivers = admission ?
    R"([{"manifest":"provider.json"},{"manifest":"middle.json"},{"manifest":"leaf.json"},{"manifest":"unrelated.json"}])" :
    R"([{"manifest":"provider.json"}])";
  write("boot.json", "{\"board\":\"board.json\",\"default_app\":\"default.elf\",\"provider_activation\":\"demand\",\"drivers\":" +
    drivers + R"(,"app_capabilities":[{"manifest":"app.json","grants":[]}]})");
  write("file", "abcdef");
}
void prepare(Runtime& r, RiscTest::FileSources& sources) {
  assert(sources.bind(r));
  if (!r.prepare(root.c_str())) { std::fprintf(stderr, "prepare: %s\n", r.error()); std::abort(); }
  assert(r.prepared_ && !r.retained() && !r.graph_.liveGrants());
}
void clearAdmissionBinding(Runtime& r, RiscBoot::RuntimeFiles& files) {
  // Test-only removal of an unactivated admission record. No graph grant or
  // file binding exists, and no production lifecycle is bypassed on live data.
  for (auto& binding : r.nativeFiles_) if (binding.files == &files) {
    assert(!binding.grant.slot && !files.bound()); binding = {}; return;
  }
  assert(false);
}
void admissionCheck(Runtime& r, bool compatible, const char* label, bool checkRun = true) {
  assert(r.firmwareFilesCompatible(0) == compatible);
  RiscBoot::RuntimeFiles candidate;
  assert(r.bindNativeFiles("guard-files", 0, candidate, owner, safe, false, Lifetime::Firmware) == compatible);
  assert(!candidate.bound() && !candidate.retained() && !r.graph_.liveGrants());
  if (compatible) clearAdmissionBinding(r, candidate);
  if (checkRun) {
    // Omitting the lifetime argument proves the existing Run default remains.
    assert(r.bindNativeFiles("guard-files", 0, candidate, owner, safe));
    assert(r.nativeFiles_[0].lifetime == Lifetime::Run);
    clearAdmissionBinding(r, candidate);
  }
  std::printf("admission %-42s firmware=%s%s PASS\n", label,
              compatible ? "accepted" : "rejected", checkRun ? " default-Run=accepted" : "");
}
void testAdmission() {
  inputs(true);
  RiscTest::FileSources sources({root.c_str()});
  Runtime r(port()); prepare(r, sources);
  assert(r.driverCount_ == 4 && r.drivers_[0].count == 1 && r.drivers_[1].count == 1);
  assert(!std::strcmp(r.drivers_[0].requirements[0].providerId, "guard-middle"));
  assert(!std::strcmp(r.drivers_[1].requirements[0].providerId, "guard-leaf"));
  admissionCheck(r, true, "prepared resolved ordinary dependency chain");
  // Unrelated Runtime-bound services are not a reason to reject this root.
  r.providerStorage_[3].needsTcp = true;
  admissionCheck(r, true, "unrelated Runtime-bound provider excluded");
  // Image-source metadata is an explicit private-access injection because the
  // manifest fixture's images normally come from the native test boot source.
  r.drivers_[1].sourceDriver = 3;
  admissionCheck(r, false, "transitive image-source provider included");
  r.providerStorage_[3].needsTcp = false;
  admissionCheck(r, true, "safe ordinary and image-source closure");
  const char* nodes[] = {"root", "ordinary dependency", "transitive dependency", "image source"};
  const char* fields[] = {"count", "fileCount", "needsEntropy", "needsTcp", "needsRealtime"};
  for (size_t node = 0; node != 4; ++node) {
    for (size_t field = 0; field != 5; ++field) {
      auto& storage = r.providerStorage_[node];
      switch (field) {
        case 0: storage.count = 1; break;
        case 1: storage.fileCount = 1; break;
        case 2: storage.needsEntropy = true; break;
        case 3: storage.needsTcp = true; break;
        case 4: storage.needsRealtime = true; break;
      }
      const std::string label = std::string(nodes[node]) + ": " + fields[field];
      admissionCheck(r, false, label.c_str());
      storage.count = storage.fileCount = 0;
      storage.needsEntropy = storage.needsTcp = storage.needsRealtime = false;
    }
    for (const char* requirement : {"platform.installation", "platform.bank-store", "platform.provision-request"}) {
      auto& driver = r.drivers_[node];
      static const unsigned trusted = 1;
      const auto previous = driver.requirements[driver.count];
      // Even a trusted table must not bypass the Runtime-owned broker denylist.
      driver.requirements[driver.count++] = {requirement, 1, nullptr, 0, &trusted};
      const std::string label = std::string(nodes[node]) + ": " + requirement;
      admissionCheck(r, false, label.c_str());
      driver.requirements[--driver.count] = previous;
    }
  }
  const auto oldRequirement = r.drivers_[0].requirements[0];
  for (const char* missing : {static_cast<const char*>(nullptr), "absent-provider"}) {
    r.drivers_[0].requirements[0].providerId = missing;
    admissionCheck(r, false, missing ? "missing resolved dependency" : "unresolved dependency", false);
  }
  r.drivers_[0].requirements[0] = oldRequirement;
  ++r.drivers_[0].requirements[0].providerInstance;
  admissionCheck(r, false, "dependency instance mismatch", false);
  r.drivers_[0].requirements[0] = oldRequirement;
  ++r.drivers_[0].requirements[0].api;
  admissionCheck(r, false, "dependency API mismatch", false);
  r.drivers_[0].requirements[0] = oldRequirement;
  const auto unrelated = r.drivers_[3];
  std::strcpy(r.drivers_[3].id, r.drivers_[1].id);
  std::strcpy(r.drivers_[3].provides, r.drivers_[1].provides);
  admissionCheck(r, false, "ambiguous resolved dependency", false);
  r.drivers_[3] = unrelated;
  r.drivers_[1].sourceDriver = int(r.driverCount_);
  admissionCheck(r, false, "missing image-source provider", false);
  r.drivers_[1].sourceDriver = 3;
  assert(!r.firmwareFilesCompatible(r.driverCount_));
  const auto count = r.driverCount_;
  r.driverCount_ = Runtime::MaxDrivers + 1;
  assert(!r.firmwareFilesCompatible(0));
  r.driverCount_ = count;
  for (bool missingShutdown : {true, false}) {
    if (missingShutdown) r.port_.serialFilesShutdown = nullptr;
    else r.port_.serialFilesStop = nullptr;
    RiscBoot::RuntimeFiles candidate;
    assert(r.firmwareFilesCompatible(0));
    assert(!r.bindNativeFiles("guard-files", 0, candidate, owner, safe, false, Lifetime::Firmware));
    assert(r.bindNativeFiles("guard-files", 0, candidate, owner, safe));
    clearAdmissionBinding(r, candidate);
    r.port_.serialFilesShutdown = shutdown; r.port_.serialFilesStop = stop;
    std::printf("admission missing serialFiles%s: Firmware rejected / default Run accepted PASS\n",
                missingShutdown ? "Shutdown" : "Stop");
  }
  admissionCheck(r, true, "restored closure and mandatory callbacks");
  assert(!providerEvents[0][0] && !providerEvents[1][0]);
  puts("Runtime firmware admission guards: prepared metadata, private-access fault injections PASS");
}
std::vector<unsigned char> graphBytes() {
  std::vector<unsigned char> bytes(sizeof(runtime->graph_));
  std::memcpy(bytes.data(), &runtime->graph_, bytes.size()); return bytes;
}
void unchanged(const std::vector<unsigned char>& bytes) {
  assert(!std::memcmp(bytes.data(), &runtime->graph_, bytes.size()));
}
void testParked(const std::string& mode) {
  inputs(false);
  RiscTest::FileSources sources({root.c_str()});
  backends[0].reset(new RiscStorage::PosixFiles(root.c_str()));
  backends[1].reset(new RiscStorage::PosixFiles(root.c_str()));
  runtime = new Runtime(port()); prepare(*runtime, sources);
  const bool runOnly = mode == "default-run";
  if (runOnly) assert(runtime->bindNativeFiles("guard-files", 0, client, owner, safe));
  else assert(runtime->bindNativeFiles("guard-files", 0, client, owner, safe, false, Lifetime::Firmware));
  leaveOldHandle = mode == "undrained-old-session";
  const bool ran = runtime->run();
  if (leaveOldHandle) {
    assert(!ran && runtime->retained() && client.retained() && client.bound());
    assert(openHandle && !backends[0]->exitSafe());
    assert(runtime->nativeFilesState_ != Runtime::NativeFilesState::Parked);
    assert(std::strstr(runtime->error(), "native file provider shutdown retained"));
    assert(runtime->graph_.liveGrants() == 1 && oldSessionGraph.size() == sizeof(runtime->graph_));
    unchanged(oldSessionGraph);
    assert(providerEvents[0][0] == 1 && providerEvents[0][1] == 0 && providerEvents[0][2] == 0);
    assert(shutdowns == 2 && stops == 0);
    assert(!runtime->nativeFilesCheckpoint() && !runtime->stopNativeFiles()); unchanged(oldSessionGraph);
    puts("Undrained old-session handle rejects parking and retains native graph without cleanup PASS");
    puts("Intentional retained-custody process exit: native provider and open handle remain pinned");
    std::fflush(stdout); _exit(0);
  }
  assert(ran && !runtime->retained() && !runtime->active() && !risc_runtime_get_api(1));
  assert(appHooks[0] == 1 && appHooks[1] == 1 && appHooks[2] == 1);
  assert(shutdowns == 2 && stops == 0 && runtime->nativeFilesServiceSafe());
  if (runOnly) {
    assert(!client.bound() && runtime->graph_.liveGrants() == 0);
    assert(providerEvents[0][0] == 1 && providerEvents[0][1] == 1 && providerEvents[0][2] == 1);
    assert(runtime->stopNativeFiles()); delete runtime; runtime = nullptr;
    puts("Default Run lifetime retires provider at clean Runtime return PASS"); return;
  }
  assert(client.bound() && runtime->graph_.liveGrants() == 1);
  assert(providerEvents[0][0] == 1 && providerEvents[0][1] == 0 && providerEvents[0][2] == 0);
  if (mode == "readiness") {
    assert(client.open("/file", RISC_FILES_READ, &openHandle) == RISC_FILES_OK);
    assert(!client.exitSafe() && runtime->nativeFilesServiceSafe());
    const auto before = graphBytes(); const unsigned full = dispatches;
    for (bool storage : {false, true}) {
      nativeReady = storage; storageReady = !storage;
      const unsigned controlBefore = controls;
      for (unsigned i = 0; i != 3; ++i) {
        assert(runtime->nativeFilesCheckpoint());
        assert(dispatches == full && controls == controlBefore + i + 1 && reads == 0);
        assert(!runtime->retained() && !client.retained() && client.bound()); unchanged(before);
      }
    }
    nativeReady = storageReady = true;
    assert(runtime->nativeFilesCheckpoint() && dispatches == full + 1 && reads == 1);
    assert(runtime->nativeFilesCheckpoint() && dispatches == full + 2 && reads == 2);
    assert(!runtime->retained() && !client.retained()); unchanged(before);
    assert(client.close(openHandle) == RISC_FILES_OK); openHandle = 0;
    assert(runtime->stopNativeFiles() && runtime->stopNativeFiles());
    assert(!client.bound() && runtime->graph_.liveGrants() == 0 && stops == 1);
    assert(providerEvents[0][1] == 1 && providerEvents[0][2] == 1);
    delete runtime; runtime = nullptr;
    puts("Parked readiness false dispatches control only; true resumes healthy open handle without retention PASS");
    return;
  }
  assert(mode == "leaked-grant" || mode == "leaked-consumer");
  assert(client.open("/file", RISC_FILES_READ, &openHandle) == RISC_FILES_OK);
  assert(!backends[0]->exitSafe() && runtime->nativeFilesServiceSafe());
  // Deliberate private-access custody fault: add an ordinary host fixture
  // directly to the graph after parking, bypassing Runtime metadata admission.
  // Its real module has no Runtime-bound start lease, so the graph can acquire
  // it outside currentRuntime. Optionally leave only an unattributed consumer.
  const std::string image = root + "/unrelated.elf";
  assert(runtime->graph_.addVerified({"guard-unrelated", image.c_str(), "storage.files", 1, nullptr, 0}));
  const auto foreign = runtime->graph_.acquireFrom("guard-unrelated", "storage.files", 1);
  assert(foreign.slot && runtime->graph_.interfaceFor(foreign));
  if (mode == "leaked-consumer") {
    assert(runtime->graph_.nodes_[1].module.pinConsumer());
    assert(runtime->graph_.release(foreign));
    assert(runtime->graph_.liveGrants() == 1 && runtime->graph_.nodes_[1].module.consumers() == 1);
  } else assert(runtime->graph_.liveGrants() == 2);
  const auto before = graphBytes();
  unsigned callbacks[2][3]; std::memcpy(callbacks, providerEvents, sizeof(callbacks));
  const unsigned oldDispatches = dispatches, oldControls = controls, oldShutdowns = shutdowns, oldStops = stops;
  assert(!runtime->nativeFilesServiceSafe() && !runtime->retained()); unchanged(before);
  assert(!runtime->nativeFilesCheckpoint() && runtime->retained() && client.retained());
  assert(std::strstr(runtime->error(), "parked custody retained")); unchanged(before);
  assert(!runtime->nativeFilesCheckpoint() && !runtime->stopNativeFiles()); unchanged(before);
  assert(!std::memcmp(callbacks, providerEvents, sizeof(callbacks)));
  assert(dispatches == oldDispatches && controls == oldControls && shutdowns == oldShutdowns && stops == oldStops);
  assert(openHandle && !backends[0]->exitSafe());
  assert(client.bound() && runtime->graph_.nodes_[0].module.state() == RuntimeProviders::ModuleV2::State::Active);
  assert(runtime->graph_.nodes_[1].module.state() == RuntimeProviders::ModuleV2::State::Active);
  std::printf("Parked %s latches retention: graph bytes, loaded modules, handles and all cleanup callbacks unchanged PASS\n", mode.c_str());
  puts("Intentional retained-custody process exit: no Runtime/provider destruction or cleanup attempted");
  std::fflush(stdout); _exit(0);
}
}
extern "C" const void* lifetime_guard_backend(unsigned slot) {
  assert(slot < 2 && backends[slot]); return backends[slot]->api();
}
extern "C" bool lifetime_guard_backend_safe(unsigned slot) {
  assert(slot < 2 && backends[slot]); return backends[slot]->exitSafe();
}
extern "C" void lifetime_guard_provider_event(unsigned slot, unsigned event) {
  assert(slot < 2 && event < 3); ++providerEvents[slot][event];
}
extern "C" void test_atomic_app(unsigned hook, unsigned) {
  assert(hook < 3); ++appHooks[hook];
  const auto* api = risc_runtime_get_api(1); assert(api && runtime->active());
  assert(!runtime->nativeFilesServiceSafe());
  if (hook == 1) {
    assert(client.bound()); assert(api->service_checkpoint() == RISC_RUNTIME_CHECKPOINT_OK);
    if (leaveOldHandle) assert(client.open("/file", RISC_FILES_READ, &openHandle) == RISC_FILES_OK);
  } else assert(api->service_checkpoint() == RISC_RUNTIME_CHECKPOINT_CONTEXT);
}
int main(int argc, char** argv) {
  assert(argc == 3); root = argv[1]; const std::string mode = argv[2];
  if (mode == "admission") testAdmission(); else testParked(mode);
}
#endif
