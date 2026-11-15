#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Private resident arithmetic/query boundary, never a provider import. The
 * layout is copied from the pinned kernel port and resident ELF linker symbols.
 * It describes initial writes, not arbitrary task or cleanup-callback demand. */
typedef struct {
    uintptr_t tls_data_start, tls_data_end, tls_bss_start, tls_bss_end;
    uint32_t coprocessor_bytes, initial_frame_bytes, alignment;
} esp_cpu_task_stack_layout_v1_t;

/* Production implementation reads real resident symbols/architecture constants.
 * Host tests instrument this narrow boundary, never pretend to run that port. */
bool esp_cpu_task_stack_layout_current_v1(esp_cpu_task_stack_layout_v1_t *out);

/* Checked conservative allocation floor in BYTES. Adds one full alignment unit
 * for the kernel's (base + size - 1) top and downward alignment, then separately
 * rounded coprocessor, TLS, initial-frame and explicit working-budget regions.
 * Requires nonzero working budget/frame and a power-of-two alignment. Rejects
 * malformed/overlapping TLS ranges, span/sum/rounding/total overflow. Output is
 * unchanged on failure. This is not a working-stack adequacy determination. */
bool esp_cpu_task_stack_floor_v1(const esp_cpu_task_stack_layout_v1_t *layout,
                               uint32_t working_stack_bytes, uint32_t *out);

#ifdef __cplusplus
}
#endif
