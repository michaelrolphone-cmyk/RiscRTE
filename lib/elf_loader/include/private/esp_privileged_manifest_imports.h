/* Private signed-manifest import contract for a hardware-owning provider ELF.
 * This checks that the declared import set exactly matches actual ELF imports;
 * it does NOT authenticate the declarations. The caller must obtain them from
 * an authenticated package manifest, never from unsigned JSON or an app.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Declarations are nonempty, strictly sorted, unique, bounded names. Both
 * .dynsym and .symtab are inspected; repeated imports across those tables
 * count only once. Every declared import must appear in the ELF, and every
 * undefined ELF import must be declared. The v1 fixed ABI allowlist is also
 * enforced independently. A NULL/empty declaration never grants privilege. */
bool esp_elf_privileged_manifest_imports_match_v1(
    const uint8_t *image, size_t length,
    const char *const *declared_imports, size_t declared_count);

#ifdef __cplusplus
}
#endif
