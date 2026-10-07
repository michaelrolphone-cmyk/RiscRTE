/* Real graph-owned provider. No native poison is implied by a bool failure. */
#include <RiscProviderV2.h>
#include <GardenPlatformV1.h>
#include <RiscHardwareConfigV1.h>
#include <assert.h>
#include <string.h>
#ifndef PROVIDER_ID
#define PROVIDER_ID "leaf"
#endif
extern void provider_exit_event(const char*);
extern const char* provider_exit_mode(void);
extern void provider_exit_save_provider(const void*, bool);
struct probe_api { uint32_t api_version, struct_size; bool (*operation)(void); };
static const struct probe_api* dependency;
static unsigned attempts;
#ifdef LEAF
static bool operation_retained;
#endif
#ifndef LEAF
static const garden_gpio_v1* gpio;
static uint64_t output;
static bool root_operation(void) {
  assert(gpio && output);
  return gpio->write(gpio->context, output, true);
}
static const struct probe_api root_api = {1, sizeof(root_api), root_operation};
#endif
__attribute__((constructor)) static void loaded(void) { provider_exit_event(PROVIDER_ID ":loaded"); }
__attribute__((destructor)) static void unloaded(void) { provider_exit_event(PROVIDER_ID ":unloaded"); }
static bool start(const risc_provider_dependency_v1* deps, size_t count) {
#ifdef LEAF
  assert(count == 1 && !strcmp(deps[0].capability_id, "test.root"));
  dependency = deps[0].api;
  assert(dependency && dependency->api_version == 1);
#else
  if (!strcmp(provider_exit_mode(), "cpu-gpio-retained-eager")) {
    const risc_hardware_device_v1* hardware = 0;
    for (size_t i = 0; i < count; ++i) {
      if (!strcmp(deps[i].capability_id, "platform.gpio")) gpio = deps[i].api;
      if (!strcmp(deps[i].capability_id, "hardware.device")) hardware = deps[i].api;
    }
    assert(gpio && hardware && count == 2);
    const risc_hw_gpio_bank_v1* config = hardware->config;
    assert(config->count == 1 && gpio->claim(gpio->context, config->pins[0], true, false, false, &output));
  } else assert(count == 0);
#endif
  provider_exit_event(PROVIDER_ID ":start");
  provider_exit_save_provider(&attempts,
#ifdef LEAF
    true
#else
    false
#endif
  );
#ifdef LEAF
  return strncmp(provider_exit_mode(), "start-", 6) != 0;
#else
  return true;
#endif
}
static bool quiesce(void) {
  provider_exit_event(PROVIDER_ID ":quiesce");
#ifdef LEAF
  assert(dependency && dependency->api_version == 1);
  const char* mode = provider_exit_mode();
  if (!strcmp(mode, "start-retained") || !strcmp(mode, "release-retained") ||
      operation_retained) return false;
  if (!strcmp(mode, "release-retry") && ++attempts == 1) return false;
#endif
  return true;
}
static void stop(void) { provider_exit_event(PROVIDER_ID ":stop"); dependency = 0; }
#ifdef LEAF
static bool operation(void) {
  if (!strcmp(provider_exit_mode(), "cpu-gpio-retained-eager")) {
    // Model the panel's provider-local terminal state after a raw GPIO failure.
    // CpuPort itself does not interpret an ordinary write false as poison.
    assert(dependency && !dependency->operation());
    operation_retained = true;
  } else if (!strcmp(provider_exit_mode(), "operation-retained-eager") ||
             !strcmp(provider_exit_mode(), "operation-retained-demand")) operation_retained = true;
  provider_exit_event("leaf:operation-false"); return false;
}
static const struct { uint32_t api_version, struct_size; bool (*operation)(void); } api = {1, sizeof(api), operation};
#define TABLE &api
#else
#define TABLE &root_api
#endif
static const risc_driver_v2 driver = {2, sizeof(driver), PROVIDER_ID, "test." PROVIDER_ID,
  1, TABLE, start, stop, quiesce};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi) {
  return abi == 2 ? &driver : 0;
}
