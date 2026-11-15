#pragma once
#include "RiscCpuTaskScopeV1.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Host-private. NEVER an ELF import. Module owns this retained attachment;
 * its resident slot is also its v5 worker descriptor, not a mapping registry.
 * No provider-callable function accepts module, mapping or bounds as authority. */
typedef struct { uint64_t token; } esp_cpu_task_binding_v1;
typedef struct { uintptr_t begin; size_t bytes; } esp_cpu_task_region_v1;
typedef struct {
    const void *module;
    uint64_t mapping;
    esp_cpu_task_region_v1 executable;
    esp_cpu_task_region_v1 data, bss;
} esp_cpu_task_mapping_v1;
int esp_cpu_task_binding_begin_v1(const esp_cpu_task_mapping_v1 *, esp_cpu_task_binding_v1 *);
/* Ends the exact creator's start window. Failed starts cancel only the
 * resident-held gate; pending cleanup retains Module rather than unmapping. */
int esp_cpu_task_binding_end_v1(esp_cpu_task_binding_v1, bool start_succeeded);
/* Host creator only: cancel an ended STAGED window before commit/private entry.
 * Bounded poll through the existing actual owner tail/scope close. No forced
 * deletion. COMMITTED/CLOSING refuses; Pending/Retained never authorizes release. */
int esp_cpu_task_binding_cancel_staged_v1(esp_cpu_task_binding_v1);
/* Called only after exact Graph root-grant/policy/mapping validation. No wait,
 * worker creation or callback. True is permission to enter, not readiness. */
bool esp_cpu_task_binding_commit_v1(esp_cpu_task_binding_v1, const void *module, uint64_t mapping);
void esp_cpu_task_binding_retain_v1(esp_cpu_task_binding_v1);
/* Host-side lifecycle barrier. True only after actual scoped cleanup and close,
 * or when no attachment exists; never infers PHY/callback image retirement. */
bool esp_cpu_task_binding_safe_v1(esp_cpu_task_binding_v1);
bool esp_cpu_task_binding_release_v1(esp_cpu_task_binding_v1 *);
#ifdef __cplusplus
}
#endif
