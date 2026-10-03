/* Host-only provider using the real CPU GPIO capability and typed board data. */
#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <GardenPlatformV1.h>
#include <assert.h>
#include <string.h>

extern void test_retained_trace(const char*);
extern const char* test_retained_mode(void);
static const garden_gpio_v1* gpio;
static uint64_t input, output;

__attribute__((constructor)) static void loaded(void) {
  test_retained_trace("PROVIDER loaded");
}
__attribute__((destructor)) static void unloaded(void) {
  test_retained_trace("PROVIDER unloaded");
}
static int32_t enter(const char* mode) {
  const int32_t held = gpio->deep_sleep_hold(gpio->context, output, true);
  if (held != 0) return held;
  const int32_t result = gpio->deep_sleep(gpio->context, input, false);
  /* A terminal failure must not trigger further hardware mutation. */
  if (result == RISC_DEEP_SLEEP_RETAINED || !strcmp(mode, "held-output"))
    return result;
  const int32_t released = gpio->deep_sleep_hold(gpio->context, output, false);
  return released == 0 ? result : released;
}
static bool start(const risc_provider_dependency_v1* deps, size_t count) {
  const risc_hardware_device_v1* hardware = NULL;
  for (size_t i = 0; i < count; ++i) {
    if (!strcmp(deps[i].capability_id, "platform.gpio")) gpio = deps[i].api;
    if (!strcmp(deps[i].capability_id, "hardware.device")) hardware = deps[i].api;
  }
  if (!hardware || !gpio || gpio->struct_size < GARDEN_GPIO_DEEP_SLEEP_HOLD_V1_SIZE ||
      !gpio->deep_sleep || !gpio->deep_sleep_hold) return false;
  const risc_hw_gpio_bank_v1* config = hardware->config;
  if (config->count != 2 ||
      !gpio->claim(gpio->context, config->pins[0], false, false, true, &input) ||
      !gpio->claim(gpio->context, config->pins[1], true, false, false, &output))
    return false;
  test_retained_trace("PROVIDER started");
  if (!strcmp(test_retained_mode(), "initial-held"))
    assert(gpio->deep_sleep_hold(gpio->context, output, true) == 0);
  return true;
}
static bool quiesce(void) {
  test_retained_trace("PROVIDER quiesce-attempt");
  if (output && !gpio->release(gpio->context, output)) return false;
  output = 0;
  if (input && !gpio->release(gpio->context, input)) return false;
  input = 0;
  test_retained_trace("PROVIDER quiesced");
  return true;
}
static void stop(void) {
  test_retained_trace("PROVIDER stopped");
  gpio = NULL;
}
static const struct {
  uint32_t api_version, struct_size;
  int32_t (*enter)(const char*);
} api = {1, sizeof(api), enter};
static const risc_driver_v2 driver = {
  2, sizeof(driver), "retained-probe", "test.retained", 1,
  &api, start, stop, quiesce
};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi) {
  return abi == 2 ? &driver : NULL;
}
