#include "private/esp_cpu_task_stack_v1.h"
#include <limits.h>
#include "freertos/FreeRTOS.h"
#include <xtensa/config/core.h>
#include <xtensa_context.h>

/* Exact pinned non-SMP Xtensa initializer uses these linker spans and constants.
 * No kernel/configuration edits or private-image TLS substitution are allowed. */
#if CONFIG_FREERTOS_SMP
#error "Requalify task stack initialization for SMP"
#endif
_Static_assert(CHAR_BIT == 8 && sizeof(StackType_t) == 1,
               "Pinned stack contract uses bytes");
_Static_assert(portSTACK_GROWTH == -1 && portBYTE_ALIGNMENT == 16,
               "Requalify task stack layout for this port");
_Static_assert(XT_STK_FRMSZ > 0 && XT_STK_FRMSZ <= UINT32_MAX,
               "Invalid initial frame size");
#if XCHAL_CP_NUM > 0
_Static_assert(XT_CP_SIZE <= UINT32_MAX, "Invalid coprocessor save size");
#endif
extern char _thread_local_data_start, _thread_local_data_end;
extern char _thread_local_bss_start, _thread_local_bss_end;

bool esp_cpu_task_stack_layout_current_v1(esp_cpu_task_stack_layout_v1_t *out) {
    if (!out) return false;
    const esp_cpu_task_stack_layout_v1_t layout = {
        (uintptr_t)&_thread_local_data_start, (uintptr_t)&_thread_local_data_end,
        (uintptr_t)&_thread_local_bss_start, (uintptr_t)&_thread_local_bss_end,
#if XCHAL_CP_NUM > 0
        XT_CP_SIZE,
#else
        0,
#endif
        XT_STK_FRMSZ, portBYTE_ALIGNMENT
    };
    *out = layout;
    return true;
}
