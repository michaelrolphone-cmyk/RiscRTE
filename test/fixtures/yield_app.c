#include <RiscRuntimeV1.h>
extern void test_yield_owner(bool);
__attribute__((visibility("default"))) void app_main(void) {
  const risc_runtime_api_v1* api=risc_runtime_get_api(1);
  if(!api)return;
  api->yield_ms(0);api->yield_ms(1);api->yield_ms(20);api->yield_ms(999);
  test_yield_owner(false);api->yield_ms(1);test_yield_owner(true);
}
