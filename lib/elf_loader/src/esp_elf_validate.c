/* T5S3 integration: bounded structural checks before the upstream relocator.
 * This checks format, not the safety of native instructions. Trusted apps only.
 */
#include <string.h>
#include <sys/types.h>
#include "private/elf_types.h"

#include "private/esp_elf_data_layout.h"
#define MAX_IMAGE ESP_ELF_MAX_IMAGE_BYTES
static bool range(size_t length, uint32_t offset, uint64_t size) {
    return offset <= length && size <= length - offset;
}
static bool string_at(const uint8_t *buf, const elf32_shdr_t *str, uint32_t offset) {
    if (offset >= str->size) return false;
    const size_t remaining = str->size - offset;
    return memchr(buf + str->offset + offset, 0, remaining < 1024 ? remaining : 1024) != NULL;
}
static bool mapped(const elf32_shdr_t *sections, unsigned count, uint32_t addr, unsigned size) {
    for (unsigned i = 0; i < count; ++i) {
        const elf32_shdr_t *s = &sections[i];
        if ((s->flags & SHF_ALLOC) && addr >= s->addr && (uint64_t)addr + size <= (uint64_t)s->addr + s->size)
            return true;
    }
    return false;
}
bool esp_elf_validate_file(const uint8_t *buf, size_t length) {
    if (!buf || length < sizeof(elf32_hdr_t) || length > MAX_IMAGE) return false;
    const elf32_hdr_t *h = (const elf32_hdr_t *)buf;
    if (memcmp(h->ident, "\177ELF\1\1\1", 7) || h->machine != 94 || h->type != 3 || h->version != 1 ||
        h->ehsize != sizeof(*h) || h->shentsize != sizeof(elf32_shdr_t) || !h->shnum ||
        h->shnum > 256 || h->shstrndx >= h->shnum || h->shoff % 4 ||
        !range(length, h->shoff, (uint64_t)h->shnum * sizeof(elf32_shdr_t))) return false;
    const elf32_shdr_t *s = (const elf32_shdr_t *)(buf + h->shoff);
    const elf32_shdr_t *names = &s[h->shstrndx];
    if (names->type != SHT_STRTAB || !range(length, names->offset, names->size)) return false;
    uint64_t allocated = 0;
    bool text = false, dynsym = false;
    for (unsigned i = 0; i < h->shnum; ++i) {
        if (!string_at(buf, names, s[i].name) || s[i].size > MAX_IMAGE ||
            (s[i].type != SHT_NOBITS && !range(length, s[i].offset, s[i].size))) return false;
        const char *name = (const char *)buf + names->offset + s[i].name;
        if (!strcmp(name, ELF_DYNSYM)) {
            if (dynsym || s[i].type != SHT_SYNSYM) return false;
            dynsym = true;
        }
        if (s[i].flags & SHF_ALLOC) {
            allocated += s[i].size;
            if (allocated > MAX_IMAGE || (uint64_t)s[i].addr + s[i].size > UINT32_MAX) return false;
        }
        if (!strcmp(name, ELF_TEXT)) {
            if (text || s[i].type != SHT_PROGBITS || !(s[i].flags & SHF_EXECINSTR) || !s[i].size ||
                !(s[i].flags & SHF_ALLOC)) return false;
            text = true;
        }
        if (s[i].type == SHT_SYNSYM || s[i].type == SHT_SYMTAB) {
            if (s[i].size / sizeof(elf32_sym_t) > 65535 || s[i].offset % 4 || s[i].size % sizeof(elf32_sym_t) || s[i].link >= h->shnum ||
                s[s[i].link].type != SHT_STRTAB || !range(length, s[s[i].link].offset, s[s[i].link].size)) return false;
            const elf32_sym_t *sym = (const elf32_sym_t *)(buf + s[i].offset);
            for (unsigned j = 0; j < s[i].size / sizeof(*sym); ++j) {
                if (!string_at(buf, &s[s[i].link], sym[j].name)) return false;
                if (ELF_ST_TYPE(sym[j].info) == STT_FUNC && sym[j].shndx != SHN_UNDEF &&
                    (sym[j].shndx >= h->shnum || !mapped(s, h->shnum, sym[j].value, 1))) return false;
            }

        }
        if (s[i].type == SHT_RELA) {
            if (s[i].size % sizeof(elf32_rela_t) || s[i].offset % 4 || s[i].link >= h->shnum ||
                (s[s[i].link].type != SHT_SYNSYM && s[s[i].link].type != SHT_SYMTAB)) return false;
            const elf32_rela_t *rel = (const elf32_rela_t *)(buf + s[i].offset);
            for (unsigned j = 0; j < s[i].size / sizeof(*rel); ++j) {
                const unsigned type = ELF_R_TYPE(rel[j].info);
                if (ELF_R_SYM(rel[j].info) >= s[s[i].link].size / sizeof(elf32_sym_t) ||
                    (type != 2 && type != 3 && type != 4 && type != 5) ||
                    (rel[j].offset % 4) || !mapped(s, h->shnum, rel[j].offset, 4)) return false;
            }
        }
    }
    return text && dynsym;
}
