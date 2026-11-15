#pragma once
#include "private/esp_cpu_task_scope_binding_v1.h"
/* Host-private additive mapping revision. NOT an ELF import. Existing v1
 * objects/callers are not reinterpreted, expanded or granted broader bounds.
 * All ranges are copied into the SAME retained Module/worker attachment. */
#define ESP_CPU_TASK_MAPPING_TAG_V2 UINT32_C(0x4d415032)
typedef struct {
    uint32_t struct_size;
    uint32_t tag;
    esp_cpu_task_mapping_v1 legacy;
    esp_cpu_task_region_v1 internal_executable;
    esp_cpu_task_region_v1 internal_data;
    esp_cpu_task_region_v1 internal_bss;
} esp_cpu_task_mapping_v2;
#ifdef __cplusplus
extern "C" {
#endif
int esp_cpu_task_binding_begin_v2(const esp_cpu_task_mapping_v2 *, esp_cpu_task_binding_v1 *);
#ifdef __cplusplus
}
#endif
