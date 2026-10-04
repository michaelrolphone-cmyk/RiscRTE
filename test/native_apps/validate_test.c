#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "private/elf_types.h"
extern bool esp_elf_validate_file(const uint8_t *, size_t);
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    uint8_t *buf = malloc(size);
    assert(buf && fread(buf, 1, size, f) == (size_t)size);
    fclose(f);
    assert(esp_elf_validate_file(buf, size));
    // All truncated section-table variants and short headers must be rejected.
    for (size_t n = 0; n < 52; ++n) assert(!esp_elf_validate_file(buf, n));
    elf32_hdr_t *h = (elf32_hdr_t *)buf;
    for (size_t n = h->shoff; n < (size_t)size; ++n) assert(!esp_elf_validate_file(buf, n));
    const elf32_hdr_t good = *h;
    h->machine = 3; assert(!esp_elf_validate_file(buf, size)); *h = good;
    h->shoff = UINT32_MAX; assert(!esp_elf_validate_file(buf, size)); *h = good;
    h->shstrndx = h->shnum; assert(!esp_elf_validate_file(buf, size)); *h = good;
    h->shentsize = 1; assert(!esp_elf_validate_file(buf, size)); *h = good;
    elf32_shdr_t *s = (elf32_shdr_t *)(buf + h->shoff);
    for (unsigned i = 1; i < h->shnum; ++i) {
        const elf32_shdr_t orig = s[i];
        s[i].name = UINT32_MAX; assert(!esp_elf_validate_file(buf, size)); s[i] = orig;
        s[i].size = UINT32_MAX; assert(!esp_elf_validate_file(buf, size)); s[i] = orig;
        if (s[i].type == SHT_SYNSYM) {
            s[i].type = SHT_PROGBITS;
            assert(!esp_elf_validate_file(buf, size));
            s[i] = orig;
        }
        if (s[i].type == SHT_RELA && s[i].size) {
            elf32_rela_t *r = (elf32_rela_t *)(buf + s[i].offset);
            const elf32_rela_t saved = *r;
            r->info = UINT32_MAX; assert(!esp_elf_validate_file(buf, size)); *r = saved;
            r->offset = UINT32_MAX; assert(!esp_elf_validate_file(buf, size)); *r = saved;
        }
    }
    assert(esp_elf_validate_file(buf, size));
    free(buf);
    puts("Native ELF structural validation tests passed");
}
