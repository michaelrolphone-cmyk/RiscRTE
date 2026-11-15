/* Actual loader/validator/Xtensa relocation, modeled allocator and publication
 * boundaries. No native instructions, radio or crypto execute in this suite. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_elf.h"
#include "private/esp_elf_internal_v1.h"
#include "private/elf_platform.h"
#include "private/esp_privileged_os_cpu.h"
#include "esp_elf.c"

static bool privileged = true, supported = true;
static unsigned allocations, fail_at, live, internal_text, internal_data, external_text, external_data;
static int publish_result;
static void *owned[32];
static void *allocate(size_t bytes) {
    ++allocations;
    if (allocations == fail_at) return NULL;
    void *p = malloc(bytes); assert(p && live < 32);
    memset(p, 0xa5, bytes); owned[live++] = p; return p;
}
void *esp_elf_malloc(uint32_t bytes, bool exec) {
    if (exec) ++external_text; else ++external_data;
    return allocate(bytes);
}
void esp_elf_free(void *p) {
    if (!p) return;
    unsigned i; for (i = 0; i < live && owned[i] != p; ++i) {}
    assert(i < live); owned[i] = owned[--live]; free(p);
}
bool esp_elf_internal_supported_v1(void) { return supported; }
int esp_elf_internal_alloc_text_v1(uint32_t bytes, void **owner, uintptr_t *data, uintptr_t *code) {
    assert(bytes && !(bytes & 3u) && !*owner && !*data && !*code);
    ++internal_text; void *p = allocate(bytes); if (!p) return -ENOMEM;
    *owner = p; *data = (uintptr_t)p; *code = *data + 0x10000000u; return 0;
}
int esp_elf_internal_alloc_data_v1(uint32_t bytes, void **owner) {
    assert(bytes && !*owner); ++internal_data;
    *owner = allocate(bytes); return *owner ? 0 : -ENOMEM;
}
int esp_elf_internal_publish_v1(const esp_elf_t *e) { assert(e); return publish_result; }
int esp_elf_arch_flush(esp_elf_t *e) { assert(e); return 0; }
uintptr_t elf_remap_text(esp_elf_t *e, uintptr_t value) {
    const esp_elf_sec_t *s = &e->internal_v1.sec[0];
    if (e->internal_v1.tag == ESP_ELF_INTERNAL_TAG_V1 && esp_elf_span_contains_v1(s->addr, s->size, value, 1))
        return e->internal_v1.text_code + value - s->addr;
    return value;
}
bool esp_elf_privileged_os_cpu_scope_owned_v1(void) { return privileged; }
bool esp_elf_privileged_os_cpu_relocation_enter_v1(const void *e) { return e != NULL; }
bool esp_elf_privileged_os_cpu_relocation_leave_v1(const void *e) { return e != NULL; }
uintptr_t elf_find_sym_default(const char *s) { (void)s; return 0; }

typedef struct {
    elf32_hdr_t header;
    elf32_shdr_t section[12];
    char names[256], symbols[64];
    elf32_sym_t symbol[3];
    uint32_t text[4], data[4], internal_code[4], internal_bytes[4], readonly[4];
} Fixture;
static void add(Fixture *f, unsigned i, const char *name, uint32_t type, uint32_t flags,
                uint32_t address, size_t offset, uint32_t bytes) {
    size_t n = 1;
    for (unsigned j = 1; j < i; ++j) {
        size_t end = f->section[j].name + strlen(f->names + f->section[j].name) + 1;
        if (end > n) n = end;
    }
    assert(n + strlen(name) + 1 <= sizeof(f->names)); strcpy(f->names + n, name);
    f->section[i] = (elf32_shdr_t){.name = (uint32_t)n, .type = type, .flags = flags,
        .addr = address, .offset = (uint32_t)offset, .size = bytes, .addralign = 4};
}
static Fixture fixture(bool extended) {
    Fixture f = {0};
    memcpy(f.header.ident, "\177ELF\1\1\1", 7); f.header.type = 3; f.header.machine = 94;
    f.header.version = 1; f.header.ehsize = sizeof(f.header); f.header.shoff = offsetof(Fixture, section);
    f.header.shentsize = sizeof(elf32_shdr_t); f.header.shnum = extended ? 11 : 7; f.header.shstrndx = 1;
    add(&f, 1, ".shstrtab", SHT_STRTAB, 0, 0, offsetof(Fixture, names), sizeof(f.names));
    add(&f, 2, ".dynstr", SHT_STRTAB, 0, 0, offsetof(Fixture, symbols), sizeof(f.symbols));
    add(&f, 3, ".dynsym", SHT_SYNSYM, 0, 0, offsetof(Fixture, symbol), sizeof(f.symbol));
    f.section[3].link = 2;
    add(&f, 4, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0, offsetof(Fixture, text), sizeof(f.text));
    add(&f, 5, ".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0x100, offsetof(Fixture, data), sizeof(f.data));
    add(&f, 6, ".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 0x200, 0, 16);
    if (extended) {
        add(&f, 7, ".risc.internal.text", SHT_PROGBITS, 6, 0x1000, offsetof(Fixture, internal_code), 16);
        add(&f, 8, ".risc.internal.data", SHT_PROGBITS, 3, 0x1100, offsetof(Fixture, internal_bytes), 16);
        add(&f, 9, ".risc.internal.rodata", SHT_PROGBITS, 2, 0x1200, offsetof(Fixture, readonly), 16);
        add(&f, 10, ".risc.internal.bss", SHT_NOBITS, 3, 0x1300, 0, 16);
    }
    strcpy(f.symbols + 1, "ordinary"); strcpy(f.symbols + 16, "internal");
    f.symbol[1] = (elf32_sym_t){.name = 1, .value = 0, .size = 0,
        .info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC), .shndx = 4};
    if (extended) f.symbol[2] = (elf32_sym_t){.name = 16, .value = 0x1000, .size = 4,
        .info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC), .shndx = 7};
    f.internal_code[0] = 0x01020304; f.internal_bytes[0] = 0x05060708; f.readonly[0] = 0x11223344;
    return f;
}
extern bool esp_elf_validate_file(const uint8_t *, size_t);
static void legacy_real(const char *path) {
    FILE *file = fopen(path, "rb"); assert(file);
    assert(!fseek(file, 0, SEEK_END)); long length = ftell(file); assert(length == 16756);
    rewind(file); unsigned char *bytes = malloc((size_t)length); assert(bytes);
    assert(fread(bytes, 1, (size_t)length, file) == (size_t)length); assert(!fclose(file));
    assert(esp_elf_validate_file(bytes, (size_t)length));
    esp_elf_t image; assert(!esp_elf_init(&image)); assert(!esp_elf_load_section(&image, bytes));
    const elf32_hdr_t *h = (const elf32_hdr_t *)bytes;
    const elf32_shdr_t *sections = (const elf32_shdr_t *)(bytes + h->shoff);
    const char *names = (const char *)bytes + sections[h->shstrndx].offset;
    unsigned functions = 0;
    for (unsigned i = 0; i < h->shnum; ++i) if (sections[i].type == SHT_SYNSYM) {
        const elf32_sym_t *symbols = (const elf32_sym_t *)(bytes + sections[i].offset);
        const char *strings = (const char *)bytes + sections[sections[i].link].offset;
        for (unsigned j = 0; j < sections[i].size / sizeof(*symbols); ++j) {
            const elf32_sym_t *s = &symbols[j];
            if (ELF_ST_BIND(s->info) == STB_GLOBAL && ELF_ST_TYPE(s->info) == STT_FUNC && s->shndx != SHN_UNDEF) {
                assert(!strcmp(strings + s->name, "t5_driver_get"));
                assert(elf_function_export_addr(&image, s, sections, h->shnum, names)); ++functions;
            }
        }
    }
    assert(functions == 1 && !image.internal_v1.tag && !internal_text && !internal_data);
    esp_elf_deinit(&image); free(bytes); assert(!live);
}
int main(int argc, char **argv) {
    assert(argc >= 2); const char *test = argv[1];
    if (!strcmp(test, "legacy-real")) {
        assert(argc == 3); legacy_real(argv[2]); printf("PASS %s\n", test); return 0;
    }
    assert(argc == 2);
    bool legacy = !strcmp(test, "legacy-zero-vaddr") || !strcmp(test, "legacy-port-unavailable");
    Fixture f = fixture(!legacy); esp_elf_t image; assert(!esp_elf_init(&image));
    if (!strcmp(test, "internal-zero-vaddr")) {
        f.section[4].addr = f.symbol[1].value = f.header.entry = 0x2000;
        f.section[7].addr = f.symbol[2].value = 0;
    }
    if (!strcmp(test, "internal-entry")) f.header.entry = f.section[7].addr;
    if (!strcmp(test, "flags")) f.section[7].flags |= SHF_WRITE;
    if (!strcmp(test, "alignment")) f.section[7].addralign = 3;
    if (!strcmp(test, "duplicate")) f.section[8].name = f.section[7].name;
    if (!strcmp(test, "unknown-class")) strcpy(f.names + f.section[10].name, ".risc.internal.bad");
    if (!strcmp(test, "flags") || !strcmp(test, "alignment") || !strcmp(test, "duplicate") || !strcmp(test, "unknown-class")) {
        assert(!esp_elf_validate_file((const uint8_t *)&f, sizeof(f))); assert(!allocations); goto done;
    }
    assert(esp_elf_validate_file((const uint8_t *)&f, sizeof(f)));
    if (!strcmp(test, "overlap")) f.section[7].addr = 8;
    if (!strcmp(test, "unprivileged")) privileged = false;
    if (!strcmp(test, "unsupported") || !strcmp(test, "legacy-port-unavailable")) supported = false;
    if (!strncmp(test, "allocation-fail-", 16)) fail_at = (unsigned)strtoul(test + 16, NULL, 10);
    if (!strcmp(test, "publication-fail")) publish_result = -EIO;
    int rc = esp_elf_relocate(&image, (const uint8_t *)&f);
    if (fail_at || publish_result || !privileged || (!supported && !legacy) || !strcmp(test, "overlap")) {
        assert(rc < 0);
        if (!supported) assert(rc == -ENOTSUP);
        if (!privileged) assert(rc == -EPERM);
        esp_elf_deinit(&image); assert(!live);
        assert(!image.internal_v1.tag && !image.internal_v1.text_owner && !image.internal_v1.data_owner);
        if (!privileged || !supported || !strcmp(test, "overlap")) assert(!allocations);
        goto done;
    }
    assert(!rc && external_text == 1 && external_data >= 1);
    assert((uintptr_t)image.entry == (!strcmp(test, "internal-entry") ? image.internal_v1.text_code : image.sec[ELF_SEC_TEXT].addr));
    const uintptr_t text_virtual = f.section[4].addr;
    assert(esp_elf_map_span_v1(&image, text_virtual, 4) == image.sec[ELF_SEC_TEXT].addr);
    assert(esp_elf_map_span_v1(&image, text_virtual + 12, 4) == image.sec[ELF_SEC_TEXT].addr + 12);
    assert(!esp_elf_map_span_v1(&image, text_virtual + 13, 4) && !esp_elf_map_span_v1(&image, text_virtual + 16, 1));
    assert(image.num == (legacy ? 1 : 2));
    if (!strcmp(test, "repeat-preserves-custody")) {
        const esp_elf_t before = image; const unsigned count = live, attempts = allocations;
        assert(esp_elf_relocate(&image, (const uint8_t *)&f) == -EINVAL);
        assert(live == count && allocations == attempts && !memcmp(&before, &image, sizeof(image)));
    }
    if (legacy) assert(!internal_text && !internal_data && !image.internal_v1.tag);
    else {
        assert(internal_text == 1 && internal_data == 1 && image.internal_v1.tag == ESP_ELF_INTERNAL_TAG_V1);
        assert(*(uint32_t *)image.internal_v1.sec[0].addr == 0x01020304);
        assert(*(uint32_t *)image.internal_v1.sec[1].addr == 0x05060708);
        assert(*(uint32_t *)image.internal_v1.sec[2].addr == 0x11223344);
        for (size_t n = 0; n < 16; ++n) assert(!((unsigned char *)image.internal_v1.sec[3].addr)[n]);
        assert(image.symtab[1].addr == (void *)image.internal_v1.text_code);
        elf32_rela_t r = {.offset = 0x110c, .info = ELF_R_INFO(0, 3)}; elf32_sym_t sym = {0};
        assert(!esp_elf_arch_relocate(&image, &r, &sym, 0x778899aa));
        assert(*(uint32_t *)(image.internal_v1.sec[1].addr + 12) == 0x778899aa);
        r.offset = 0x1101; assert(esp_elf_arch_relocate(&image, &r, &sym, 0) == -EINVAL);
        r.offset = 0x110d; assert(esp_elf_arch_relocate(&image, &r, &sym, 0) == -EINVAL);
        r.offset = 0x1110; assert(esp_elf_arch_relocate(&image, &r, &sym, 0) == -EINVAL);
        elf32_sym_t bad = f.symbol[2]; bad.shndx = 8;
        assert(!elf_function_export_addr(&image, &bad, f.section, f.header.shnum, f.names));
        bad = f.symbol[2]; bad.value = f.section[7].addr + 15; bad.size = 2;
        assert(!elf_function_export_addr(&image, &bad, f.section, f.header.shnum, f.names));
        bad = f.symbol[2]; bad.shndx = 0xfff1;
        assert(!elf_function_export_addr(&image, &bad, f.section, f.header.shnum, f.names));
        bad = f.symbol[2]; bad.size = 0;
        assert(elf_function_export_addr(&image, &bad, f.section, f.header.shnum, f.names) == image.internal_v1.text_code);
    }
    esp_elf_deinit(&image); assert(!live);
done:
    printf("PASS %s\n", test); return 0;
}
