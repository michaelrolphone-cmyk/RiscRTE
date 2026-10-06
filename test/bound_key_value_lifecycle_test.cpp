#include "bootstrap/Runtime.h"
#include "bootstrap/KeyValueGeneration.h"
#include <RiscBoundKeyValueV1.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <map>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// Real Runtime, Graph, Module and dynamic app/provider images. Only the final
// backend and owner/native-retention state are modeled. No hardware claim.
static std::string root;
static RiscBoot::Runtime* running;
static bool owned = true, exitSafe = true, retaining = false, failStart = false;
static const void* providerImages[2]{};
static unsigned reads, writes, appRuns, starts[2], unloads[2], platformBindings;
static std::vector<std::string> events;
static std::map<std::pair<uint32_t, std::string>, std::string> values;
static risc_bound_key_value_v1 saved[2]{}, stale[2]{};
static bool owner() { return owned; }
static bool safe() { return exitSafe; }
static bool health(risc_runtime_health_v1*) { return true; }
static bool logLine(const char*) { return true; }
static void delay(uint32_t) {}
static unsigned calls() { return reads + writes; }
static void deny(const risc_bound_key_value_v1& kv) {
  if (!kv.get) return;
  const unsigned before = calls(); char data = 42; uint32_t size = 999;
  assert(kv.get(kv.context, "alarm_occ", &data, 1, &size) == RISC_BOUND_KEY_VALUE_CONTEXT);
  assert(size == 0 && data == 42);
  assert(kv.put(kv.context, "alarm_occ", &data, 1) == RISC_BOUND_KEY_VALUE_CONTEXT);
  assert(calls() == before);
}
extern "C" void test_bound_event(unsigned index, const char* event) {
  assert(index < 2); events.push_back(std::to_string(index) + ":" + event);
  if (!strcmp(event, "start")) ++starts[index];
  if (!strcmp(event, "unloaded")) ++unloads[index];
}
extern "C" void test_bound_image(unsigned index, const void* image) { assert(index < 2); providerImages[index] = image; }
extern "C" void test_bound_started(unsigned index, risc_bound_key_value_v1 table) {
  assert(index < 2 && running && !running->active() && !risc_runtime_get_api(1));
  // Replacement Runtime may allocate the exact same table address, but cannot
  // revive copied callback/context pairs from the previous boot/session.
  deny(stale[0]); deny(stale[1]);
  if (stale[index].context) assert(stale[index].context != table.context);
  saved[index] = table;
  if (index) assert(saved[0].context != saved[1].context);
}
extern "C" void test_bound_denied(unsigned index, risc_bound_key_value_v1 table) {
  assert(index < 2); deny(table);
}
extern "C" int test_bound_should_fail(unsigned index) { return failStart && !index; }
extern "C" unsigned test_bound_app_enter() { assert(running->active()); return appRuns++; }
static int32_t get(void*, uint32_t ns, const char* key, void* data, uint32_t capacity, uint32_t* size) {
  ++reads; assert(ns >= 1 && ns <= INT32_MAX && data && capacity == 64 && size); *size = 0;
  if (!strcmp(key, "partial")) { memset(data, 0, capacity); *size = 4; return RISC_KEY_VALUE_IO; }
  if (!strcmp(key, "oversize")) { *size = 65; return RISC_KEY_VALUE_OK; }
  if (!strcmp(key, "empty")) return RISC_KEY_VALUE_OK;
  if (!strcmp(key, "badstatus")) return 777;
  const auto found = values.find({ns, key});
  if (found == values.end()) return RISC_KEY_VALUE_NOT_FOUND;
  *size = found->second.size(); assert(*size <= capacity);
  memcpy(data, found->second.data(), *size); return RISC_KEY_VALUE_OK;
}
static int32_t put(void*, uint32_t ns, const char* key, const void* data, uint32_t size) {
  ++writes; assert(ns >= 1 && ns <= INT32_MAX && data && size && size <= 64);
  values[{ns, key}] = std::string(static_cast<const char*>(data), size);
  // Deliberately persist before returning IO. Broker must not retry or imply
  // rollback when the backend's durable result is uncertain.
  return !strcmp(key, "fail") ? 77 : RISC_KEY_VALUE_OK;
}
static const RiscBoot::KeyValueBackend backend{nullptr, get, put};
static void live(unsigned index) {
  const auto& kv = saved[index]; const char* expected = index ? "owned-1" : "owned-0";
  char data[64]; uint32_t size = 999; const unsigned before = reads;
  assert(kv.get(kv.context, "alarm_occ", data, sizeof(data), &size) == RISC_BOUND_KEY_VALUE_OK);
  assert(size == strlen(expected) && !memcmp(data, expected, size) && reads == before + 1);
}
extern "C" void test_bound_live() { live(0); live(1); }
extern "C" void test_bound_probe() {
  const auto& kv = saved[0]; char data[65]; memset(data, 0xa5, sizeof(data)); uint32_t size = 999;
  unsigned before = calls();
  const char* badKeys[] = {"", "UPPER", "bad/key", "abcdefghijklmnop", "bad key", "caf\xc3\xa9", nullptr};
  for (const char* key : badKeys) {
    size = 999;
    assert(kv.get(kv.context, key, data, 64, &size) == RISC_BOUND_KEY_VALUE_INVALID && size == 0);
    assert(kv.put(kv.context, key, data, 1) == RISC_BOUND_KEY_VALUE_INVALID);
  }
  assert(kv.get(kv.context, "alarm_occ", data, 64, nullptr) == RISC_BOUND_KEY_VALUE_INVALID);
  size = 999; assert(kv.get(kv.context, "alarm_occ", nullptr, 1, &size) == RISC_BOUND_KEY_VALUE_INVALID && size == 0);
  assert(kv.put(kv.context, "alarm_occ", data, 0) == RISC_BOUND_KEY_VALUE_INVALID);
  assert(kv.put(kv.context, "alarm_occ", data, 65) == RISC_BOUND_KEY_VALUE_INVALID);
  assert(kv.put(kv.context, "alarm_occ", nullptr, 1) == RISC_BOUND_KEY_VALUE_INVALID);
  for (const char* key : {"another_mode", "unknown", "alarm_occ.x"}) {
    size = 999; assert(kv.get(kv.context, key, data, 64, &size) == RISC_BOUND_KEY_VALUE_CONTEXT && size == 0);
    assert(kv.put(kv.context, key, data, 1) == RISC_BOUND_KEY_VALUE_CONTEXT);
  }
  for (const char* key : {"alert_mode", "alarm_cfg"})
    assert(kv.put(kv.context, key, data, 1) == RISC_BOUND_KEY_VALUE_CONTEXT);
  assert(calls() == before && static_cast<unsigned char>(data[0]) == 0xa5);
  // An existing namespace1 key is invisible without that exact key mapping.
  assert(values.at({1, "another_mode"}) == "secret");
  risc_bound_key_value_v1 null = kv; null.context = nullptr; deny(null);
  owned = false; deny(kv); deny(saved[1]); owned = true;
  live(0); live(1); // Wrong-task denial does not mint or replace either token.
  before = reads;
  size = 999; assert(kv.get(kv.context, "alert_mode", nullptr, 0, &size) == RISC_BOUND_KEY_VALUE_BUFFER_SMALL && size == 5);
  size = 999; assert(kv.get(kv.context, "alert_mode", data, 4, &size) == RISC_BOUND_KEY_VALUE_BUFFER_SMALL && size == 5);
  assert(reads == before + 2 && static_cast<unsigned char>(data[0]) == 0xa5);
  size = 999; assert(kv.get(kv.context, "alarm_cfg", data, 64, &size) == RISC_BOUND_KEY_VALUE_NOT_FOUND && size == 0);
  for (const char* key : {"partial", "oversize", "empty", "badstatus"}) {
    before = reads; size = 999;
    assert(kv.get(kv.context, key, data, 64, &size) == RISC_BOUND_KEY_VALUE_IO && size == 0);
    assert(reads == before + 1 && static_cast<unsigned char>(data[0]) == 0xa5);
  }
  before = writes;
  assert(kv.put(kv.context, "fail", "uncertain", 9) == RISC_BOUND_KEY_VALUE_IO);
  assert(writes == before + 1 && values.at({4, "fail"}) == "uncertain");
  before = writes;
  assert(kv.put(kv.context, "alarm_occ", data, 64) == RISC_BOUND_KEY_VALUE_OK && writes == before + 1);
  assert(values.at({4, "alarm_occ"}).size() == 64);
  assert(kv.put(kv.context, "alarm_occ", "owned-0", 7) == RISC_BOUND_KEY_VALUE_OK);
  // The second selected provider has a different exact map despite the same
  // key spelling, and never inherits the first provider's fault-test keys.
  before = calls(); size = 999;
  assert(saved[1].get(saved[1].context, "partial", data, 64, &size) == RISC_BOUND_KEY_VALUE_CONTEXT && size == 0);
  assert(saved[1].put(saved[1].context, "fail", data, 1) == RISC_BOUND_KEY_VALUE_CONTEXT && calls() == before);
  size = 999; before = reads;
  assert(saved[1].get(saved[1].context, "abcdefghijklmno", data, 64, &size) == RISC_BOUND_KEY_VALUE_OK);
  assert(size == 4 && !memcmp(data, "edge", 4) && reads == before + 1);
  before = writes;
  assert(saved[1].put(saved[1].context, "a-._09", "x", 1) == RISC_BOUND_KEY_VALUE_OK);
  assert(writes == before + 1 && values.at({2, "a-._09"}) == "x");
}
extern "C" int test_bound_retention() {
  if (!retaining) return 0;
  assert(running->active() && appRuns == 1);
  const size_t previousEvents = events.size(); const unsigned before = calls();
  exitSafe = false;
  deny(saved[0]); deny(saved[1]); // Still inside app_main, before appExitBarrier.
  exitSafe = true;
  deny(saved[0]); deny(saved[1]); // A transiently clear barrier cannot resurrect.
  assert(calls() == before && events.size() == previousEvents);
  exitSafe = false;
  return 1;
}
static void file(const char* name, const std::string& content) { std::ofstream(root + "/" + name) << content; }
static std::string encode(const JsonDocument& doc) { std::string text; serializeJson(doc, text); return text; }
static JsonDocument parseDoc(const std::string& value) { JsonDocument doc; assert(!deserializeJson(doc, value)); return doc; }
static const char* emptyBoard = R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})";
static const char* requirement = R"([{"capability":"storage.key-value.bound","api":1}])";
static const char* primaryMap = R"([{"key":"alert_mode","namespace":1,"access":"read"},{"key":"alarm_cfg","namespace":3,"access":"read"},{"key":"alarm_occ","namespace":4,"access":"read-write"},{"key":"partial","namespace":4,"access":"read"},{"key":"oversize","namespace":4,"access":"read"},{"key":"empty","namespace":4,"access":"read"},{"key":"badstatus","namespace":4,"access":"read"},{"key":"fail","namespace":4,"access":"read-write"}])";
static const char* secondaryMap = R"([{"key":"alert_mode","namespace":2,"access":"read"},{"key":"alarm_occ","namespace":5,"access":"read-write"},{"key":"abcdefghijklmno","namespace":2147483647,"access":"read"},{"key":"a-._09","namespace":2,"access":"read-write"}])";
static std::string driver(unsigned index, const std::string& required = requirement) {
  return std::string(R"({"type":"driver","id":")") + (index ? "bound-second" : "bound-first") +
    R"(","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"provider)" + std::to_string(index) +
    R"(.elf","requires":)" + required + R"(,"provides":[{"capability":")" + (index ? "test.bound.second" : "test.bound.first") + R"(","api":1}]})";
}
static std::string app(const char* name) {
  return std::string(R"({"type":"application","id":"bound-)") + name + R"(","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":")" + name +
    R"(.elf","entry":"app_main","requires":[{"capability":"test.bound.first","api":1},{"capability":"test.bound.second","api":1},{"capability":"storage.key-value","api":1}]})";
}
static std::string boot() {
  std::string text = std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"first.json","key_value":)") + primaryMap +
    R"(},{"manifest":"second.json","key_value":)" + secondaryMap + R"(}],"app_capabilities":[)";
  bool comma = false;
  for (const char* name : {"default", "child", "third"}) {
    if (comma) text += ',';
    comma = true;
    text += std::string(R"({"manifest":")") + name + R"(.json","grants":[{"capability":"test.bound.first","api":1,"instance_id":0},{"capability":"test.bound.second","api":1,"instance_id":0},{"capability":"storage.key-value","api":1,"instance_id":3}]})";
  }
  return text + "]}";
}
static void fixtures() {
  file("board.json", emptyBoard); file("first.json", driver(0)); file("second.json", driver(1));
  for (const char* name : {"default", "child", "third"}) file((std::string(name) + ".json").c_str(), app(name));
  file("boot.json", boot());
}
static bool bindAdmission(RiscBoot::Runtime&) { ++platformBindings; return true; }
static void expectAdmission(bool accepted, const RiscBoot::KeyValueBackend* source = &backend, bool beforeBinding = false) {
  const size_t beforeEvents = events.size(); const unsigned before = calls(), previousBindings = platformBindings;
  RiscBoot::Runtime runtime({owner, health, delay, logLine, bindAdmission, source, safe});
  const bool result = runtime.prepare(root.c_str());
  if (result != accepted) fprintf(stderr, "Unexpected prepare=%d, expected=%d, error=%s\n", result, accepted, runtime.error());
  assert(result == accepted);
  if (!accepted) assert(!runtime.run());
  assert(events.size() == beforeEvents && calls() == before);
  if (beforeBinding) assert(platformBindings == previousBindings);
  if (accepted) assert(platformBindings == previousBindings + 1);
}
static void schema() {
  fixtures(); expectAdmission(true);
  // No malformed policy may cause even dlopen constructors/descriptor calls.
  for (const char* value : {"null", "[]", "{}", "false", "1", "\"map\"", "[null]", "[false]", "[1]", "[{}]"}) {
    auto doc = parseDoc(boot()); doc["drivers"][0]["key_value"].set(parseDoc(value).as<JsonVariantConst>()); file("boot.json", encode(doc)); expectAdmission(false);
  }
  const auto invalidEntry = [&](const std::string& value) {
    auto doc = parseDoc(boot()); doc["drivers"][0]["key_value"].set(parseDoc("[" + value + "]").as<JsonVariantConst>());
    file("boot.json", encode(doc)); expectAdmission(false);
  };
  for (const char* key : {"", "UPPER", "bad/key", "bad key", "abcdefghijklmnop", "caf\\u00e9", "mode\\u0000tail"})
    invalidEntry(std::string("{\"key\":\"") + key + "\",\"namespace\":1,\"access\":\"read\"}");
  for (const char* ns : {"0", "-1", "2147483648", "1.5", "true", "null", "\"1\""})
    invalidEntry(std::string("{\"key\":\"mode\",\"namespace\":") + ns + ",\"access\":\"read\"}");
  for (const char* access : {"write", "READ", "readonly", "", "read_write"})
    invalidEntry(std::string("{\"key\":\"mode\",\"namespace\":1,\"access\":\"") + access + "\"}");
  for (const char* entry : {R"({"key":"mode","namespace":1})", R"({"key":"mode","access":"read"})", R"({"namespace":1,"access":"read"})",
      R"({"key":"mode","namespace":1,"access":null})", R"({"key":1,"namespace":1,"access":"read"})", R"({"key":"mode","namespace":1,"access":"read","extra":1})"}) invalidEntry(entry);
  auto doc = parseDoc(boot()); doc["drivers"][0].remove("key_value"); file("boot.json", encode(doc)); expectAdmission(false);
  doc = parseDoc(boot()); doc["drivers"][0]["key_value"][1]["key"] = "alert_mode"; file("boot.json", encode(doc)); expectAdmission(false);
  // The ninth exact authorization is admitted; a tenth rejects before even
  // platform binding, ELF constructors or storage/hardware backend activity.
  doc = parseDoc(boot()); doc["drivers"][0]["key_value"].as<JsonArray>().add(parseDoc(R"({"key":"ninth","namespace":1,"access":"read"})").as<JsonVariantConst>()); file("boot.json", encode(doc)); expectAdmission(true);
  doc["drivers"][0]["key_value"].as<JsonArray>().add(parseDoc(R"({"key":"tenth","namespace":1,"access":"read"})").as<JsonVariantConst>()); file("boot.json", encode(doc)); expectAdmission(false, &backend, true);
  fixtures(); file("first.json", driver(0, "[]")); expectAdmission(false); // Unused map.
  fixtures(); file("first.json", driver(0, R"([{"capability":"storage.key-value.bound","api":2}])")); expectAdmission(false);
  fixtures(); file("first.json", driver(0, R"([{"capability":"storage.key-value.bound","api":1},{"capability":"storage.key-value.bound","api":1}])")); expectAdmission(false);
  fixtures(); doc = parseDoc(driver(0)); doc["provides"][0]["capability"] = RISC_BOUND_KEY_VALUE_CAPABILITY; file("first.json", encode(doc)); expectAdmission(false);
  // Requirement without policy never falls back to an ordinary ELF provider.
  fixtures(); doc = parseDoc(boot()); doc["drivers"][0].remove("key_value"); file("boot.json", encode(doc));
  auto other = parseDoc(driver(1, "[]")); other["provides"][0]["capability"] = RISC_BOUND_KEY_VALUE_CAPABILITY; file("second.json", encode(other)); expectAdmission(false);
  fixtures(); expectAdmission(false, nullptr);
  RiscBoot::KeyValueBackend incomplete{nullptr, nullptr, put}; expectAdmission(false, &incomplete);
  incomplete = {nullptr, get, nullptr}; expectAdmission(false, &incomplete);
  fixtures(); doc = parseDoc(app("default")); doc["requires"][0]["capability"] = RISC_BOUND_KEY_VALUE_CAPABILITY; file("default.json", encode(doc));
  doc = parseDoc(boot()); doc["app_capabilities"][0]["grants"][0]["capability"] = RISC_BOUND_KEY_VALUE_CAPABILITY; file("boot.json", encode(doc)); expectAdmission(false);
  fixtures(); doc = parseDoc(boot()); doc["app_capabilities"][0]["grants"][0]["capability"] = RISC_BOUND_KEY_VALUE_CAPABILITY; file("boot.json", encode(doc)); expectAdmission(false);
  // Both bounds are accepted, with no preparation I/O. API has no aliases:
  // the same exact 15-byte/punctuation key goes to its configured namespace.
  fixtures(); doc = parseDoc(boot()); doc["drivers"][0]["key_value"][0]["key"] = "abcdefghijklmno";
  doc["drivers"][0]["key_value"][0]["namespace"] = INT32_MAX; doc["drivers"][0]["key_value"][1]["key"] = "a-._09";
  file("boot.json", encode(doc)); expectAdmission(true);
  // Maximum bounded policy storage: 16 selected modules, nine entries each.
  fixtures(); doc = parseDoc(boot()); doc.remove("app_capabilities"); doc["drivers"].to<JsonArray>();
  for (unsigned i = 0; i < 16; ++i) {
    auto manifest = parseDoc(driver(0)); manifest["id"] = "bound-limit-" + std::to_string(i);
    manifest["provides"][0]["capability"] = "test.bound.limit-" + std::to_string(i);
    const std::string filename = "limit-" + std::to_string(i) + ".json"; file(filename.c_str(), encode(manifest));
    auto selection = doc["drivers"].as<JsonArray>().add<JsonObject>(); selection["manifest"] = filename;
    selection["key_value"].set(parseDoc(primaryMap).as<JsonVariantConst>());
    selection["key_value"].as<JsonArray>().add(parseDoc(R"({"key":"ninth","namespace":1,"access":"read"})").as<JsonVariantConst>());
  }
  file("boot.json", encode(doc)); expectAdmission(true);
  doc["drivers"][15]["key_value"].as<JsonArray>().add(parseDoc(R"({"key":"tenth","namespace":1,"access":"read"})").as<JsonVariantConst>());
  file("boot.json", encode(doc)); expectAdmission(false, &backend, true);
  // Legacy selections and omitted key_value remain accepted without backend.
  fixtures(); file("first.json", driver(0, "[]")); file("boot.json", R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"first.json"}]})"); expectAdmission(true, nullptr);
  // A reserved storage requirement cannot also be a physical binding.
  fixtures(); file("board.json", R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[{"instance_id":7,"chip":{"vendor":"test","model":"gpio","revision":"unspecified"},"compatible":"test,gpio","config_type":"gpio.bank","config_version":1,"config":{"pins":[7],"active_high":true,"pull_up":true,"debounce_us":0,"long_press_us":0,"click_min_us":0},"bindings":{"storage.key-value.bound":7}}]})");
  doc = parseDoc(driver(0)); doc["requires"].as<JsonArray>().add(parseDoc(R"({"capability":"hardware.device","api":1})").as<JsonVariantConst>());
  doc["hardware_compatibility"].set(parseDoc(R"([{"compatible":"test,gpio","revisions":["unspecified"],"config_type":"gpio.bank","config_version":1}])").as<JsonVariantConst>()); file("first.json", encode(doc));
  doc = parseDoc(boot()); doc["drivers"][0]["instance_id"] = 7; file("boot.json", encode(doc)); expectAdmission(false);
  fixtures(); puts("Bound KV schema, reserved capability and exact selection admission before ELF/backend PASS");
}
static void resetRun() {
  events.clear(); appRuns = starts[0] = starts[1] = unloads[0] = unloads[1] = 0;
  reads = writes = 0; values.clear(); values[{1, "alert_mode"}] = "quiet"; values[{2, "alert_mode"}] = "loud"; values[{1, "another_mode"}] = "secret"; values[{INT32_MAX, "abcdefghijklmno"}] = "edge";
  stale[0] = saved[0]; stale[1] = saved[1]; saved[0] = {}; saved[1] = {};
}
static void normal() {
  for (unsigned iteration = 0; iteration < 2; ++iteration) {
    resetRun(); RiscBoot::Runtime runtime({owner, health, delay, logLine, nullptr, &backend, safe}); running = &runtime;
    assert(runtime.prepare(root.c_str()) && calls() == 0 && events.empty());
    assert(runtime.run()); assert(appRuns == 4 && starts[0] == 1 && starts[1] == 1 && unloads[0] == 1 && unloads[1] == 1);
    assert(values.at({3, "alarm_occ"}) == "forged" && values.at({4, "alarm_occ"}) == "owned-0" && values.at({5, "alarm_occ"}) == "owned-1");
    deny(saved[0]); deny(saved[1]); deny(stale[0]); deny(stale[1]);
    assert(!risc_runtime_get_api(1)); running = nullptr;
  }
  puts("Bound KV start without app, distinct maps, exact backend counts, owner/bounds/errors, handoffs and stale Runtime contexts PASS");
}
static void failedStart() {
  resetRun(); failStart = true;
  RiscBoot::Runtime runtime({owner, health, delay, logLine, nullptr, &backend, safe}); running = &runtime;
  assert(runtime.prepare(root.c_str()) && !runtime.run());
  assert(appRuns == 0 && starts[0] == 1 && starts[1] == 0);
  assert(strstr(runtime.error(), "bound fixture rejected after storage"));
  assert((events == std::vector<std::string>{"0:loaded", "0:descriptor", "0:start", "0:check", "0:diagnostic", "0:quiesce", "0:stop", "0:unloaded"}));
  deny(saved[0]); failStart = false; running = nullptr;
  puts("Runtime failed-start storage revocation before real provider diagnostics/quiesce/stop PASS");
}
static void retained() {
  resetRun(); retaining = true;
  auto* runtime = new RiscBoot::Runtime({owner, health, delay, logLine, nullptr, &backend, safe}); running = runtime;
  assert(runtime->prepare(root.c_str()) && !runtime->run());
  assert(strstr(runtime->error(), "native retention barrier") && appRuns == 1);
  assert(unloads[0] == 0 && unloads[1] == 0);
  for (const void* image : providerImages) { Dl_info info{}; assert(image && dladdr(image, &info)); }
  for (const auto& event : events) assert(event.find("quiesce") == std::string::npos && event.find("stop") == std::string::npos);
  assert(!runtime->run()); deny(saved[0]); deny(saved[1]);
  retaining = false; exitSafe = true;
  // Reload the same artifact paths while their old instances stay retained.
  // Fresh mappings must not overwrite retained provider state or revive old
  // storage tokens. The retained invocation/graph receive no cleanup callbacks.
  const void* retainedImages[] = {providerImages[0], providerImages[1]};
  const risc_bound_key_value_v1 retainedStorage[] = {saved[0], saved[1]};
  normal();
  for (unsigned index = 0; index < 2; ++index) {
    Dl_info info{};
    assert(retainedImages[index] != providerImages[index]);
    assert(dladdr(retainedImages[index], &info));
    assert(retainedStorage[index].context != saved[index].context);
    deny(retainedStorage[index]);
  }
  _exit(0);
}
int main(int argc, char** argv) {
  assert(argc == 2); root = argv[1];
  uintptr_t generation = UINTPTR_MAX - 1;
  assert(RiscBoot::nextKeyValueContext(generation) && generation == UINTPTR_MAX);
  assert(!RiscBoot::nextKeyValueContext(generation) && generation == UINTPTR_MAX);
  schema(); normal(); failedStart();
  const pid_t child = fork(); assert(child >= 0);
  if (!child) retained();
  int status = 0; assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  puts("Native retention denies provider storage, retains images across same-path reloads, and never revives copied contexts PASS");
}
