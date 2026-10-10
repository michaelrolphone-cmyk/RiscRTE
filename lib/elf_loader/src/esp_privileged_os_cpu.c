/* Generic privileged kernel/CPU ABI for independently built hardware ELFs.
 * Versioned OS/CPU symbols only; physical drivers remain inside provider ELF.
 * A native ELF is not memory-isolated; authenticated admission is mandatory.
 */
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "private/esp_privileged_os_cpu.h"
#include "private/esp_privileged_elf.h"

/* Optional native diagnostics never become global libc hooks. Availability
 * requires all three substitutions and the exact linked diagnostic ABI. */
extern const uint32_t risc_provider_diagnostic_build_abi_v1 __attribute__((weak));
extern uint32_t risc_provider_diagnostic_abi_v1(void) __attribute__((weak));
extern int risc_provider_diagnostic_printf(const char *format, ...) __attribute__((weak));
extern int risc_provider_diagnostic_puts(const char *message) __attribute__((weak));
extern int risc_provider_diagnostic_putchar(int character) __attribute__((weak));

bool esp_elf_privileged_diagnostic_abi_supported_v1(uint32_t abi)
{
    if (abi == 0) return true;
    return abi == RISC_PROVIDER_DIAGNOSTIC_ABI_V1 &&
        &risc_provider_diagnostic_build_abi_v1 &&
        risc_provider_diagnostic_build_abi_v1 == abi &&
        risc_provider_diagnostic_abi_v1 && risc_provider_diagnostic_printf &&
        risc_provider_diagnostic_puts && risc_provider_diagnostic_putchar &&
        risc_provider_diagnostic_abi_v1() == abi;
}

/* Strong links intentionally fail firmware builds when the port ABI is absent.
 * The table contains addresses, not forwarding hardware driver functions. */
#define RISC_OS_CPU_SYMBOL(name) \
    extern const unsigned char risc_os_cpu_link_##name[] __asm__(#name);
#include "private/privileged_os_cpu_symbols_v1.def"
#undef RISC_OS_CPU_SYMBOL

typedef struct {
    const char *name;
    const void *address;
} risc_os_cpu_symbol_v1;

static const risc_os_cpu_symbol_v1 s_privileged_symbols_v1[] = {
#define RISC_OS_CPU_SYMBOL(name) { #name, risc_os_cpu_link_##name },
#include "private/privileged_os_cpu_symbols_v1.def"
#undef RISC_OS_CPU_SYMBOL
};

/* A private task-owned non-reentrant relocation scope. The module pointer is
 * a one-shot authorization: the normal esp_elf_relocate entry validates it
 * BEFORE mapping, including nested regular ELFs on the owner task. Other
 * tasks can load other modules but may not race the privileged module itself.
 * No global resolver pointer or customer export table is modified. */
static portMUX_TYPE s_scope_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_scope_owner = NULL;
static const void *s_scope_module = NULL;
static bool s_relocation_active = false;
static bool s_relocation_consumed = false;
static const char *const *s_selected_imports = NULL;
static size_t s_selected_count = 0;
static uint32_t s_diagnostic_abi = 0;

static bool begin_scope(const char *const *imports, size_t count, uint32_t diagnostic_abi)
{
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    if (caller == NULL) return false;
    bool acquired = false;
    taskENTER_CRITICAL(&s_scope_lock);
    if (s_scope_owner == NULL) {
        s_scope_module = NULL;
        s_relocation_active = false;
        s_relocation_consumed = false;
        s_selected_imports = imports;
        s_selected_count = count;
        s_diagnostic_abi = diagnostic_abi;
        s_scope_owner = caller;
        acquired = true;
    }
    taskEXIT_CRITICAL(&s_scope_lock);
    return acquired;
}

bool esp_elf_privileged_os_cpu_begin_v1(void)
{
    return begin_scope(NULL, 0, 0);
}

bool esp_elf_privileged_os_cpu_begin_selected_v1(
    const char *const *imports, size_t count)
{
    return esp_elf_privileged_os_cpu_begin_selected_diagnostics_v1(imports, count, 0);
}

bool esp_elf_privileged_os_cpu_begin_selected_diagnostics_v1(
    const char *const *imports, size_t count, uint32_t diagnostic_abi)
{
    if (!imports || count > 128 ||
        !esp_elf_privileged_diagnostic_abi_supported_v1(diagnostic_abi)) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!imports[i] || !imports[i][0] || strnlen(imports[i], 128) > 127 ||
            (i && strcmp(imports[i - 1], imports[i]) >= 0) ||
            !esp_elf_privileged_selected_import_supported_with_diagnostics_v1(imports[i], diagnostic_abi))
            return false;
    }
    return begin_scope(imports, count, diagnostic_abi);
}

bool esp_elf_privileged_os_cpu_end_v1(void)
{
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    bool released = false;
    taskENTER_CRITICAL(&s_scope_lock);
    if (caller != NULL && s_scope_owner == caller && !s_relocation_active) {
        s_scope_module = NULL;
        s_relocation_consumed = false;
        s_selected_imports = NULL;
        s_selected_count = 0;
        s_diagnostic_abi = 0;
        s_scope_owner = NULL;
        released = true;
    }
    taskEXIT_CRITICAL(&s_scope_lock);
    return released;
}

bool esp_elf_privileged_os_cpu_scope_owned_v1(void)
{
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    if (caller == NULL) return false;
    taskENTER_CRITICAL(&s_scope_lock);
    const bool owned = s_scope_owner == caller;
    taskEXIT_CRITICAL(&s_scope_lock);
    return owned;
}

bool esp_elf_privileged_os_cpu_import_allowed_v1(const char *symbol)
{
    if (!symbol || !symbol[0]) return false;
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    bool allowed = false;
    taskENTER_CRITICAL(&s_scope_lock);
    if (caller != NULL && caller == s_scope_owner) {
        if (s_selected_imports == NULL) allowed = true;
        else if (s_relocation_active) {
            for (size_t i = 0; i < s_selected_count; ++i) {
                if (strcmp(symbol, s_selected_imports[i]) == 0) {
                    allowed = true;
                    break;
                }
            }
        }
    }
    taskEXIT_CRITICAL(&s_scope_lock);
    return allowed;
}

bool esp_elf_privileged_os_cpu_authorize_relocation_v1(const void *module)
{
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    bool authorized = false;
    taskENTER_CRITICAL(&s_scope_lock);
    if (caller != NULL && s_scope_owner == caller && module != NULL &&
        s_scope_module == NULL && !s_relocation_active && !s_relocation_consumed) {
        s_scope_module = module;
        authorized = true;
    }
    taskEXIT_CRITICAL(&s_scope_lock);
    return authorized;
}

bool esp_elf_privileged_os_cpu_relocation_enter_v1(const void *module)
{
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    bool allowed = false;
    taskENTER_CRITICAL(&s_scope_lock);
    if (caller != NULL && s_scope_owner == caller) {
        if (module != NULL && module == s_scope_module &&
            !s_relocation_active && !s_relocation_consumed) {
            s_relocation_active = true;
            s_relocation_consumed = true;
            allowed = true;
        }
    } else {
        /* A different task may continue ordinary loads, but never mutate the
         * SAME module while the private scope holds its relocation grant.
         * This is an identity guard, not a global app-loading mutex. */
        allowed = (s_scope_owner == NULL || s_scope_module == NULL ||
                   module != s_scope_module);
    }
    taskEXIT_CRITICAL(&s_scope_lock);
    return allowed;
}

bool esp_elf_privileged_os_cpu_relocation_leave_v1(const void *module)
{
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    bool released = false;
    taskENTER_CRITICAL(&s_scope_lock);
    if (caller != NULL && s_scope_owner == caller) {
        if (module != NULL && module == s_scope_module && s_relocation_active) {
            s_relocation_active = false;
            released = true;
        }
    } else {
        /* A cross-task attempt cannot release the privileged module's grant. */
        released = (s_scope_owner == NULL || s_scope_module == NULL ||
                    module != s_scope_module);
    }
    taskEXIT_CRITICAL(&s_scope_lock);
    return released;
}

uintptr_t esp_elf_privileged_os_cpu_lookup_v1(const char *symbol)
{
    if (symbol == NULL || symbol[0] == '\0' ||
        !esp_elf_privileged_os_cpu_import_allowed_v1(symbol)) return 0;
    /* Legacy fixed-inventory scopes and unqualified selections stay denied.
     * The owner is the only writer, so its active scope pins this ABI value. */
    if (s_selected_imports && s_relocation_active && s_diagnostic_abi == 1) {
        if (strcmp(symbol, "printf") == 0) return (uintptr_t)&risc_provider_diagnostic_printf;
        if (strcmp(symbol, "puts") == 0) return (uintptr_t)&risc_provider_diagnostic_puts;
        if (strcmp(symbol, "putchar") == 0) return (uintptr_t)&risc_provider_diagnostic_putchar;
    }
    for (size_t i = 0; i < sizeof(s_privileged_symbols_v1) /
                           sizeof(s_privileged_symbols_v1[0]); ++i) {
        if (strcmp(symbol, s_privileged_symbols_v1[i].name) == 0)
            return (uintptr_t)s_privileged_symbols_v1[i].address;
    }
    return 0;
}

size_t esp_elf_privileged_os_cpu_symbol_count_v1(void)
{
    return sizeof(s_privileged_symbols_v1) / sizeof(s_privileged_symbols_v1[0]);
}
