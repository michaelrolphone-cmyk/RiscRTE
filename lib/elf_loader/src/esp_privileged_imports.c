/* Import allowlist for the private generic OS/CPU provider loader.
 * NO device implementation or dynamic-module symbol is an approved import.
 * This is compatibility preflight, not signer authentication or a sandbox.
 */
#include <string.h>
#include "private/elf_types.h"
#include "private/esp_privileged_imports.h"

/* Snapshot the ordinary libc contract at ABI v1. Do not call
 * elf_find_sym_default(): it also searches customer and already-loaded ELF
 * exports, which cannot authorize a hardware-owning provider.
 * Keep this snapshot synchronized with g_esp_libc_elfsyms and strict audits.
 */
static const char *const s_public_libc[] = {
#define RISC_PUBLIC_LIBC_COMPAT_SYMBOL(name) #name,
#include "private/privileged_public_libc_compat_v1.def"
#undef RISC_PUBLIC_LIBC_COMPAT_SYMBOL
};
static const char *const s_privileged[] = {
#define RISC_OS_CPU_SYMBOL(name) #name,
#include "private/privileged_os_cpu_symbols_v1.def"
#undef RISC_OS_CPU_SYMBOL
};

static const char *const s_public_libc_v2[] = {
#define RISC_PUBLIC_LIBC_COMPAT_SYMBOL(name) #name,
#include "private/privileged_public_libc_compat_v2.def"
#undef RISC_PUBLIC_LIBC_COMPAT_SYMBOL
    "printf", "puts", "putchar" /* availability requires diagnostic ABI 1 */
};
static const char *const s_privileged_v2[] = {
#define RISC_OS_CPU_SYMBOL(name) #name,
#include "private/privileged_os_cpu_symbols_v2.def"
#undef RISC_OS_CPU_SYMBOL
};

static bool within(size_t length, uint32_t start, uint32_t size)
{
    return start <= length && size <= length - start;
}

static bool permitted(const char *name, uint32_t abi)
{
    const char *const *libc = abi == 1 ? s_public_libc : s_public_libc_v2;
    const size_t libc_count = abi == 1 ? sizeof(s_public_libc)/sizeof(*s_public_libc) :
        sizeof(s_public_libc_v2)/sizeof(*s_public_libc_v2);
    const char *const *priv = abi == 1 ? s_privileged : s_privileged_v2;
    const size_t priv_count = abi == 1 ? sizeof(s_privileged)/sizeof(*s_privileged) :
        sizeof(s_privileged_v2)/sizeof(*s_privileged_v2);
    for (size_t i = 0; i < libc_count; ++i)
        if (strcmp(name, libc[i]) == 0) return true;
    for (size_t i = 0; i < priv_count; ++i)
        if (strcmp(name, priv[i]) == 0) return true;
    return false;
}

static bool symbol_table_ok(const uint8_t *image, size_t length,
                            const elf32_shdr_t *sections, uint32_t section_count,
                            const elf32_shdr_t *table, uint32_t abi)
{
    /* The relocator dereferences the symbol table named by each RELA section,
     * which may be .symtab (type 2), NOT just .dynsym (type 11). Validate both
     * even when a table currently has no relocation references, so a manifest
     * cannot smuggle an unapproved import into another table.
     */
    if (table->link >= section_count ||
        sections[table->link].type != SHT_STRTAB ||
        table->size % sizeof(elf32_sym_t) ||
        !within(length, table->offset, table->size)) return false;
    const elf32_shdr_t *names = &sections[table->link];
    if (!within(length, names->offset, names->size)) return false;
    const elf32_sym_t *symbols =
        (const elf32_sym_t *)(image + table->offset);
    const size_t count = table->size / sizeof(elf32_sym_t);
    for (size_t j = 0; j < count; ++j) {
        const elf32_sym_t *sym = &symbols[j];
        if (sym->shndx != SHN_UNDEF) continue;
        /* The only unnamed undefined symbol allowed is the table's null
         * entry. An unnamed or local later entry must not bypass policy. */
        if (j == 0 && sym->name == 0 && sym->info == 0) continue;
        const unsigned binding = ELF_ST_BIND(sym->info);
        if (binding != STB_GLOBAL && binding != STB_WEAK) return false;
        if (sym->name >= names->size) return false;
        const char *name = (const char *)image + names->offset + sym->name;
        const size_t remaining = names->size - sym->name;
        if (!name[0] || !memchr(name, 0, remaining < 1024 ? remaining : 1024) ||
            !permitted(name, abi)) return false;
    }
    return true;
}

static bool imports_valid(const uint8_t *image, size_t length, uint32_t abi)
{
    /* Also bound every read here: a future caller must not be able to skip
     * structural validation and use this routine to read out of bounds. */
    if (!image || length < sizeof(elf32_hdr_t) || length > 8u * 1024u * 1024u)
        return false;
    const elf32_hdr_t *header = (const elf32_hdr_t *)image;
    if (!header->shnum || header->shnum > 256 ||
        header->shentsize != sizeof(elf32_shdr_t) ||
        !within(length, header->shoff,
                (uint32_t)header->shnum * sizeof(elf32_shdr_t))) return false;
    const elf32_shdr_t *sections =
        (const elf32_shdr_t *)(image + header->shoff);
    unsigned dynamic_tables = 0;
    for (uint32_t i = 0; i < header->shnum; ++i) {
        const elf32_shdr_t *section = &sections[i];
        if (section->type == SHT_SYNSYM || section->type == SHT_SYMTAB) {
            if (section->type == SHT_SYNSYM && ++dynamic_tables != 1) return false;
            if (!symbol_table_ok(image, length, sections, header->shnum, section, abi))
                return false;
        } else if (section->type == SHT_RELA) {
            if (section->link >= header->shnum ||
                (sections[section->link].type != SHT_SYNSYM &&
                 sections[section->link].type != SHT_SYMTAB) ||
                section->size % sizeof(elf32_rela_t) ||
                !within(length, section->offset, section->size)) return false;
            const elf32_rela_t *relocations =
                (const elf32_rela_t *)(image + section->offset);
            const size_t count = section->size / sizeof(elf32_rela_t);
            const size_t symbols =
                sections[section->link].size / sizeof(elf32_sym_t);
            for (size_t j = 0; j < count; ++j) {
                if (ELF_R_SYM(relocations[j].info) >= symbols) return false;
            }
        } else if (section->type == SHT_REL) {
            /* The current Xtensa loader only accepts explicit-addend RELA. */
            return false;
        }
    }
    return dynamic_tables == 1;
}

bool esp_elf_privileged_imports_valid_v1(const uint8_t *image, size_t length)
{ return imports_valid(image, length, 1); }
bool esp_elf_privileged_imports_valid_v2(const uint8_t *image, size_t length)
{ return imports_valid(image, length, 2); }
