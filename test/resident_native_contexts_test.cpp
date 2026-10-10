// This file also supplies the tiny admitted provider fixture. Both application
// roles use the unchanged resident_app.c fixture and real Runtime entry hooks.
#include <GardenPlatformV1.h>
#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <cassert>
#include <cstring>

struct NativeProbe {
  uint32_t version, size;
  int32_t (*deepSleep)(unsigned);
};

#ifdef RESIDENT_NATIVE_CONTEXTS_PROVIDER
namespace {
const garden_gpio_v1* gpio = nullptr;
uint64_t input = 0;
int32_t deepSleep(unsigned form) {
  switch (form) {
    case 0: return gpio->deep_sleep(gpio->context, input, false);
    case 1: return gpio->deep_sleep_for(gpio->context, input, false, 123);
    case 2: return gpio->deep_sleep_set(gpio->context, input, false, 0);
    case 3: return gpio->deep_sleep_set(gpio->context, input, false, 123);
    default: assert(false); return RISC_DEEP_SLEEP_INVALID;
  }
}
bool start(const risc_provider_dependency_v1* dependencies, size_t count) {
  const risc_hardware_device_v1* hardware = nullptr;
  for (size_t i = 0; i < count; ++i) {
    const auto& dependency = dependencies[i];
    if (!strcmp(dependency.capability_id, "hardware.device"))
      hardware = static_cast<const risc_hardware_device_v1*>(dependency.api);
    if (!strcmp(dependency.capability_id, "platform.gpio"))
      gpio = static_cast<const garden_gpio_v1*>(dependency.api);
  }
  if (!hardware || !gpio || gpio->struct_size < sizeof(*gpio) ||
      !gpio->deep_sleep || !gpio->deep_sleep_for || !gpio->deep_sleep_set)
    return false;
  const auto* config = static_cast<const risc_hw_gpio_bank_v1*>(hardware->config);
  return config->count == 1 &&
    gpio->claim(gpio->context, config->pins[0], false, false, true, &input);
}
bool quiesce() {
  if (input && !gpio->release(gpio->context, input)) return false;
  input = 0;
  return true;
}
void stop() { assert(!input); gpio = nullptr; }
const NativeProbe probe{1, sizeof(probe), deepSleep};
const risc_driver_v2 driver{2, sizeof(driver), "resident-native-probe",
  "test.resident-native", 1, &probe, start, stop, quiesce};
}
extern "C" __attribute__((visibility("default")))
const risc_driver_v2* t5_driver_get(uint32_t abi) { return abi == 2 ? &driver : nullptr; }
#else
#include "ports/esp32s3/CpuPort.h"
#include <array>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
using RiscBoot::Runtime;
constexpr unsigned LiveGrants = 16;
constexpr unsigned ChildRuns = 2;
constexpr uint32_t DataNamespace[] = {101, 102};
constexpr uint32_t KeyNamespace[] = {201, 202};
bool owned = true;
unsigned focusedRole = 0, starts[2]{}, entries[2]{}, finis[2]{};
unsigned dispatches = 0, hardwareCalls = 0, opens = 0, closes = 0;
unsigned arms = 0, clears = 0, timeReads = 0, timeSeeds = 0, dataCalls = 0;
unsigned keyCalls = 0;
uint32_t values[] = {0x10100000, 0x10200000};
uint64_t revisions[] = {1, 1};
int64_t epoch = 1700000000;
uint32_t nanos = 1000;
Runtime* runtime = nullptr;
RiscCpu::Port* cpu = nullptr;
const risc_runtime_api_v1* api = nullptr;
unsigned* hostVisits = nullptr;

struct Copies {
  risc_realtime_api_v1 time{};
  risc_realtime_control_api_v1 control{};
  risc_app_data_v1 data{};
};
struct Invocation {
  risc_resident_client_v1 resident{};
  std::array<risc_runtime_capability_v1, LiveGrants> grants{};
  Copies copied{};
  const NativeProbe* native = nullptr;
};
Invocation invocations[2];
std::vector<Copies> released;
std::vector<Copies> completed;

bool owner() { return owned; }
bool health(risc_runtime_health_v1*) { return true; }
void delay(uint32_t) {}
bool log(const char*) { return true; }
bool bind(Runtime& r) { return cpu->bind(r); }
bool appExitSafe() { return cpu->appExitSafe(); }
bool storageSafe() { return cpu->providerStorageSafe(); }

int32_t realtimeRead(risc_realtime_snapshot_v1* out) {
  assert(owned);
  ++timeReads;
  *out = {sizeof(*out), RISC_REALTIME_VALID, epoch, nanos, 0, 100, 102};
  return RISC_REALTIME_OK;
}
int32_t realtimeSeed(int64_t seconds, uint32_t fraction) {
  assert(owned);
  ++timeSeeds;
  epoch = seconds;
  nanos = fraction;
  return RISC_REALTIME_OK;
}
unsigned dataRole(uint32_t ns) {
  assert(owned && ns == DataNamespace[focusedRole]);
  ++dataCalls;
  return focusedRole;
}
int32_t statData(void*, uint32_t ns, const char* name, uint32_t* size, uint64_t* revision) {
  const unsigned role = dataRole(ns);
  assert(!strcmp(name, "state.bin"));
  *size = sizeof(values[role]);
  *revision = revisions[role];
  return RISC_APP_DATA_OK;
}
int32_t readData(void*, uint32_t ns, const char* name, uint64_t expected,
                 void* bytes, uint32_t capacity, uint32_t* size, uint64_t* revision) {
  const unsigned role = dataRole(ns);
  assert(!strcmp(name, "state.bin") && capacity == sizeof(values[role]));
  assert(expected == revisions[role]);
  memcpy(bytes, &values[role], sizeof(values[role]));
  *size = sizeof(values[role]);
  *revision = revisions[role];
  return RISC_APP_DATA_OK;
}
int32_t replaceData(void*, uint32_t ns, const char* name, uint64_t expected,
                    const void* bytes, uint32_t size) {
  const unsigned role = dataRole(ns);
  assert(!strcmp(name, "state.bin") && size == sizeof(values[role]));
  assert(expected == revisions[role]);
  memcpy(&values[role], bytes, size);
  ++revisions[role];
  return RISC_APP_DATA_OK;
}
int32_t getKey(void*, uint32_t ns, const char*, void* bytes, uint32_t capacity, uint32_t* size) {
  assert(owned && ns == KeyNamespace[focusedRole] && capacity >= sizeof(ns));
  ++keyCalls;
  memcpy(bytes, &ns, sizeof(ns));
  *size = sizeof(ns);
  return RISC_KEY_VALUE_OK;
}
int32_t putKey(void*, uint32_t, const char*, const void*, uint32_t) {
  assert(false);
  return RISC_KEY_VALUE_CONTEXT;
}
const RiscBoot::AppDataBackend dataBackend{nullptr, statData, readData, replaceData,
  [](void*) { return true; }};
const RiscBoot::KeyValueBackend keyBackend{nullptr, getKey, putKey};

RiscCpu::Hardware hardware() {
  RiscCpu::Hardware h{};
  h.owner = owner;
  h.now = []() -> uint64_t { return 100; };
  h.sleep = delay;
  h.gpioOpen = [](uint8_t pin, bool output, bool, bool pullup) {
    assert(pin == 7 && !output && pullup); ++opens; ++hardwareCalls; return true;
  };
  h.gpioWrite = [](uint8_t, bool) { assert(false); return false; };
  h.gpioRead = [](uint8_t pin, bool* level) {
    assert(pin == 7); ++hardwareCalls; *level = true; return true;
  };
  h.gpioPwm = [](uint8_t, uint32_t, uint16_t, uint16_t) { assert(false); return false; };
  h.gpioClose = [](uint8_t pin) {
    assert(pin == 7); ++closes; ++hardwareCalls; return true;
  };
  h.i2cOpen = [](uint8_t, uint8_t, uint8_t, uint32_t) { assert(false); return false; };
  h.i2cTransfer = [](uint8_t, uint8_t, const uint8_t*, size_t, uint8_t*, size_t, uint32_t) {
    assert(false); return false;
  };
  h.i2cClose = [](uint8_t) { assert(false); return false; };
  h.spiOpen = [](uint8_t, int16_t, int16_t, int16_t) { assert(false); return false; };
  h.spiBegin = [](uint8_t, uint8_t, uint32_t, uint8_t, uint32_t) { assert(false); return false; };
  h.spiTransfer = [](uint8_t, const uint8_t*, uint8_t*, size_t, uint32_t) { assert(false); return false; };
  h.spiEnd = [](uint8_t, uint8_t, uint32_t) { assert(false); return false; };
  h.spiClose = [](uint8_t) { assert(false); return false; };
  h.deepWakeValid = [](uint8_t pin) { assert(pin == 7); ++hardwareCalls; return true; };
  h.deepReady = []() { ++hardwareCalls; return true; };
  // Deliberately refuse at the lowest fake arm boundary. Reaching this callback
  // proves admission; successful cleanup lets the real Runtime finish normally.
  h.deepWakeArm = [](uint8_t pin, bool high, bool pullup) {
    assert(pin == 7 && !high && pullup); ++arms; ++hardwareCalls; return false;
  };
  h.deepWakeClear = [](uint8_t pin, bool pullup) {
    assert(pin == 7 && pullup); ++clears; ++hardwareCalls; return true;
  };
  h.deepSleep = []() { assert(false); };
  h.timerArm = [](uint32_t) { assert(false); return false; };
  h.timerClear = []() { ++hardwareCalls; return true; };
  h.deepWakeSetValid = [](uint64_t mask, uint64_t high) {
    assert(mask == (uint64_t(1) << 7) && !high); ++hardwareCalls; return true;
  };
  h.deepWakeSetArm = [](uint64_t mask, uint64_t high, uint64_t pulls) {
    assert(mask == (uint64_t(1) << 7) && !high && pulls == mask);
    ++arms; ++hardwareCalls; return false;
  };
  h.deepWakeSetClear = [](uint64_t mask, uint64_t high, uint64_t pulls) {
    assert(mask == (uint64_t(1) << 7) && !high && pulls == mask);
    ++clears; ++hardwareCalls; return true;
  };
  h.realtimeRead = realtimeRead;
  h.realtimeSeed = realtimeSeed;
  return h;
}

risc_runtime_capability_v1 acquire(const char* capability, uint64_t instance = 0) {
  risc_runtime_capability_v1 grant{};
  grant.struct_size = sizeof(grant);
  assert(api->acquire(capability, 1, instance, &grant));
  assert(grant.slot && grant.slot <= LiveGrants && grant.generation && grant.api);
  return grant;
}
void acquisitionDenied(const char* capability, uint64_t instance = 0) {
  risc_runtime_capability_v1 grant{};
  grant.struct_size = sizeof(grant);
  assert(!api->acquire(capability, 1, instance, &grant));
}
void timeDenied(const Copies& copy) {
  const unsigned reads = timeReads, seeds = timeSeeds;
  risc_realtime_snapshot_v1 sample;
  memset(&sample, 0xa5, sizeof(sample));
  sample.struct_size = sizeof(sample);
  const auto before = sample;
  if (copy.time.read) {
    assert(copy.time.read(copy.time.context, &sample) == RISC_REALTIME_CONTEXT);
    assert(!memcmp(&sample, &before, sizeof(sample)));
  }
  if (copy.control.read) {
    assert(copy.control.read(copy.control.context, &sample) == RISC_REALTIME_CONTEXT);
    assert(!memcmp(&sample, &before, sizeof(sample)));
    assert(copy.control.seed(copy.control.context, 1700000999, 2000) == RISC_REALTIME_CONTEXT);
  }
  assert(reads == timeReads && seeds == timeSeeds);
}
void dataDenied(const risc_app_data_v1& table) {
  if (!table.stat) return;
  const unsigned calls = dataCalls;
  uint32_t size = 99, bytes = 0xfeedface;
  uint64_t revision = 99;
  assert(table.stat(table.context, "state.bin", &size, &revision) == RISC_APP_DATA_CONTEXT);
  assert(!size && !revision);
  size = 99; revision = 99;
  assert(table.read(table.context, "state.bin", 1, &bytes, sizeof(bytes), &size, &revision) == RISC_APP_DATA_CONTEXT);
  assert(!size && !revision && bytes == 0xfeedface);
  assert(table.replace(table.context, "state.bin", 1, &bytes, sizeof(bytes)) == RISC_APP_DATA_CONTEXT);
  assert(calls == dataCalls);
}
void denied(const Copies& copy) { timeDenied(copy); dataDenied(copy.data); }
void staleDenied() {
  for (const auto& copy : released) denied(copy);
  for (const auto& copy : completed) denied(copy);
}
void dataLive(const risc_app_data_v1& table, unsigned role) {
  assert(focusedRole == role);
  const uint32_t other = values[1 - role];
  uint32_t size = 0, value = 0;
  uint64_t revision = 0, readRevision = 0;
  assert(table.stat(table.context, "state.bin", &size, &revision) == RISC_APP_DATA_OK);
  assert(size == sizeof(value) && revision == revisions[role]);
  assert(table.read(table.context, "state.bin", revision, &value, sizeof(value), &size, &readRevision) == RISC_APP_DATA_OK);
  assert(value == values[role] && size == sizeof(value) && readRevision == revision);
  ++value;
  assert(table.replace(table.context, "state.bin", revision, &value, sizeof(value)) == RISC_APP_DATA_OK);
  assert(values[role] == value && values[1 - role] == other);
}
void live(unsigned role) {
  auto& current = invocations[role];
  assert(focusedRole == role);
  risc_realtime_snapshot_v1 sample{sizeof(sample)};
  assert(current.copied.time.read(current.copied.time.context, &sample) == RISC_REALTIME_OK);
  assert(sample.validity == RISC_REALTIME_VALID && sample.epoch_seconds == epoch && sample.nanoseconds == nanos);
  const int64_t seconds = 1700000000 + role;
  assert(current.copied.control.seed(current.copied.control.context, seconds, 3000) == RISC_REALTIME_OK);
  assert(current.copied.control.read(current.copied.control.context, &sample) == RISC_REALTIME_OK);
  assert(sample.epoch_seconds == seconds && sample.nanoseconds == 3000);
  dataLive(current.copied.data, role);
  for (unsigned i = 4; i < LiveGrants; ++i) {
    const auto* key = static_cast<const risc_key_value_v1*>(current.grants[i].api);
    uint32_t ns = 0, size = 0;
    assert(key->get(key->context, "probe", &ns, sizeof(ns), &size) == RISC_KEY_VALUE_OK);
    assert(ns == KeyNamespace[role] && size == sizeof(ns));
  }
  acquisitionDenied(RISC_KEY_VALUE_CAPABILITY, KeyNamespace[role]);
}
void copyTables(Invocation& current) {
  current.copied.data = *static_cast<const risc_app_data_v1*>(current.grants[0].api);
  current.copied.time = *static_cast<const risc_realtime_api_v1*>(current.grants[1].api);
  current.copied.control = *static_cast<const risc_realtime_control_api_v1*>(current.grants[2].api);
}
void recycle(unsigned role) {
  auto& current = invocations[role];
  const auto previous = current.copied;
  auto oldGrants = current.grants;
  for (unsigned i = 0; i < 3; ++i) assert(api->release(&current.grants[i]));
  denied(previous);
  current.grants[0] = acquire(RISC_APP_DATA_CAPABILITY, DataNamespace[role]);
  current.grants[1] = acquire(RISC_REALTIME_CAPABILITY);
  current.grants[2] = acquire(RISC_REALTIME_CONTROL_CAPABILITY);
  copyTables(current);
  assert(previous.data.context != current.copied.data.context);
  assert(previous.time.context != current.copied.time.context);
  assert(previous.control.context != current.copied.control.context);
  for (unsigned i = 0; i < 3; ++i) {
    assert(oldGrants[i].slot == current.grants[i].slot);
    assert(oldGrants[i].generation != current.grants[i].generation);
    assert(!api->release(&oldGrants[i]));
  }
  released.push_back(previous);
  staleDenied();
  live(role);
}
void wrongOwner(unsigned role) {
  owned = false;
  denied(invocations[role].copied);
  acquisitionDenied(RISC_KEY_VALUE_CAPABILITY, KeyNamespace[role]);
  auto grant = invocations[role].grants[0];
  assert(!api->release(&grant));
  risc_resident_client_v1 client{};
  client.struct_size = sizeof(client);
  assert(!api->resident_shell(&client));
  owned = true;
}
void foreignGrantsDenied(unsigned role) {
  for (auto grant : invocations[role].grants) assert(!api->release(&grant));
}
void nativeBlocked(unsigned role) {
  assert(!runtime->residentResetSafe() && !cpu->restartResourcesSafe());
  const unsigned before = hardwareCalls;
  for (unsigned form = 0; form < 4; ++form)
    assert(invocations[role].native->deepSleep(form) == RISC_DEEP_SLEEP_BUSY);
  assert(hardwareCalls == before && cpu->appExitSafe());
}
void nativeAllowed() {
  assert(runtime->residentResetSafe() && cpu->restartResourcesSafe());
  for (unsigned form = 0; form < 4; ++form) {
    const unsigned beforeArms = arms, beforeClears = clears;
    assert(invocations[0].native->deepSleep(form) == RISC_DEEP_SLEEP_PLATFORM);
    assert(arms == beforeArms + 1 && clears == beforeClears + 1);
    assert(cpu->appExitSafe() && cpu->restartResourcesSafe());
  }
}
int32_t dispatch(void* context, const risc_resident_request_v1* request, risc_resident_reply_v1* reply) {
  assert(context == hostVisits && *hostVisits == 1);
  assert(request->reason == RISC_RESIDENT_CHECKPOINT_SLEEP);
  ++dispatches;
  focusedRole = 0;
  nativeBlocked(0); // A host callback still has a suspended foreground stack.
  denied(invocations[1].copied);
  foreignGrantsDenied(1);
  staleDenied();
  live(0); // Original host tables regain authority and their original namespace.
  wrongOwner(0);
  recycle(0);
  reply->flags = RISC_RESIDENT_REPLY_REDRAW;
  focusedRole = 1;
  return RISC_RESIDENT_OK;
}
void failed(void*, const risc_resident_failure_v1*) { assert(false); }
void save(const std::string& root, const char* name, const JsonDocument& document) {
  std::string bytes;
  serializeJson(document, bytes);
  std::ofstream(root + "/" + name) << bytes;
}
void setup(const std::string& root) {
  std::ofstream(root + "/board.json") << R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[{"instance_id":7,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[7],"active_high":true,"pull_up":true,"debounce_us":0,"long_press_us":0,"click_min_us":0}}]})";
  std::ofstream(root + "/probe.json") << R"({"type":"driver","id":"resident-native-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"probe.elf","requires":[{"capability":"hardware.device","api":1},{"capability":"platform.gpio","api":1}],"provides":[{"capability":"test.resident-native","api":1}],"hardware_compatibility":[{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}]})";
  JsonDocument boot;
  boot["board"] = "board.json";
  boot["default_app"] = "host.elf";
  boot["provider_activation"] = "demand";
  auto driver = boot["drivers"].to<JsonArray>().add<JsonObject>();
  driver["manifest"] = "probe.json";
  driver["instance_id"] = 7;
  auto resident = boot["resident_shell"].to<JsonObject>();
  resident["api"] = 1;
  resident["host"] = "host.elf";
  resident["foreground"].to<JsonArray>().add("child.elf");
  auto policies = boot["app_capabilities"].to<JsonArray>();
  const char* names[] = {"host", "child"};
  for (unsigned role = 0; role < 2; ++role) {
    JsonDocument app;
    app["type"] = "application";
    app["id"] = names[role];
    app["version"] = "1.0.0";
    app["architecture"] = "xtensa-esp32s3";
    app["file_name"] = std::string(names[role]) + ".elf";
    app["entry"] = "app_main";
    auto requirements = app["requires"].to<JsonArray>();
    auto policy = policies.add<JsonObject>();
    policy["manifest"] = std::string(names[role]) + ".json";
    auto grants = policy["grants"].to<JsonArray>();
    auto add = [&](const char* capability, uint32_t instance) {
      auto requirement = requirements.add<JsonObject>();
      requirement["capability"] = capability; requirement["api"] = 1;
      auto grant = grants.add<JsonObject>();
      grant["capability"] = capability; grant["api"] = 1; grant["instance_id"] = instance;
    };
    add(RISC_APP_DATA_CAPABILITY, DataNamespace[role]);
    add(RISC_REALTIME_CAPABILITY, 0);
    add(RISC_REALTIME_CONTROL_CAPABILITY, 0);
    add("test.resident-native", 7);
    add(RISC_KEY_VALUE_CAPABILITY, KeyNamespace[role]);
    save(root, (std::string(names[role]) + ".json").c_str(), app);
  }
  save(root, "boot.json", boot);
}
}

extern "C" int test_resident_init(unsigned role) {
  assert(role < 2);
  focusedRole = role;
  ++starts[role];
  invocations[role] = {};
  api = risc_runtime_get_api(1);
  assert(api && api->struct_size >= RISC_RUNTIME_RESIDENT_SHELL_V1_SIZE);
  risc_resident_client_v1 client{};
  client.struct_size = sizeof(client);
  assert(!api->resident_shell(&client));
  acquisitionDenied(RISC_REALTIME_CAPABILITY);
  acquisitionDenied(RISC_REALTIME_CONTROL_CAPABILITY);
  acquisitionDenied(RISC_APP_DATA_CAPABILITY, DataNamespace[1 - role]);
  acquisitionDenied("platform.gpio", 7);
  staleDenied();
  if (role) denied(invocations[0].copied);
  // App-data is intentionally usable during init/fini; realtime requires entry.
  auto& current = invocations[role];
  current.grants[0] = acquire(RISC_APP_DATA_CAPABILITY, DataNamespace[role]);
  current.copied.data = *static_cast<const risc_app_data_v1*>(current.grants[0].api);
  dataLive(current.copied.data, role);
  wrongOwner(role);
  return 0;
}
extern "C" void test_resident_main(unsigned role, unsigned* visits) {
  assert(role < 2 && ++*visits == 1);
  focusedRole = role;
  ++entries[role];
  auto& current = invocations[role];
  current.resident.struct_size = sizeof(current.resident);
  assert(api->resident_shell(&current.resident));
  assert(current.resident.role == (role ? RISC_RESIDENT_ROLE_FOREGROUND : RISC_RESIDENT_ROLE_HOST));
  current.grants[1] = acquire(RISC_REALTIME_CAPABILITY);
  current.grants[2] = acquire(RISC_REALTIME_CONTROL_CAPABILITY);
  current.grants[3] = acquire("test.resident-native", 7);
  current.native = static_cast<const NativeProbe*>(current.grants[3].api);
  copyTables(current);
  for (unsigned i = 4; i < LiveGrants; ++i)
    current.grants[i] = acquire(RISC_KEY_VALUE_CAPABILITY, KeyNamespace[role]);
  for (unsigned i = 0; i < LiveGrants; ++i) assert(current.grants[i].slot == i + 1);
  live(role);
  wrongOwner(role);
  staleDenied();
  if (!role) {
    hostVisits = visits;
    risc_resident_callbacks_v1 callbacks{1, sizeof(callbacks), visits, dispatch, failed};
    assert(current.resident.register_shell(current.resident.invocation, &callbacks) == RISC_RESIDENT_OK);
    nativeAllowed();
    for (unsigned run = 0; run < ChildRuns; ++run) {
      risc_resident_result_v1 result{};
      result.struct_size = sizeof(result);
      assert(current.resident.run_foreground(current.resident.invocation, "child.elf", &result) == RISC_RESIDENT_OK);
      focusedRole = 0;
      assert(result.status == RISC_RESIDENT_OK && result.invocation && finis[1] == run + 1);
      assert(*visits == 1 && starts[0] == 1 && !finis[0]);
      denied(invocations[1].copied);
      foreignGrantsDenied(1);
      staleDenied();
      live(0);
      nativeAllowed(); // Clean foreground teardown restores the ordinary path.
    }
  } else {
    denied(invocations[0].copied);
    foreignGrantsDenied(0);
    nativeBlocked(1);
    for (unsigned checkpoint = 0; checkpoint < 2; ++checkpoint) {
      const auto before = current.copied;
      risc_resident_request_v1 request{sizeof(request), RISC_RESIDENT_CHECKPOINT_SLEEP, 0, 0};
      risc_resident_reply_v1 reply{sizeof(reply), 0};
      assert(current.resident.checkpoint(current.resident.invocation, &request, &reply) == RISC_RESIDENT_OK);
      assert(reply.flags == RISC_RESIDENT_REPLY_REDRAW);
      assert(current.copied.time.context == before.time.context &&
             current.copied.control.context == before.control.context &&
             current.copied.data.context == before.data.context);
      live(1); // The same child copies regain only the child's namespace.
      denied(invocations[0].copied);
      foreignGrantsDenied(0);
      nativeBlocked(1);
      recycle(1);
    }
  }
}
extern "C" void test_resident_fini(unsigned role) {
  focusedRole = role;
  ++finis[role];
  risc_resident_client_v1 client{};
  client.struct_size = sizeof(client);
  assert(!api->resident_shell(&client));
  timeDenied(invocations[role].copied);
  acquisitionDenied(RISC_REALTIME_CAPABILITY);
  acquisitionDenied(RISC_REALTIME_CONTROL_CAPABILITY);
  dataLive(invocations[role].copied.data, role);
  wrongOwner(role);
  staleDenied();
  if (role) { denied(invocations[0].copied); nativeBlocked(1); }
  completed.push_back(invocations[role].copied);
  // Keep all sixteen grants live: real Runtime teardown must revoke them.
}
int main(int argc, char** argv) {
  assert(argc == 2);
  setup(argv[1]);
  RiscCpu::Port cpuPort(hardware());
  cpu = &cpuPort;
  RiscBoot::Port port{owner, health, delay, log, bind, &keyBackend, appExitSafe, storageSafe};
  port.appData = &dataBackend;
  Runtime running(port);
  runtime = &running;
  assert(running.prepare(argv[1]));
  assert(!hardwareCalls); // Policy/board admission must perform no hardware I/O.
  assert(running.run() && !running.retained());
  assert(starts[0] == 1 && entries[0] == 1 && finis[0] == 1);
  assert(starts[1] == ChildRuns && entries[1] == ChildRuns && finis[1] == ChildRuns);
  assert(dispatches == 2 * ChildRuns && opens == 1 && closes == 1);
  assert(arms == 4 * (ChildRuns + 1) && arms == clears);
  assert(running.residentResetSafe() && cpu->restartResourcesSafe() && cpu->quiescent());
  staleDenied();
  assert(timeReads && timeSeeds && dataCalls && keyCalls);
  puts("Resident Runtime/CpuPort/admitted ELFs: realtime/app-data focus, namespace, stale generations, owner/init/fini, 16 grants per invocation, all deep-sleep forms and restart fences PASS");
}
#endif
