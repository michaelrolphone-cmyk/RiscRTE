/* Exact import-set matching is separate from privilege authorization. */
#include <string.h>
#include "private/elf_types.h"
#include "private/esp_privileged_imports.h"
#include "private/esp_privileged_manifest_imports.h"

#define MAX_MANIFEST_IMPORTS 128u
#define MAX_IMPORT_NAME 127u

bool esp_elf_privileged_manifest_imports_match_v1(
    const uint8_t *image, size_t length,
    const char *const *declared, size_t declared_count)
{
    /* An empty import list is legitimate ONLY for an ELF with no undefined
     * symbols in either table. Keep a nonnull metadata pointer so a missing
     * declaration remains distinguishable from an intentional empty list. */
    if (!image || !declared || declared_count > MAX_MANIFEST_IMPORTS ||
        !esp_elf_privileged_imports_valid_v1(image, length)) return false;

    for (size_t i = 0; i < declared_count; ++i) {
        if (!declared[i]) return false;
        const size_t size = strnlen(declared[i], MAX_IMPORT_NAME + 1u);
        if (!size || size > MAX_IMPORT_NAME ||
            (i && strcmp(declared[i - 1], declared[i]) >= 0)) return false;
    }
    uint8_t seen[MAX_MANIFEST_IMPORTS] = {0};
    const elf32_hdr_t *header = (const elf32_hdr_t *)image;
    const elf32_shdr_t *sections =
        (const elf32_shdr_t *)(image + header->shoff);
    for (size_t i = 0; i < header->shnum; ++i) {
        const elf32_shdr_t *table = &sections[i];
        if (table->type != SHT_SYNSYM && table->type != SHT_SYMTAB) continue;
        const elf32_shdr_t *names = &sections[table->link];
        const elf32_sym_t *symbols =
            (const elf32_sym_t *)(image + table->offset);
        const size_t count = table->size / sizeof(elf32_sym_t);
        for (size_t j = 0; j < count; ++j) {
            const elf32_sym_t *symbol = &symbols[j];
            if (symbol->shndx != SHN_UNDEF ||
                (j == 0 && !symbol->name && !symbol->info)) continue;
            const char *name = (const char *)image + names->offset + symbol->name;
            size_t k = 0;
            for (; k < declared_count; ++k) {
                const int order = strcmp(declared[k], name);
                if (order == 0) { seen[k] = 1; break; }
                if (order > 0) break;
            }
            if (k == declared_count || strcmp(declared[k], name) != 0)
                return false;
        }
    }
    for (size_t i = 0; i < declared_count; ++i)
        if (!seen[i]) return false;
    return true;
}
