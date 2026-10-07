/* Frozen 0.1.50 ABI prefix, deliberately compiled without the new SDK header. */
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
typedef struct { uint32_t struct_size, uptime_ms, free_heap, app_address; uint8_t mac[6]; char target[96]; } health_v1;
typedef struct { uint32_t struct_size, slot, generation; const void* api; } grant_v1;
typedef struct {
  uint32_t api_version, struct_size;
  bool (*health)(health_v1*);
  void (*yield_ms)(uint32_t);
  bool (*diagnostic)(const char*);
  bool (*request_launch)(const char*);
  bool (*acquire)(const char*, uint32_t, uint64_t, grant_v1*);
  bool (*release)(grant_v1*);
  bool (*confirm_boot)(void);
} runtime_v1_legacy;
extern const runtime_v1_legacy* risc_runtime_get_api(uint32_t);
extern void provider_exit_event(const char*);
__attribute__((visibility("default"))) void app_main(void) {
  const runtime_v1_legacy* rt = risc_runtime_get_api(1);
  assert(rt && rt->api_version == 1 && rt->struct_size >= sizeof(*rt));
  health_v1 h = {.struct_size = sizeof(h)};
  grant_v1 g = {.struct_size = sizeof(g)};
  assert(rt->health(&h) && rt->diagnostic("legacy-prefix"));
  assert(rt->acquire("test.leaf", 1, 0, &g));
  assert(rt->release(&g));
  rt->yield_ms(1);
  (void)rt->confirm_boot();
  provider_exit_event("legacy:complete");
}
