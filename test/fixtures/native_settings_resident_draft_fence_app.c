/* Host-only dlopen fixtures for the native settings resident draft fence. */
#include <RiscResidentShellV1.h>
#include <RiscProviderV2.h>

#ifndef SETTINGS_FENCE_ROLE
#define SETTINGS_FENCE_ROLE 0
#endif

#if SETTINGS_FENCE_ROLE == 2
extern void test_settings_fence_provider(unsigned event, uint32_t budget);
static const uint32_t capability[2] = {1, sizeof(capability)};
static bool start(const risc_provider_dependency_v1* dependencies, size_t count) {
  (void)dependencies;
  if (count) return false;
  test_settings_fence_provider(0, 0);
  return true;
}
static void service(uint32_t budget) { test_settings_fence_provider(1, budget); }
static bool quiesce(void) { test_settings_fence_provider(2, 0); return true; }
static void stop(void) { test_settings_fence_provider(3, 0); }
static const risc_driver_service_v2 driver = {
  {{{2, sizeof(driver), "settings-fence-service", "test.settings-fence-service", 1,
     capability, start, stop, quiesce}, 0, 0}, 0},
  RISC_DRIVER_SERVICE_TAG_V1, RISC_DRIVER_SERVICE_VERSION_V1, service
};
__attribute__((visibility("default")))
const risc_driver_v2* t5_driver_get(uint32_t abi) {
  return abi == 2 ? &driver.poll.streams.driver : 0;
}
#else
extern void test_settings_fence_app(unsigned event, unsigned role);
__attribute__((visibility("default")))
const risc_resident_app_descriptor_v1_t risc_resident_app_descriptor_v1 = {
  1, sizeof(risc_resident_app_descriptor_v1_t),
  SETTINGS_FENCE_ROLE ? RISC_RESIDENT_ROLE_FOREGROUND : RISC_RESIDENT_ROLE_HOST, 0
};
__attribute__((visibility("default"))) int app_module_init(void) {
  test_settings_fence_app(0, SETTINGS_FENCE_ROLE);
  return 0;
}
__attribute__((visibility("default"))) void app_main(void) {
  test_settings_fence_app(1, SETTINGS_FENCE_ROLE);
}
__attribute__((visibility("default"))) void app_module_fini(void) {
  test_settings_fence_app(2, SETTINGS_FENCE_ROLE);
}
__attribute__((destructor)) static void unmapped(void) {
  test_settings_fence_app(3, SETTINGS_FENCE_ROLE);
}
#endif
