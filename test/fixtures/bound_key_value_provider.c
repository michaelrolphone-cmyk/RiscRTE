/* Test-only dynamic provider. Exercises the real injected dependency table;
 * no global storage symbol or foreground-app acquisition is used. */
#include <RiscProviderV2.h>
#include <RiscBoundKeyValueV1.h>
#include "bound_key_value_test_api.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>
#ifndef PROVIDER_INDEX
#define PROVIDER_INDEX 0
#endif
_Static_assert(offsetof(risc_bound_key_value_v1, api_version) == 0, "version prefix");
_Static_assert(offsetof(risc_bound_key_value_v1, struct_size) == 4, "size prefix");
_Static_assert(offsetof(risc_bound_key_value_v1, context) == 8, "context prefix");
extern void test_bound_event(unsigned, const char*);
extern void test_bound_started(unsigned, risc_bound_key_value_v1);
extern void test_bound_image(unsigned, const void*);
extern void test_bound_denied(unsigned, risc_bound_key_value_v1);
extern int test_bound_should_fail(unsigned);
static risc_bound_key_value_v1 storage;
static void check(void) {
  char data[64]; uint32_t size = 0;
  const char* expected = PROVIDER_INDEX ? "loud" : "quiet";
  assert(storage.get(storage.context, "alert_mode", data, sizeof(data), &size) == RISC_BOUND_KEY_VALUE_OK);
  assert(size == strlen(expected) && !memcmp(data, expected, size));
  expected = PROVIDER_INDEX ? "owned-1" : "owned-0";
  assert(storage.get(storage.context, "alarm_occ", data, sizeof(data), &size) == RISC_BOUND_KEY_VALUE_OK);
  assert(size == strlen(expected) && !memcmp(data, expected, size));
  test_bound_event(PROVIDER_INDEX, "check");
}
static bool start(const risc_provider_dependency_v1* deps, size_t count) {
  test_bound_event(PROVIDER_INDEX, "start");
  assert(count == 1 && deps && !strcmp(deps[0].capability_id, RISC_BOUND_KEY_VALUE_CAPABILITY));
  assert(deps[0].api_version == 1 && deps[0].api);
  const risc_bound_key_value_v1* kv = deps[0].api;
  assert(kv->api_version == RISC_BOUND_KEY_VALUE_API_V1 && kv->struct_size == sizeof(*kv));
  assert(kv->context && kv->get && kv->put);
  storage = *kv;
  test_bound_image(PROVIDER_INDEX, (const void*)check);
  test_bound_started(PROVIDER_INDEX, storage);
  const char* value = PROVIDER_INDEX ? "owned-1" : "owned-0";
  assert(storage.put(storage.context, "alarm_occ", value, (uint32_t)strlen(value)) == RISC_BOUND_KEY_VALUE_OK);
  check();
  return !test_bound_should_fail(PROVIDER_INDEX);
}
static bool quiesce(void) {
  test_bound_event(PROVIDER_INDEX, "quiesce");
  test_bound_denied(PROVIDER_INDEX, storage);
  return true;
}
static void stop(void) {
  test_bound_event(PROVIDER_INDEX, "stop");
  test_bound_denied(PROVIDER_INDEX, storage);
}
static bool diagnostic(char* out, size_t capacity) {
  test_bound_event(PROVIDER_INDEX, "diagnostic");
  test_bound_denied(PROVIDER_INDEX, storage);
  const char text[] = "bound fixture rejected after storage";
  assert(capacity >= sizeof(text)); memcpy(out, text, sizeof(text)); return true;
}
static const test_bound_provider_api api = {1, sizeof(api), check};
static const risc_driver_diagnostics_v2 descriptor = {{
  2, sizeof(descriptor), PROVIDER_INDEX ? "bound-second" : "bound-first",
  PROVIDER_INDEX ? "test.bound.second" : "test.bound.first", 1, &api,
  start, stop, quiesce
}, diagnostic};
__attribute__((constructor)) static void loaded(void) { test_bound_event(PROVIDER_INDEX, "loaded"); }
__attribute__((destructor)) static void unloaded(void) { test_bound_event(PROVIDER_INDEX, "unloaded"); }
__attribute__((visibility("default")))
const risc_driver_v2* t5_driver_get(uint32_t version) {
  test_bound_event(PROVIDER_INDEX, "descriptor");
  return version == 2 ? &descriptor.base : 0;
}
