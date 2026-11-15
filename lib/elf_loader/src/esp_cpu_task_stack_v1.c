#include "private/esp_cpu_task_stack_v1.h"
#include <stddef.h>

static bool add_u32(uint32_t a, uint32_t b, uint32_t *out) {
    if (b > UINT32_MAX - a) return false;
    *out = a + b;
    return true;
}
static bool round_up(uint32_t value, uint32_t alignment, uint32_t *out) {
    const uint32_t remainder = value & (alignment - 1);
    return add_u32(value, remainder ? alignment - remainder : 0, out);
}
static bool span(uintptr_t start, uintptr_t end, uint32_t *out) {
    if (end < start) return false;
    const uintptr_t bytes = end - start;
#if UINTPTR_MAX > UINT32_MAX
    /* Wider hosted/port addresses need a checked narrowing. On the pinned
     * 32-bit port every uintptr_t span is already representable in uint32_t. */
    if (bytes > (uintptr_t)UINT32_MAX) return false;
#endif
    *out = (uint32_t)bytes;
    return true;
}
bool esp_cpu_task_stack_floor_v1(const esp_cpu_task_stack_layout_v1_t *layout,
                               uint32_t working_stack_bytes, uint32_t *out) {
    if (!layout || !out || !working_stack_bytes || !layout->initial_frame_bytes ||
        !layout->alignment || (layout->alignment & (layout->alignment - 1)) ||
        layout->tls_data_end > layout->tls_bss_start) return false;
    uint32_t data, bss, tls, total = layout->alignment, region;
    if (!span(layout->tls_data_start, layout->tls_data_end, &data) ||
        !span(layout->tls_bss_start, layout->tls_bss_end, &bss) ||
        !add_u32(data, bss, &tls)) return false;
    const uint32_t regions[] = {layout->coprocessor_bytes, tls,
                                layout->initial_frame_bytes, working_stack_bytes};
    for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i) {
        if (!round_up(regions[i], layout->alignment, &region) ||
            !add_u32(total, region, &total)) return false;
    }
    *out = total;
    return true;
}
