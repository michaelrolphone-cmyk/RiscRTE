/* Adapted from T5S3-Reader PR370 test/hardware/heartbeat/src/main.cpp.
 * Same health fields and 2-second sequence cadence; OS facts/one-way diagnostics
 * now arrive through the headless RiscRTE service instead of Arduino imports. */
#include <RiscRuntimeV1.h>
#include <stdio.h>
__attribute__((visibility("default"))) void app_main(void) {
  const risc_runtime_api_v1* api=risc_runtime_get_api(1);
  if(!api || api->struct_size<sizeof(*api)) return;
  uint32_t sequence=0, previous=0;
  for(;;) {
    risc_runtime_health_v1 health={0}; health.struct_size=sizeof(health);
    if(!api->health(&health)) return;
    if((uint32_t)(health.uptime_ms-previous)>=2000) {
      previous=health.uptime_ms;
      char line[256];
      int n=snprintf(line,sizeof(line),"RTE_HEARTBEAT version=1.0.0 target=%s mac=%02x:%02x:%02x:%02x:%02x:%02x sequence=%lu uptime_ms=%lu heap=%u app=0x%lx",
        health.target,health.mac[0],health.mac[1],health.mac[2],health.mac[3],health.mac[4],health.mac[5],
        (unsigned long)++sequence,(unsigned long)health.uptime_ms,(unsigned)health.free_heap,(unsigned long)health.app_address);
      if(n<0 || (size_t)n>=sizeof(line) || !api->diagnostic(line)) return;
    }
    api->yield_ms(20);
  }
}
