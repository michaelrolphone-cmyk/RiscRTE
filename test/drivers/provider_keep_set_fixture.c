/* Host fixture only: lifecycle observations never become provider authority. */
#include <RiscProviderV2.h>
#ifndef FIXTURE_SLOT
#error FIXTURE_SLOT must be defined
#endif
#define STRINGIFY_INNER(value) #value
#define STRINGIFY(value) STRINGIFY_INNER(value)
#ifndef FIXTURE_CAPABILITY
#define FIXTURE_CAPABILITY "cap.keep-" STRINGIFY(FIXTURE_SLOT)
#endif
extern void keep_set_event(unsigned slot, unsigned event);
extern bool keep_set_control(unsigned slot, bool starting);
static const uint32_t api[2] = {1, sizeof(api)};
static const risc_provider_dependency_v1* retained;
static size_t retained_count;
static void check_dependencies(void) {
  for (size_t i = 0; i < retained_count; ++i)
    if (!retained[i].api || !retained[i].api_version) __builtin_trap();
}
static bool start(const risc_provider_dependency_v1* deps, size_t count) {
  retained = deps;
  retained_count = count;
  check_dependencies();
  keep_set_event(FIXTURE_SLOT, 0);
  return keep_set_control(FIXTURE_SLOT, true);
}
static bool quiesce(void) {
  check_dependencies();
  keep_set_event(FIXTURE_SLOT, 1);
  return keep_set_control(FIXTURE_SLOT, false);
}
static void stop(void) {
  check_dependencies();
  keep_set_event(FIXTURE_SLOT, 2);
  retained = 0;
  retained_count = 0;
}
static void poll(uint32_t budget) {
  if (!budget) __builtin_trap();
  check_dependencies();
  keep_set_event(FIXTURE_SLOT, 3);
}
#if FIXTURE_SLOT == 5
static bool bind(const risc_stream_provider_v1* host) {
  return host && host->context;
}
#else
#define bind 0
#endif
static const risc_driver_poll_v2 driver = {
  {{2, sizeof(driver), "keep-" STRINGIFY(FIXTURE_SLOT), FIXTURE_CAPABILITY,
    1, api, start, stop, quiesce}, 0, bind}, poll
};
__attribute__((visibility("default")))
const risc_driver_v2* t5_driver_get(uint32_t abi) {
  return abi == 2 ? &driver.streams.driver : 0;
}
