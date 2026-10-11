/* Link-time availability matrix against the production loader and resolver.
 * Diagnostic implementations below stand in for independently linked native
 * components. Their presence alone must never authorize ABI-1 relocation. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_elf.h"
#include "private/elf_platform.h"
#include "private/esp_privileged_elf.h"
#include "private/esp_privileged_os_cpu.h"

#ifndef TEST_MARKER_ABI
#define TEST_MARKER_ABI 1
#endif
#ifndef TEST_QUERY_ABI
#define TEST_QUERY_ABI 1
#endif
#ifndef TEST_AVAILABLE
#define TEST_AVAILABLE 0
#endif

#ifndef TEST_MISSING_MARKER
const uint32_t risc_provider_diagnostic_build_abi_v1 = TEST_MARKER_ABI;
#endif
#ifndef TEST_MISSING_QUERY
uint32_t risc_provider_diagnostic_abi_v1(void) { return TEST_QUERY_ABI; }
#endif
#ifndef TEST_MISSING_PRINTF
int risc_provider_diagnostic_printf(const char* format, ...) { (void)format; return -1; }
#endif
#ifndef TEST_MISSING_PUTS
int risc_provider_diagnostic_puts(const char* text) { (void)text; return -1; }
#endif
#ifndef TEST_MISSING_PUTCHAR
int risc_provider_diagnostic_putchar(int value) { (void)value; return -1; }
#endif

static void* task = (void*)1;
static unsigned allocations, relocated;
static const char* const diagnostics[] = {"printf", "putchar", "puts"};
static const char* const ordinary[] = {"memcpy"};
static const char* const* active_imports;
static size_t active_count;

void* diagnostic_test_task(void) { return task; }
void* esp_elf_malloc(uint32_t size, bool executable) {
    (void)executable;
    void* result = malloc(size);
    if (result) ++allocations;
    return result;
}
void esp_elf_free(void* memory) {
    if (memory) { assert(allocations); --allocations; free(memory); }
}
int esp_elf_arch_flush(esp_elf_t* module) { (void)module; return 0; }
int esp_elf_arch_relocate(esp_elf_t* module, const elf32_rela_t* relocation,
                         const elf32_sym_t* symbol, uint32_t address) {
    (void)module; (void)relocation; (void)symbol; assert(address);
    ++relocated;
    assert(esp_elf_privileged_os_cpu_scope_owned_v1());
    for (size_t i = 0; i < active_count; ++i) assert(elf_find_sym(active_imports[i]));
    return 0;
}

static uint8_t* read_image(const char* path, size_t* size) {
    FILE* file = fopen(path, "rb"); assert(file);
    assert(!fseek(file, 0, SEEK_END));
    const long length = ftell(file); assert(length > 0); rewind(file);
    uint8_t* bytes = malloc((size_t)length); assert(bytes);
    assert(fread(bytes, 1, (size_t)length, file) == (size_t)length);
    assert(!fclose(file)); *size = (size_t)length;
    return bytes;
}

static void scopes(void) {
    assert(esp_elf_privileged_diagnostic_abi_supported_v1(0));
    assert(esp_elf_privileged_diagnostic_abi_supported_v1(1) == TEST_AVAILABLE);
    assert(!esp_elf_privileged_diagnostic_abi_supported_v1(2));
    assert(esp_elf_privileged_selected_import_supported_with_diagnostics_v1("memcpy", 0));
    assert(esp_elf_privileged_selected_import_supported_with_diagnostics_v1("memcpy", 1) == TEST_AVAILABLE);
    for (size_t i = 0; i < 3; ++i) {
        assert(!elf_find_sym(diagnostics[i]));
        assert(!esp_elf_privileged_selected_import_supported_v1(diagnostics[i]));
        assert(!esp_elf_privileged_selected_import_supported_with_diagnostics_v1(diagnostics[i], 0));
        assert(esp_elf_privileged_selected_import_supported_with_diagnostics_v1(diagnostics[i], 1) == TEST_AVAILABLE);
        assert(!esp_elf_privileged_selected_import_supported_with_diagnostics_v1(diagnostics[i], 2));
    }
    assert(esp_elf_privileged_os_cpu_begin_v1());
    for (size_t i = 0; i < 3; ++i) assert(!elf_find_sym(diagnostics[i]));
    assert(esp_elf_privileged_os_cpu_end_v1());
    assert(!esp_elf_privileged_os_cpu_begin_selected_v1(diagnostics, 3));
    assert(!esp_elf_privileged_os_cpu_begin_selected_diagnostics_v1(diagnostics, 3, 2));
    assert(esp_elf_privileged_os_cpu_begin_selected_diagnostics_v1(diagnostics, 3, 1) == TEST_AVAILABLE);
#if TEST_AVAILABLE
    for (size_t i = 0; i < 3; ++i) assert(!elf_find_sym(diagnostics[i]));
    esp_elf_t module = {0};
    assert(esp_elf_privileged_os_cpu_authorize_relocation_v1(&module));
    assert(esp_elf_privileged_os_cpu_relocation_enter_v1(&module));
    assert(elf_find_sym("printf") == (uintptr_t)&risc_provider_diagnostic_printf);
    assert(elf_find_sym("puts") == (uintptr_t)&risc_provider_diagnostic_puts);
    assert(elf_find_sym("putchar") == (uintptr_t)&risc_provider_diagnostic_putchar);
    assert(!elf_find_sym("memcpy"));
    task = (void*)2;
    for (size_t i = 0; i < 3; ++i) assert(!elf_find_sym(diagnostics[i]));
    task = (void*)1;
    assert(esp_elf_privileged_os_cpu_relocation_leave_v1(&module));
    for (size_t i = 0; i < 3; ++i) assert(!elf_find_sym(diagnostics[i]));
    assert(esp_elf_privileged_os_cpu_end_v1());
#endif
    assert(!esp_elf_privileged_os_cpu_scope_owned_v1());
}

static void relocate(const char* diagnostic_path, const char* ordinary_path) {
    size_t size;
    uint8_t* bytes = read_image(diagnostic_path, &size);
    esp_elf_t module = {0};
    active_imports = diagnostics; active_count = 3;
    assert(esp_elf_relocate_privileged_selected_v1(&module, bytes, size, diagnostics, 3) == -EINVAL);
    assert(esp_elf_relocate_privileged_selected_diagnostics_v1(&module, bytes, size, diagnostics, 3, 2) == -EINVAL);
    const int result = esp_elf_relocate_privileged_selected_diagnostics_v1(&module, bytes, size, diagnostics, 3, 1);
    assert(result == (TEST_AVAILABLE ? 0 : -EINVAL));
    assert(TEST_AVAILABLE ? relocated > 0 : relocated == 0);
    if (!result) esp_elf_deinit(&module);
    assert(!allocations && !esp_elf_privileged_os_cpu_scope_owned_v1());
    for (size_t i = 0; i < 3; ++i) assert(!elf_find_sym(diagnostics[i]));
    free(bytes);

    /* An unavailable ABI also fails when the import list has no diagnostics.
     * ABI 0 must continue to admit the ordinary import in every link profile. */
    bytes = read_image(ordinary_path, &size);
    active_imports = ordinary; active_count = 1;
    const unsigned before = relocated;
    const int selected = esp_elf_relocate_privileged_selected_diagnostics_v1(&module, bytes, size, ordinary, 1, 1);
    assert(selected == (TEST_AVAILABLE ? 0 : -EINVAL));
    assert(TEST_AVAILABLE ? relocated > before : relocated == before);
    if (!selected) esp_elf_deinit(&module);
    assert(!allocations && !esp_elf_privileged_os_cpu_scope_owned_v1());
    assert(!esp_elf_relocate_privileged_selected_v1(&module, bytes, size, ordinary, 1));
    esp_elf_deinit(&module);
    assert(!allocations && !esp_elf_privileged_os_cpu_scope_owned_v1());
    free(bytes);
}

int main(int argc, char** argv) {
    assert(argc == 3);
    scopes(); relocate(argv[1], argv[2]);
    return 0;
}
