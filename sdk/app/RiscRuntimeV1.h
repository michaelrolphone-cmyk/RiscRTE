#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_RUNTIME_API_V1 1u
/* Minimal headless runtime service. Append-only. Available only on the active
 * app owner task, from module init through fini. Native apps are trusted code.
 * No pointers/callbacks/tasks may outlive app_main/fini. */
typedef struct {
  uint32_t struct_size;
  uint32_t uptime_ms, free_heap, app_address;
  uint8_t mac[6];
  char target[96];
} risc_runtime_health_v1;
typedef struct {
  uint32_t api_version, struct_size;
  bool (*health)(risc_runtime_health_v1* out);
  void (*yield_ms)(uint32_t milliseconds); /* clamp 1..50; polls providers */
  bool (*diagnostic)(const char* line); /* one-way, max 255 bytes, newline added */
  /* Copies one normalized relative .elf path under the configured boot store.
   * True queues a handoff: return from app_main immediately. Current ELF is
   * finalized and unmapped before the next load. A second request is denied.
   * Failure to unload/quiesce blocks handoff. Child return/load failure reloads the configured default from scratch.
   * Default return enters Idle; default failure enters Error. */
  bool (*request_launch)(const char* relative_elf);
} risc_runtime_api_v1;
const risc_runtime_api_v1* risc_runtime_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif
