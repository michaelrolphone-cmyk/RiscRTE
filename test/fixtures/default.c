#include <RiscRuntimeV1.h>
__attribute__((visibility("default"))) void app_main(void) {
  const risc_runtime_api_v1* api=risc_runtime_get_api(1);
  if(!api) return;
  risc_runtime_health_v1 h={0}; h.struct_size=sizeof(h);
  if(!api->health(&h)) return;
  api->diagnostic("TEST default");
  if(h.uptime_ms<3) {
    if(api->request_launch(h.uptime_ms==2?"missing.elf":"child.elf")) return;
  }
}
