// Exercises the production adapter and startup wrapper with IDF-shaped faults.
// This is host contract evidence; it does not qualify flash or real hardware.
#include "ports/esp32s3/NvsKeyValue.h"
#include <esp_partition.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

extern "C" esp_err_t __wrap_nvs_flash_init();

namespace {
const char* scenario = "initialization";
#define CHECK(condition) do { if (!(condition)) { \
  std::fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, scenario, #condition); \
  std::abort(); } } while (false)
using Bytes = std::vector<uint8_t>;
struct Value { Bytes bytes; bool blob = true; };
using Namespace = std::map<std::string, Value>;
struct OpenHandle {
  std::string name;
  nvs_open_mode_t mode;
  // Protocol fault model, not the pinned flash implementation: IDF 4.4.7
  // set_blob writes storage and NVSHandleSimple::commit only validates its
  // handle. Pending state lets us test every API failure boundary, including
  // uncertain persistence, without claiming real flash atomicity or durability.
  // https://github.com/espressif/esp-idf/blob/v4.4.7/components/nvs_flash/src/nvs_handle_simple.cpp
  Namespace pending;
};
struct Event {
  std::string operation, name, key;
  nvs_handle_t handle;
  size_t size;
};
struct Fault {
  esp_err_t error = ESP_OK;
  int64_t length = -1;
  bool scribble = false;
};
std::map<std::string, Namespace> store;
std::map<nvs_handle_t, OpenHandle> handles;
std::map<std::string, Fault> faults;
std::vector<Event> events;
nvs_handle_t nextHandle = 1;
esp_err_t realInitResult = ESP_OK;
unsigned initAttempts = 0, eraseAttempts = 0;
unsigned partitionLookups = 0, partitionErases = 0;
const esp_partition_t fakePartition{nullptr, ESP_PARTITION_TYPE_DATA,
  ESP_PARTITION_SUBTYPE_DATA_NVS, 0x9000, 0x5000, "nvs", false};
bool persistOnSetError = false, persistOnCommitError = false;
const Bytes oldValue{0x12, 0x00, 0xa4, 0x7f};
const Bytes newValue{0x89, 0xff, 0x00, 0x31};

void closed() {
  CHECK(handles.empty());
  CHECK(eraseAttempts == 0);
  CHECK(partitionLookups == 0 && partitionErases == 0);
}
void reset(const char* name) {
  closed();
  scenario = name;
  store.clear(); events.clear(); faults.clear();
  persistOnSetError = false; persistOnCommitError = false;
}
void observe() { closed(); events.clear(); faults.clear(); }
void seed(const Bytes& bytes = oldValue, bool blob = true) {
  store["rte00000001"]["clock"] = {bytes, blob};
}
void operations(std::initializer_list<const char*> expected) {
  CHECK(events.size() == expected.size());
  size_t i = 0;
  for (const auto* operation : expected) CHECK(events[i++].operation == operation);
  closed();
}
size_t count(const char* operation) {
  return std::count_if(events.begin(), events.end(), [&](const Event& event) {
    return event.operation == operation;
  });
}
void getResult(int32_t expected, uint32_t id = 1, const char* key = "clock",
               uint32_t capacity = RISC_KEY_VALUE_BLOB_MAX,
               uint32_t required = 0, const Bytes& bytes = {}) {
  std::array<uint8_t, RISC_KEY_VALUE_BLOB_MAX + 8> output;
  output.fill(0xa5);
  auto before = output;
  uint32_t actual = UINT32_MAX;
  const auto* backend = RiscNvs::backend();
  CHECK(backend && backend->get && backend->put && !backend->context);
  CHECK(backend->get(backend->context, id, key, output.data(), capacity, &actual) == expected);
  CHECK(actual == required);
  if (expected == RISC_KEY_VALUE_OK) {
    CHECK(bytes.size() == actual);
    std::copy(bytes.begin(), bytes.end(), before.begin());
  }
  CHECK(output == before); // Includes all errors and the bytes past a good read.
  closed();
}
void probe(int32_t expected, uint32_t required = 0, uint32_t capacity = 0) {
  uint32_t actual = UINT32_MAX;
  CHECK(RiscNvs::get(nullptr, 1, "clock", nullptr, capacity, &actual) == expected);
  CHECK(actual == required);
  closed();
}
void putResult(int32_t expected, const Bytes& bytes = newValue,
               uint32_t id = 1, const char* key = "clock") {
  const auto* backend = RiscNvs::backend();
  CHECK(backend->put(backend->context, id, key, bytes.data(), bytes.size()) == expected);
  closed();
}

void testValidation() {
  reset("invalid namespace, key and arguments do not touch NVS");
  for (const uint32_t id : {0u, uint32_t(INT32_MAX) + 1u, UINT32_MAX}) {
    getResult(RISC_KEY_VALUE_INVALID, id);
    putResult(RISC_KEY_VALUE_INVALID, newValue, id);
  }
  for (const char* key : {static_cast<const char*>(nullptr), "", "A", "clock key", "a/b", "a\\b", "a:b",
                           "a\n", "\x80", "\xff", "abcdefghijklmnop"}) {
    getResult(RISC_KEY_VALUE_INVALID, 1, key);
    putResult(RISC_KEY_VALUE_INVALID, newValue, 1, key);
  }
  // A fully populated overlong array must be rejected without searching beyond
  // KEY_MAX + 1 bytes for a terminator (ASan checks this exact-sized allocation).
  std::vector<char> unterminated(RISC_KEY_VALUE_KEY_MAX + 1, 'a');
  getResult(RISC_KEY_VALUE_INVALID, 1, unterminated.data());
  putResult(RISC_KEY_VALUE_INVALID, newValue, 1, unterminated.data());
  uint8_t byte = 7;
  CHECK(RiscNvs::get(nullptr, 1, "clock", &byte, 1, nullptr) == RISC_KEY_VALUE_INVALID);
  CHECK(byte == 7);
  probe(RISC_KEY_VALUE_INVALID, 0, 1);
  CHECK(RiscNvs::put(nullptr, 1, "clock", nullptr, 1) == RISC_KEY_VALUE_INVALID);
  CHECK(RiscNvs::put(nullptr, 1, "clock", &byte, 0) == RISC_KEY_VALUE_INVALID);
  CHECK(RiscNvs::put(nullptr, 1, "clock", &byte, RISC_KEY_VALUE_BLOB_MAX + 1) == RISC_KEY_VALUE_INVALID);
  CHECK(RiscNvs::put(nullptr, 1, "clock", &byte, UINT32_MAX) == RISC_KEY_VALUE_INVALID);
  for (unsigned code = 1; code <= 255; ++code) {
    const bool allowed = (code >= 'a' && code <= 'z') || (code >= '0' && code <= '9') ||
                         code == '_' || code == '.' || code == '-';
    if (allowed) continue;
    const char key[]{static_cast<char>(code), '\0'};
    getResult(RISC_KEY_VALUE_INVALID, 1, key);
    putResult(RISC_KEY_VALUE_INVALID, newValue, 1, key);
  }
  operations({});

  reset("all permitted ASCII key characters and exact key length");
  for (const char character : std::string("abcdefghijklmnopqrstuvwxyz0123456789_.-")) {
    char key[]{character, '\0'};
    putResult(RISC_KEY_VALUE_OK, newValue, 1, key);
    getResult(RISC_KEY_VALUE_OK, 1, key, newValue.size(), newValue.size(), newValue);
  }
  putResult(RISC_KEY_VALUE_OK, newValue, 1, "abcdefghijklmno");
  getResult(RISC_KEY_VALUE_OK, 1, "abcdefghijklmno", newValue.size(), newValue.size(), newValue);
}

void testNamespaceAndBounds() {
  reset("exact namespace names and namespace isolation");
  const std::pair<uint32_t, const char*> names[] = {
    {1, "rte00000001"}, {42, "rte0000002a"}, {0x1020304, "rte01020304"},
    {uint32_t(INT32_MAX), "rte7fffffff"}
  };
  uint8_t value = 1;
  for (const auto& name : names) {
    const Bytes bytes{value++};
    observe();
    putResult(RISC_KEY_VALUE_OK, bytes, name.first);
    operations({"open_rw", "set", "commit", "open_ro", "query", "read", "close", "close"});
    for (const auto& event : events) CHECK(event.name == name.second);
    CHECK(events[0].handle != events[3].handle);
    CHECK(events[6].handle == events[3].handle && events[7].handle == events[0].handle);
    CHECK(store.at(name.second).at("clock").bytes == bytes);
  }
  value = 1;
  for (const auto& name : names) getResult(RISC_KEY_VALUE_OK, name.first, "clock", 1, 1, Bytes{value++});
  getResult(RISC_KEY_VALUE_NOT_FOUND, 43);
  CHECK(store.size() == 4); // Reading missing namespaces never creates them.

  reset("opaque minimum and maximum-size values");
  for (const uint32_t size : {1u, RISC_KEY_VALUE_BLOB_MAX}) {
    Bytes bytes(size);
    for (uint32_t i = 0; i < size; ++i) bytes[i] = uint8_t(i * 71u);
    putResult(RISC_KEY_VALUE_OK, bytes);
    getResult(RISC_KEY_VALUE_OK, 1, "clock", size, size, bytes);
  }
}

void testReads() {
  reset("absent namespace is NOT_FOUND without creation");
  getResult(RISC_KEY_VALUE_NOT_FOUND);
  operations({"open_ro"});
  CHECK(store.empty());
  observe(); probe(RISC_KEY_VALUE_NOT_FOUND); operations({"open_ro"});

  reset("absent key is NOT_FOUND and closes the handle");
  store["rte00000001"] = {};
  getResult(RISC_KEY_VALUE_NOT_FOUND);
  operations({"open_ro", "query", "close"});

  reset("wrong stored type is IO rather than missing");
  seed(oldValue, false);
  getResult(RISC_KEY_VALUE_IO);
  operations({"open_ro", "query", "close"});
  observe(); probe(RISC_KEY_VALUE_IO);
  operations({"open_ro", "query", "close"});

  for (const size_t size : {size_t(0), size_t(RISC_KEY_VALUE_BLOB_MAX + 1), size_t(4096)}) {
    reset("zero and oversized stored blobs are IO, including probes");
    seed(Bytes(size, 0xbb));
    getResult(RISC_KEY_VALUE_IO);
    operations({"open_ro", "query", "close"});
    observe(); probe(RISC_KEY_VALUE_IO);
    operations({"open_ro", "query", "close"});
  }

  reset("size probes and short capacities validate complete data first");
  seed();
  probe(RISC_KEY_VALUE_BUFFER_SMALL, oldValue.size());
  operations({"open_ro", "query", "read", "close"});
  for (const uint32_t capacity : {0u, 1u, uint32_t(oldValue.size() - 1)}) {
    observe();
    getResult(RISC_KEY_VALUE_BUFFER_SMALL, 1, "clock", capacity, oldValue.size());
    operations({"open_ro", "query", "read", "close"});
  }
  for (const uint32_t capacity : {uint32_t(oldValue.size()), RISC_KEY_VALUE_BLOB_MAX, UINT32_MAX}) {
    observe();
    getResult(RISC_KEY_VALUE_OK, 1, "clock", capacity, oldValue.size(), oldValue);
    operations({"open_ro", "query", "read", "close"});
    CHECK(events[2].size == RISC_KEY_VALUE_BLOB_MAX);
  }
}

void testReadFaults() {
  for (const esp_err_t error : {ESP_FAIL, ESP_ERR_NO_MEM, ESP_ERR_NVS_NOT_INITIALIZED,
                               ESP_ERR_NVS_PART_NOT_FOUND, ESP_ERR_NVS_NOT_ENOUGH_SPACE}) {
    reset("read-open backend errors are IO and never close an unopened handle");
    seed(); faults["open_ro"].error = error;
    getResult(RISC_KEY_VALUE_IO);
    operations({"open_ro"});
  }
  for (const esp_err_t error : {ESP_FAIL, ESP_ERR_NVS_TYPE_MISMATCH,
                               ESP_ERR_NVS_INVALID_STATE, ESP_ERR_NVS_INVALID_HANDLE,
                               ESP_ERR_NVS_INVALID_LENGTH}) {
    reset("metadata errors are IO, with no data read");
    seed(); faults["query"].error = error;
    getResult(RISC_KEY_VALUE_IO);
    operations({"open_ro", "query", "close"});
  }
  for (const esp_err_t error : {ESP_FAIL, ESP_ERR_NVS_NOT_FOUND, ESP_ERR_NVS_TYPE_MISMATCH,
                               ESP_ERR_NVS_INVALID_STATE, ESP_ERR_NVS_INVALID_LENGTH}) {
    for (const uint32_t capacity : {0u, 1u, RISC_KEY_VALUE_BLOB_MAX}) {
      reset("failed second reads cannot return partial data or BUFFER_SMALL");
      seed(); faults["read"] = {error, 2, true};
      getResult(RISC_KEY_VALUE_IO, 1, "clock", capacity);
      operations({"open_ro", "query", "read", "close"});
      observe(); faults["read"] = {error, 2, true};
      probe(RISC_KEY_VALUE_IO);
      operations({"open_ro", "query", "read", "close"});
    }
  }
  for (const int64_t actual : {int64_t(0), int64_t(3), int64_t(5), int64_t(65)}) {
    reset("successful second read with inconsistent length is IO");
    seed(); faults["read"].length = actual;
    getResult(RISC_KEY_VALUE_IO);
    operations({"open_ro", "query", "read", "close"});
    observe(); faults["read"].length = actual;
    probe(RISC_KEY_VALUE_IO);
  }
}

void testWriteFaults() {
  for (const esp_err_t error : {ESP_FAIL, ESP_ERR_NO_MEM, ESP_ERR_NVS_NOT_FOUND,
                               ESP_ERR_NVS_NOT_INITIALIZED, ESP_ERR_NVS_PART_NOT_FOUND,
                               ESP_ERR_NVS_NOT_ENOUGH_SPACE}) {
    reset("write-open errors, including full NVS, are IO");
    seed(); faults["open_rw"].error = error;
    putResult(RISC_KEY_VALUE_IO);
    operations({"open_rw"});
    CHECK(store.at("rte00000001").at("clock").bytes == oldValue);
  }
  for (const esp_err_t error : {ESP_FAIL, ESP_ERR_NVS_NOT_ENOUGH_SPACE,
                               ESP_ERR_NVS_READ_ONLY, ESP_ERR_NVS_REMOVE_FAILED}) {
    reset("set errors stop before commit and readback");
    seed(); faults["set"].error = error;
    putResult(RISC_KEY_VALUE_IO);
    operations({"open_rw", "set", "close"});
    CHECK(store.at("rte00000001").at("clock").bytes == oldValue);
  }
  reset("a failed set may already have persisted and is never rolled back");
  seed(); faults["set"].error = ESP_ERR_NVS_REMOVE_FAILED; persistOnSetError = true;
  putResult(RISC_KEY_VALUE_IO);
  operations({"open_rw", "set", "close"});
  CHECK(store.at("rte00000001").at("clock").bytes == newValue);

  for (const bool persist : {false, true}) {
    reset("commit error does not promise old data or trigger rollback/retry");
    seed(); faults["commit"].error = ESP_FAIL; persistOnCommitError = persist;
    putResult(RISC_KEY_VALUE_IO);
    operations({"open_rw", "set", "commit", "close"});
    CHECK(store.at("rte00000001").at("clock").bytes == (persist ? newValue : oldValue));
    observe();
    const auto& expected = persist ? newValue : oldValue;
    getResult(RISC_KEY_VALUE_OK, 1, "clock", expected.size(), expected.size(), expected);
  }

  for (const char* stage : {"open_ro", "query", "read"}) {
    for (const esp_err_t error : {ESP_FAIL, ESP_ERR_NVS_NOT_FOUND, ESP_ERR_NVS_TYPE_MISMATCH}) {
      reset("failed independent readback after commit reports IO and closes every handle");
      seed(); faults[stage] = {error, -1, true};
      putResult(RISC_KEY_VALUE_IO);
      CHECK(count("open_rw") == 1 && count("set") == 1 && count("commit") == 1);
      CHECK(count("open_ro") == 1);
      CHECK(count("close") == (std::string(stage) == "open_ro" ? 1u : 2u));
      CHECK(store.at("rte00000001").at("clock").bytes == newValue);
    }
  }
  reset("successful readback with different bytes reports IO");
  seed(); faults["read"].scribble = true;
  putResult(RISC_KEY_VALUE_IO);
  operations({"open_rw", "set", "commit", "open_ro", "query", "read", "close", "close"});
  CHECK(store.at("rte00000001").at("clock").bytes == newValue);

  for (const int64_t size : {int64_t(0), int64_t(65)}) {
    reset("invalid readback sizes never report a committed write as verified");
    seed(); faults["query"].length = size;
    putResult(RISC_KEY_VALUE_IO);
    operations({"open_rw", "set", "commit", "open_ro", "query", "close", "close"});
    CHECK(store.at("rte00000001").at("clock").bytes == newValue);
  }

  reset("internally consistent readback of a different size still reports IO");
  seed(); faults["query"].length = 3; faults["read"].length = 3;
  putResult(RISC_KEY_VALUE_IO);
  operations({"open_rw", "set", "commit", "open_ro", "query", "read", "close", "close"});
  CHECK(store.at("rte00000001").at("clock").bytes == newValue);

  reset("retry is caller-controlled after an uncertain commit");
  seed(); faults["commit"].error = ESP_FAIL; persistOnCommitError = true;
  putResult(RISC_KEY_VALUE_IO);
  observe();
  putResult(RISC_KEY_VALUE_OK);
  operations({"open_rw", "set", "commit", "open_ro", "query", "read", "close", "close"});
}

esp_err_t selectedResult(const std::string& name) {
  if (name == "ok") return ESP_OK;
  if (name == "no-free-pages") return ESP_ERR_NVS_NO_FREE_PAGES;
  if (name == "new-version") return ESP_ERR_NVS_NEW_VERSION_FOUND;
  if (name == "fail") return ESP_FAIL;
  if (name == "no-mem") return ESP_ERR_NO_MEM;
  if (name == "invalid-state") return ESP_ERR_INVALID_STATE;
  CHECK(name == "not-initialized");
  return ESP_ERR_NVS_NOT_INITIALIZED;
}
esp_err_t arduinoNvsInitialization() {
  // NVS portion of Arduino 2.0.17 initArduino(), without diagnostic logging.
  // Linker wrapping sends each nvs_flash_init call to the production wrapper.
  // https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/esp32-hal-misc.c#L233-L246
  esp_err_t error = __wrap_nvs_flash_init();
  if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    const esp_partition_t* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
    if (partition != nullptr) {
      error = esp_partition_erase_range(partition, 0, partition->size);
      if (!error) error = __wrap_nvs_flash_init();
    }
  }
  return error;
}
void testInitialization(esp_err_t result) {
  scenario = "startup is attempted once, preserves real status, never erases";
  CHECK(RiscNvs::initializationStatus() == ESP_ERR_INVALID_STATE);
  getResult(RISC_KEY_VALUE_IO);
  probe(RISC_KEY_VALUE_IO);
  putResult(RISC_KEY_VALUE_IO);
  operations({});
  CHECK(initAttempts == 0);
  realInitResult = result;
  const bool recoveryError = result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND;
  const esp_err_t returned = arduinoNvsInitialization();
  CHECK(returned == (recoveryError ? ESP_FAIL : result));
  CHECK(RiscNvs::initializationStatus() == result && initAttempts == 1);
  CHECK(eraseAttempts == 0 && partitionLookups == 0 && partitionErases == 0);
  realInitResult = result == ESP_OK ? ESP_FAIL : ESP_OK;
  for (unsigned i = 0; i < 4; ++i) {
    CHECK(arduinoNvsInitialization() == returned);
    CHECK(RiscNvs::initializationStatus() == result);
  }
  CHECK(initAttempts == 1);
  if (result != ESP_OK) {
    seed();
    for (unsigned i = 0; i < 3; ++i) {
      getResult(RISC_KEY_VALUE_IO);
      probe(RISC_KEY_VALUE_IO);
      putResult(RISC_KEY_VALUE_IO);
    }
    CHECK(store.at("rte00000001").at("clock").bytes == oldValue);
    operations({});
    CHECK(initAttempts == 1);
  }
}
} // namespace

extern "C" {
esp_err_t __real_nvs_flash_init() { ++initAttempts; return realInitResult; }
esp_err_t nvs_flash_erase() { ++eraseAttempts; return ESP_OK; }
const esp_partition_t* esp_partition_find_first(esp_partition_type_t type,
    esp_partition_subtype_t subtype, const char* label) {
  ++partitionLookups;
  CHECK(type == ESP_PARTITION_TYPE_DATA && subtype == ESP_PARTITION_SUBTYPE_DATA_NVS && !label);
  return &fakePartition;
}
esp_err_t esp_partition_erase_range(const esp_partition_t* partition, size_t offset, size_t size) {
  ++partitionErases;
  CHECK(partition == &fakePartition && offset == 0 && size == fakePartition.size);
  return ESP_OK;
}
esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* output) {
  CHECK(name && output);
  CHECK(mode == NVS_READONLY || mode == NVS_READWRITE);
  const char* operation = mode == NVS_READONLY ? "open_ro" : "open_rw";
  events.push_back({operation, name, {}, 0, 0});
  const auto error = faults[operation].error;
  *output = 0xbaadf00d; // An unsuccessful open must not produce a close call.
  if (error != ESP_OK) return error;
  if (mode == NVS_READONLY && !store.count(name)) return ESP_ERR_NVS_NOT_FOUND;
  if (mode == NVS_READWRITE) store.emplace(name, Namespace{});
  const auto handle = nextHandle++;
  handles.emplace(handle, OpenHandle{name, mode, {}});
  events.back().handle = handle;
  *output = handle;
  return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* output, size_t* length) {
  CHECK(handles.count(handle) && key && length);
  const auto& state = handles.at(handle);
  const char* operation = output ? "read" : "query";
  const auto fault = faults[operation];
  events.push_back({operation, state.name, key, handle, *length});
  if (fault.error != ESP_OK) {
    // Defensive test: even a lower layer that partially copies before failure
    // must not leak any bytes into the caller's output buffer.
    if (output && fault.scribble && *length) std::memset(output, 0xe3, std::min(size_t(2), *length));
    if (fault.length >= 0) *length = size_t(fault.length);
    return fault.error;
  }
  const auto found = store.at(state.name).find(key);
  if (found == store.at(state.name).end()) return ESP_ERR_NVS_NOT_FOUND;
  if (!found->second.blob) return ESP_ERR_NVS_TYPE_MISMATCH;
  const auto& bytes = found->second.bytes;
  const auto reported = fault.length >= 0 ? size_t(fault.length) : bytes.size();
  if (!output) { *length = reported; return ESP_OK; }
  if (*length < bytes.size()) { *length = bytes.size(); return ESP_ERR_NVS_INVALID_LENGTH; }
  if (!bytes.empty()) std::memcpy(output, bytes.data(), bytes.size());
  if (fault.scribble && !bytes.empty()) static_cast<uint8_t*>(output)[0] ^= 0xff;
  *length = reported;
  return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length) {
  CHECK(handles.count(handle) && key && value && length);
  auto& state = handles.at(handle);
  CHECK(state.mode == NVS_READWRITE);
  events.push_back({"set", state.name, key, handle, length});
  const auto* data = static_cast<const uint8_t*>(value);
  const Value stored{Bytes(data, data + length), true};
  const auto error = faults["set"].error;
  if (error == ESP_OK) state.pending[key] = stored;
  else if (persistOnSetError) store.at(state.name)[key] = stored;
  return error;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
  CHECK(handles.count(handle));
  auto& state = handles.at(handle);
  CHECK(state.mode == NVS_READWRITE && !state.pending.empty());
  events.push_back({"commit", state.name, {}, handle, 0});
  const auto error = faults["commit"].error;
  if (error == ESP_OK || persistOnCommitError) {
    for (const auto& value : state.pending) store.at(state.name)[value.first] = value.second;
    state.pending.clear();
  }
  return error;
}
void nvs_close(nvs_handle_t handle) {
  CHECK(handles.count(handle) == 1); // Includes double-close and failed-open bugs.
  events.push_back({"close", handles.at(handle).name, {}, handle, 0});
  handles.erase(handle);
}
} // extern "C"

int main(int argc, char** argv) {
  CHECK(argc == 2);
  const auto result = selectedResult(argv[1]);
  testInitialization(result);
  if (result == ESP_OK) {
    testValidation(); testNamespaceAndBounds(); testReads(); testReadFaults(); testWriteFaults();
    std::puts("Native NVS adapter: namespace isolation, bounds, probes, missing/type/size faults, atomic output, every-stage IO and exact readback PASS");
  }
  closed();
  CHECK(initAttempts == 1);
  std::printf("Native NVS initialization: %s, real status latched, one attempt, no erase PASS\n", argv[1]);
}
