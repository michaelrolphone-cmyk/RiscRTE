#pragma once
/* Private loader interface: never register these functions in an ELF export
 * table or expose them through an application capability. They only control
 * import resolution while trusted firmware is relocating a verified provider.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RISC_PRIVILEGED_OS_CPU_ABI_V1 1u
#define RISC_PROVIDER_DIAGNOSTIC_ABI_V1 1u

/* No runtime/task authority is granted by this availability query. */
bool esp_elf_privileged_diagnostic_abi_supported_v1(uint32_t abi);

/* The caller MUST authenticate package identity, signature, ABI and imports.
 * This mechanism is not itself a package verifier or memory sandbox.
 * At most one privileged relocation scope may exist at a time. */
bool esp_elf_privileged_os_cpu_begin_v1(void);
/* Narrower admission for an immutable, validated, exact import declaration.
 * Names are borrowed only until end_v1. No name resolves before entry into
 * the one authorized relocation or after it leaves. This is not public ABI. */
bool esp_elf_privileged_os_cpu_begin_selected_v1(
    const char *const *imports, size_t count);
/* Explicit diagnostic policy, 0 disables, 1 selects the bounded native sink. */
bool esp_elf_privileged_os_cpu_begin_selected_diagnostics_v1(
    const char *const *imports, size_t count, uint32_t diagnostic_abi);
/* True only for the owner and an allowed name in its active selected scope.
 * The legacy fixed-inventory scope retains its previous lookup behavior. */
bool esp_elf_privileged_os_cpu_import_allowed_v1(const char *symbol);
/* Refuses to release while a relocation remains active or for another task. */
bool esp_elf_privileged_os_cpu_end_v1(void);
/* True only for the task holding the privileged scope. */
bool esp_elf_privileged_os_cpu_scope_owned_v1(void);
/* Only the owning task can resolve names; other tasks always get zero. */
uintptr_t esp_elf_privileged_os_cpu_lookup_v1(const char *symbol);
size_t esp_elf_privileged_os_cpu_symbol_count_v1(void);

/* One-shot module-specific authorization, private to trusted ELF admission.
 * After successful esp_elf_init and BEFORE calling esp_elf_relocate, authorize
 * the exact esp_elf_t address. At most one relocation may consume this grant.
 * The ordinary esp_elf_relocate entry calls enter/leave for EVERY module.
 * If this task owns a privileged scope, any unbound or nested ordinary ELF
 * relocation is denied. Other tasks remain able to relocate normal apps.
 * All three functions are never exported to ELF applications/providers. */
bool esp_elf_privileged_os_cpu_authorize_relocation_v1(const void *module);
bool esp_elf_privileged_os_cpu_relocation_enter_v1(const void *module);
bool esp_elf_privileged_os_cpu_relocation_leave_v1(const void *module);

#ifdef __cplusplus
}
#endif
