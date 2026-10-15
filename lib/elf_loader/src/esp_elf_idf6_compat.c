/* Private OS/CPU ABI v1 compatibility for native IDF6. The old Xtensa
 * portYIELD_FROM_ISR macro called this variadic function; SDK6 inlines the
 * identical trace/switch sequence. Preserve the exact old argc/int semantics.
 * Sources: ESP-IDF v4.4.7 components/freertos/port/xtensa/port.c and
 * pinned v6.1 FreeRTOS-Kernel/portable/xtensa/include/freertos/portmacro.h.
 * This does not establish compatibility for old drivers' private SDK structs.
 */
#if RISC_NATIVE_ESP_IDF
#include <stdarg.h>
#include <esp_attr.h>
#include <esp_idf_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(6, 1, 0)
#error "Re-audit the native ELF yield bridge for this SDK"
#endif
void IRAM_ATTR vPortEvaluateYieldFromISR(int argc, ...)
{
    int requested = pdTRUE;
    va_list args;
    va_start(args, argc);
    if (argc) requested = va_arg(args, int);
    va_end(args);
    if (requested == pdTRUE) {
        traceISR_EXIT_TO_SCHEDULER();
        _frxt_setup_switch();
    }
}
#endif
