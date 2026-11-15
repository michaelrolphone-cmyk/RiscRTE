/* Exact optional observer signatures from the pinned loader. Instrumentation
 * is not image/grant/allocator authority. Registered imports must not be used by
 * these privileged-path cases; abort instead of silently granting a result. */
#include <stdint.h>
#include <stdlib.h>
void risc_perf_loader_event(uint32_t phase, uint32_t value) {
    (void)phase; (void)value;
}
void esp_elf_registered_symbol_used(const void *table, const char *name, uintptr_t address) {
    (void)table; (void)name; (void)address;
    abort();
}
