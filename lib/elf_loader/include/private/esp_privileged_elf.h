#pragma once
/* PRIVATE: only the firmware's trusted package loader may invoke this after
 * validating package signature, identity, ABI, and exact imported symbols.
 * The package verifier supplies the SHA-256 bound snapshot and the declared
 * imports from the SAME authenticated package; an arbitrary declaration list
 * or digest is not signing authority. No application-facing symbol export.
 */
#include <stddef.h>
#include <stdint.h>
#include "esp_elf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* On success, *module owns relocated code/data and must remain resident until
 * generic provider quiescence (ISR, DMA, tasks, callbacks) is established.
 * On failure, the partially mapped image is deinitialized by this routine.
 * The manifest's canonical, unique, sorted import names must match the ELF's
 * ACTUAL complete undefined import set across .dynsym and .symtab. A missing
 * or extra declaration fails BEFORE executable memory mapping or scope grant.
 * -EINVAL malformed/mismatched declarations, -EBUSY concurrent privileged
 * relocation, -EIO scope failure, or native relocator's negative error.
 */
int esp_elf_relocate_privileged_verified_v1(esp_elf_t *module,
                                            const uint8_t *verified_bytes,
                                            size_t verified_length,
                                            const char *const *signed_imports,
                                            size_t signed_import_count);

#ifdef __cplusplus
}
#endif
