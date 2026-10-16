/* Production registry, cache, structural validation and section/symbol mapping.
 * Only allocator/RTOS/architecture operations are host substitutes. The reader
 * supplies synthetic valid Xtensa ELF bytes; no target compiler is required and
 * no mapped instructions execute. Any POSIX image open is a test failure. */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_elf.h"
#include "esp_dlfcn.h"
#include "private/esp_dlmod.h"
#include "private/elf_platform.h"
#include "freertos/task.h"

static struct { void *pointer; size_t size; bool image; } owned[256];
static unsigned allocations, live_images, relocation_calls, publications, validates;
static size_t live_bytes;
static int fail_allocation_at = -1;
static unsigned fail_allocation_count;
static int publication_error;
static unsigned publication_failures;
static const uint8_t *relocation_input;
static const uint8_t *expected_input;
static TickType_t ticks;

static unsigned allocation_index(const void *pointer) {
    unsigned i = 0;
    while (i < allocations && owned[i].pointer != pointer) ++i;
    return i;
}
void *esp_elf_malloc(uint32_t size, bool executable) {
    (void)executable;
    if (fail_allocation_at == 0) {
        assert(fail_allocation_count);
        if (!--fail_allocation_count) fail_allocation_at = -1;
        return NULL;
    }
    if (fail_allocation_at > 0) --fail_allocation_at;
    void *result = malloc(size);
    if (result) {
        assert(allocations < sizeof(owned) / sizeof(*owned));
        owned[allocations].pointer = result;
        owned[allocations].size = size;
        owned[allocations++].image = false;
        live_bytes += size;
    }
    return result;
}
void esp_elf_free(void *pointer) {
    if (!pointer) return;
    const unsigned i = allocation_index(pointer);
    assert(i < allocations); /* Detect duplicate frees, including reader failures. */
    if (owned[i].image) { assert(live_images); --live_images; }
    live_bytes -= owned[i].size;
    owned[i] = owned[--allocations];
    free(pointer);
}
TickType_t xTaskGetTickCount(void) { return ticks; }
void vTaskDelay(TickType_t amount) { ticks += amount; }
int risc_test_forbidden_open(const char *path, int flags, ...) {
    (void)path; (void)flags;
    assert(!"Explicit reader escaped to POSIX open");
    errno = EIO;
    return -1;
}
void esp_elf_registered_symbol_used(const void *table, const char *name, uintptr_t addr) {
    (void)table; (void)name; (void)addr;
}
uintptr_t elf_find_sym_default(const char *name) { (void)name; return 1; }
bool esp_elf_privileged_os_cpu_scope_owned_v1(void) { return false; }
bool esp_elf_privileged_os_cpu_relocation_enter_v1(const void *module) { (void)module; return true; }
bool esp_elf_privileged_os_cpu_relocation_leave_v1(const void *module) { (void)module; return true; }
int esp_elf_arch_relocate(esp_elf_t *elf, const elf32_rela_t *rela,
                          const elf32_sym_t *sym, uint32_t address) {
    (void)elf; (void)rela; (void)sym; (void)address;
    return 0;
}
int esp_elf_arch_flush(esp_elf_t *elf) {
    (void)elf;
    ++publications;
    if (publication_failures) { --publication_failures; return publication_error; }
    return 0;
}
int risc_test_real_relocate(esp_elf_t *elf, const uint8_t *bytes);
int esp_elf_relocate(esp_elf_t *elf, const uint8_t *bytes) {
    const unsigned index = allocation_index(bytes);
    assert(index < allocations && owned[index].image);
    if (expected_input) assert(bytes == expected_input);
    relocation_input = bytes;
    ++relocation_calls;
    return risc_test_real_relocate(elf, bytes);
}

static const char section_names[] = "\0.text\0.data\0.bss\0.dynsym\0.dynstr\0.shstrtab";
struct TestImage {
    elf32_hdr_t header;
    elf32_shdr_t sections[7];
    uint32_t text, data;
    elf32_sym_t symbols[2];
    char dynstr[10];
    char shstrtab[sizeof(section_names)];
};
static struct TestImage fixture;
extern bool esp_elf_validate_file(const uint8_t *, size_t);
static unsigned name_offset(const char *name) {
    for (size_t i = 1; i < sizeof(section_names); i += strlen(section_names + i) + 1)
        if (!strcmp(section_names + i, name)) return (unsigned)i;
    assert(!"Missing fixture section name");
    return 0;
}
static void prepare_fixture(void) {
    elf32_hdr_t *h = &fixture.header;
    memcpy(h->ident, "\177ELF\1\1\1", 7);
    h->type = 3; h->machine = 94; h->version = 1; h->entry = 0x1000;
    h->ehsize = sizeof(*h); h->shoff = offsetof(struct TestImage, sections);
    h->shentsize = sizeof(elf32_shdr_t); h->shnum = 7; h->shstrndx = 6;
    const char *names[] = {"", ".text", ".data", ".bss", ".dynsym", ".dynstr", ".shstrtab"};
    for (unsigned i = 1; i < 7; ++i) fixture.sections[i].name = name_offset(names[i]);
    elf32_shdr_t *s = fixture.sections;
    s[1].type = SHT_PROGBITS; s[1].flags = SHF_ALLOC | SHF_EXECINSTR;
    s[1].addr = 0x1000; s[1].offset = offsetof(struct TestImage, text); s[1].size = 4; s[1].addralign = 4;
    s[2].type = SHT_PROGBITS; s[2].flags = SHF_ALLOC | SHF_WRITE;
    s[2].addr = 0x2000; s[2].offset = offsetof(struct TestImage, data); s[2].size = 4; s[2].addralign = 4;
    s[3].type = SHT_NOBITS; s[3].flags = SHF_ALLOC | SHF_WRITE;
    s[3].addr = 0x2004; s[3].size = 64; s[3].addralign = 4;
    s[4].type = SHT_SYNSYM; s[4].offset = offsetof(struct TestImage, symbols);
    s[4].size = sizeof(fixture.symbols); s[4].link = 5; s[4].entsize = sizeof(elf32_sym_t);
    s[5].type = SHT_STRTAB; s[5].offset = offsetof(struct TestImage, dynstr); s[5].size = sizeof(fixture.dynstr);
    s[6].type = SHT_STRTAB; s[6].offset = offsetof(struct TestImage, shstrtab); s[6].size = sizeof(fixture.shstrtab);
    fixture.data = 0x12345678;
    fixture.symbols[1].name = 1; fixture.symbols[1].value = 0x1000; fixture.symbols[1].size = 4;
    fixture.symbols[1].info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC); fixture.symbols[1].shndx = 1;
    memcpy(fixture.dynstr, "\0app_main", sizeof("\0app_main"));
    memcpy(fixture.shstrtab, section_names, sizeof(section_names));
    assert(esp_elf_validate_file((const uint8_t *)&fixture, sizeof(fixture)));
}

struct Reader {
    unsigned calls, failures;
    int error; /* -1 means leave errno untouched. */
    bool forbid, dirty_failure, malformed;
    size_t size;
    const char *expected_path;
    uint8_t *last_input;
};
static struct Reader reader_state(void) {
    struct Reader result = {0};
    result.size = sizeof(fixture);
    return result;
}
static int read_image(void *context, const char *path, elf_file_t *out) {
    struct Reader *r = context;
    assert(r && !r->forbid && !out->payload && !out->size);
    assert(errno == 0);
    if (r->expected_path) assert(!strcmp(path, r->expected_path));
    ++r->calls;
    if (r->failures) {
        --r->failures;
        if (r->dirty_failure) {
            out->payload = esp_elf_malloc(r->size, false);
            assert(out->payload);
            out->size = r->size;
            esp_elf_free(out->payload); /* Reader cleans failure; output is discarded. */
        }
        if (r->error >= 0) errno = r->error;
        return -1;
    }
    uint8_t *bytes = esp_elf_malloc(r->size, false);
    if (!bytes) { errno = ENOMEM; return -1; }
    memset(bytes, 0, r->size);
    memcpy(bytes, &fixture, sizeof(fixture));
    if (r->malformed) bytes[0] = 0;
    ++validates;
    if (!esp_elf_validate_file(bytes, r->size)) {
        esp_elf_free(bytes);
        errno = ENOEXEC;
        return -1;
    }
    const unsigned i = allocation_index(bytes);
    assert(i < allocations);
    owned[i].image = true; ++live_images;
    r->last_input = bytes;
    out->payload = bytes; out->size = r->size;
    return 0;
}
static struct dlmod_slist_t *open_reader(esp_dl_image_cache **cache, const char *path, struct Reader *r) {
    r->expected_path = path;
    return esp_dlopen_cached_reader_instance(cache, path, read_image, r);
}
static void load(esp_dl_image_cache **cache, const char *path, struct Reader *r) {
    struct dlmod_slist_t *module = open_reader(cache, path, r);
    assert(module && dlsym(module, "app_main"));
    assert(!dlclose(module));
}
static void assert_empty(void) {
    assert(!allocations && !live_bytes && !live_images);
    assert(!dlmod_gethandle("default"));
}
static void finish(esp_dl_image_cache *cache) {
    esp_dl_image_cache_destroy(cache);
    expected_input = NULL;
    assert_empty();
}

int main(void) {
    prepare_fixture();
    struct Reader r = reader_state();
    esp_dl_image_cache *cache = esp_dl_image_cache_create();
#ifdef TEST_NO_PSRAM
    assert(!cache && !allocations);
    load(&cache, "/provider/default.elf", &r);
    load(&cache, "/provider/default.elf", &r);
    assert(r.calls == 2 && validates == 2 && relocation_calls == 2);
    assert_empty();
    puts("PASS: no-PSRAM explicit reader loads remain uncached, with no POSIX escape");
    return 0;
#endif
    assert(cache);
    const size_t overhead = live_bytes;

    /* A cache hit must not call even a now-failing reader or touch storage. */
    struct dlmod_slist_t *first = open_reader(&cache, "/provider/default.elf", &r);
    assert(first && r.calls == 1 && live_images == 1 && validates == 1);
    expected_input = r.last_input;
    assert(relocation_input == expected_input);
    memset((void *)first->elf->sec[ELF_SEC_DATA].addr, 0xff, 4);
    memset((void *)first->elf->sec[ELF_SEC_BSS].addr, 0xa5, 64);
    r.forbid = true;
    struct dlmod_slist_t *second = open_reader(&cache, "/provider/default.elf", &r);
    assert(second && second != first && r.calls == 1 && validates == 1 && relocation_calls == 2);
    assert(second->elf->ptext != first->elf->ptext && second->elf->pdata != first->elf->pdata);
    assert(*(uint32_t *)second->elf->sec[ELF_SEC_DATA].addr == 0x12345678);
    for (unsigned i = 0; i < 64; ++i) assert(!((uint8_t *)second->elf->sec[ELF_SEC_BSS].addr)[i]);
    esp_dl_image_cache_destroy(cache); cache = NULL; expected_input = NULL;
    assert(!live_images && dlsym(first, "app_main") && dlsym(second, "app_main"));
    assert(!dlclose(first) && !dlclose(second)); assert_empty();

    /* Null/disabled cache and mutable snapshot API always use their reader. */
    r = reader_state(); load(NULL, "/provider/default.elf", &r);
    load(&cache, "/provider/default.elf", &r); assert(r.calls == 2); assert_empty();
    for (unsigned i = 0; i < 2; ++i) {
        void *module = esp_dlopen_reader_instance("/provider/default.elf", read_image, &r);
        assert(module && !dlclose(module)); assert_empty();
    }
    assert(r.calls == 4);
    assert(!esp_dlopen_cached_reader_instance(NULL, "/provider/default.elf", NULL, &r));
    assert(!esp_dlopen_cached_reader_instance(NULL, NULL, read_image, &r));
    assert(!esp_dlopen_cached_reader_instance(NULL, "/", read_image, &r));
    assert(r.calls == 4); assert_empty();

    /* Non-memory/malformed failures do not evict valid images or retry, even
     * with stale ENOMEM on entry. Failed output remains reader-owned. */
    cache = esp_dl_image_cache_create(); r = reader_state();
    load(&cache, "/provider/default.elf", &r);
    const size_t before_failure = live_bytes;
    const int errors[] = {EIO, ENOENT, -1};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(*errors); ++i) {
        const unsigned calls = r.calls;
        r.failures = 1; r.error = errors[i]; r.dirty_failure = true; errno = ENOMEM;
        assert(!open_reader(&cache, "/provider/missing.elf", &r));
        assert(cache && r.calls == calls + 1 && live_images == 1 && live_bytes == before_failure);
    }
    r.malformed = true;
    assert(!open_reader(&cache, "/provider/malformed.elf", &r));
    assert(cache && live_bytes == before_failure);
    r.malformed = false; r.forbid = true;
    load(&cache, "/provider/default.elf", &r);
    assert(!esp_dlopen_cached_reader_instance(&cache, "/provider/default.elf", NULL, &r));
    finish(cache);

    /* Reader ENOMEM reclaims once and retries the same context/path, with a
     * fresh output. Repeated failure remains terminal; no source substitution. */
    for (unsigned failures = 1; failures <= 2; ++failures) {
        cache = esp_dl_image_cache_create(); r = reader_state();
        load(&cache, "/provider/default.elf", &r);
        r.failures = failures; r.error = ENOMEM; r.dirty_failure = true;
        struct dlmod_slist_t *module = open_reader(&cache, "/provider/child.elf", &r);
        assert(!cache && r.calls == 3 && !live_images);
        if (failures == 1) { assert(module); assert(!dlclose(module)); }
        else assert(!module);
        assert_empty();
        r.failures = 1;
        assert(!open_reader(&cache, "/provider/child.elf", &r));
        assert(r.calls == 4); assert_empty();
    }

    /* Every production allocator site can reclaim once without leaking the
     * detached input or mappings. Both one-shot and persistent failures run. */
    for (unsigned count = 1; count <= 2; ++count) {
        for (int site = 0; site < 9; ++site) {
            cache = esp_dl_image_cache_create(); r = reader_state();
            load(&cache, "/provider/default.elf", &r);
            fail_allocation_at = site; fail_allocation_count = count;
            struct dlmod_slist_t *module = open_reader(&cache, "/provider/child.elf", &r);
            fail_allocation_at = -1; fail_allocation_count = 0;
            if (module) assert(!dlclose(module));
            finish(cache);
        }
    }

    /* On a hit, mapping-allocation reclamation cannot free in-use input. */
    cache = esp_dl_image_cache_create(); r = reader_state();
    load(&cache, "/provider/default.elf", &r);
    load(&cache, "/provider/child.elf", &r);
    r.expected_path = "/provider/default.elf";
    /* Retrieve the default once to identify its original detached pointer. */
    load(&cache, "/provider/default.elf", &r); expected_input = relocation_input;
    r.forbid = true;
    fail_allocation_at = 1; fail_allocation_count = 1; /* node succeeds, ELF fails */
    load(&cache, "/provider/default.elf", &r);
    assert(!cache && r.calls == 2); finish(cache);

    /* Relocation failures preserve source custody across retry, or discard
     * just the failed image on EIO. Export names outlive reclaimed bytes. */
    for (unsigned failures = 1; failures <= 2; ++failures) {
        cache = esp_dl_image_cache_create(); r = reader_state();
        load(&cache, "/provider/default.elf", &r); expected_input = r.last_input;
        r.forbid = true; publication_error = -ENOMEM; publication_failures = failures;
        const unsigned relocations_before = relocation_calls;
        struct dlmod_slist_t *module = open_reader(&cache, "/provider/default.elf", &r);
        assert(!cache && r.calls == 1 && relocation_calls == relocations_before + 2 && !live_images);
        if (failures == 1) { assert(module && dlsym(module, "app_main")); assert(!dlclose(module)); }
        else assert(!module);
        finish(cache);
    }
    cache = esp_dl_image_cache_create(); r = reader_state();
    load(&cache, "/provider/default.elf", &r);
    publication_error = -EIO; publication_failures = 1;
    assert(!open_reader(&cache, "/provider/default.elf", &r));
    assert(cache && !live_images && allocations == 1);
    load(&cache, "/provider/default.elf", &r); assert(r.calls == 2); finish(cache);

    /* Four exact paths/1 MiB remain bounded with zero-copy ownership. */
    cache = esp_dl_image_cache_create(); r = reader_state();
    for (unsigned i = 0; i < 5; ++i) {
        char path[64]; snprintf(path, sizeof(path), "/provider/%u/default.elf", i);
        load(&cache, path, &r);
    }
    assert(r.calls == 5 && live_images == ESP_DL_IMAGE_CACHE_ENTRIES && allocations == 5);
    load(&cache, "/provider/1/default.elf", &r); assert(r.calls == 5);
    load(&cache, "/provider/0/default.elf", &r); assert(r.calls == 6);
    load(&cache, "/provider/2/default.elf", &r); assert(r.calls == 7);
    finish(cache);
    cache = esp_dl_image_cache_create(); r = reader_state(); r.size = 600 * 1024;
    load(&cache, "/provider/large0.elf", &r); load(&cache, "/provider/large1.elf", &r);
    assert(live_images == 1 && live_bytes <= overhead + ESP_DL_IMAGE_CACHE_BYTES);
    r.size = ESP_DL_IMAGE_CACHE_BYTES + 1;
    load(&cache, "/provider/oversize.elf", &r); load(&cache, "/provider/oversize.elf", &r);
    assert(r.calls == 4 && live_images == 1 && live_bytes <= overhead + ESP_DL_IMAGE_CACHE_BYTES);
    finish(cache);
    puts("PASS: explicit reader hit/miss, no POSIX escape, zero-copy input custody, fresh mappings, reader/allocation/relocation retry cleanup, bounded cache");
    return 0;
}
