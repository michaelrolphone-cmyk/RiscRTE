#pragma once
/* PRIVATE: import policy for the generic OS/CPU ABI. This checks symbol
 * compatibility, NOT package signature, identity or execution authorization.
 * The caller must authenticate the exact bytes independently. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Only call after esp_elf_validate_file has accepted this exact byte buffer.
 * Reject every undefined dynamic symbol outside the current public libc
 * contract and exact versioned privileged OS/CPU inventory. No dynamic-module
 * fallback or hardware-specific firmware export is permitted for a physical
 * provider. Inspects .dynsym, the native relocator's imported symbol table;
 * structural ELF validation is a separate mandatory predecessor. */
bool esp_elf_privileged_imports_valid_v1(const uint8_t *image, size_t length);

#ifdef __cplusplus
}
#endif
