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

/* The caller MUST authenticate package identity, signature, ABI and imports.
 * This mechanism is not itself a package verifier or memory sandbox.
 * At most one privileged relocation scope may exist at a time. */
bool esp_elf_privileged_os_cpu_begin_v1(void);
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
