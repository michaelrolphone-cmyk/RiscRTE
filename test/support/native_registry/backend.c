#define _POSIX_C_SOURCE 200809L
/* Execute host fixtures behind the UNMODIFIED target dlfcn/dlmod registry.
 * Only ELF reading/relocation/allocation are substituted. This does not emulate
 * Xtensa instructions, target memory/cache behavior, peripherals or import policy. */
#include "backend.h"
#include <esp_elf.h>
#include <dlfcn.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct Mapping {
    esp_elf_t* elf;
    void* host;
    char* path;
    struct Mapping* next;
};
struct Image { size_t size; char path[256]; uint8_t bytes[]; };
static struct Mapping* mappings;
static size_t mapping_count, relocation_count, image_count, read_count, read_bytes;
static unsigned fail_relocations;
static const char* const* extra_symbols;
static size_t extra_count;
static const char* const base_symbols[] = {
    "t5_driver_get", "app_main", "app_module_init", "app_module_fini"
};
__attribute__((weak)) void risc_test_native_loading(const char* path) { (void)path; }
__attribute__((weak)) void risc_test_native_loaded(const char* path, void* host) { (void)path; (void)host; }
__attribute__((weak)) void risc_test_native_unloading(const char* path, void* host) { (void)path; (void)host; }
size_t risc_test_native_mapping_count(void) { return mapping_count; }
size_t risc_test_native_relocation_count(void) { return relocation_count; }
size_t risc_test_native_image_count(void) { return image_count; }
size_t risc_test_native_read_count(void) { return read_count; }
size_t risc_test_native_read_bytes(void) { return read_bytes; }
void risc_test_native_fail_relocations(unsigned count) { fail_relocations = count; }
void risc_test_native_extra_symbols(const char* const* names, size_t count) {
    assert(!mapping_count && count <= 128 && (!count || names));
    extra_symbols = names;
    extra_count = count;
}
void* esp_elf_malloc(uint32_t size, bool executable) { (void)executable; return calloc(1, size); }
void esp_elf_free(void* memory) { free(memory); }
int esp_elf_open(elf_file_t* file, const char* path) {
    if (!file || !path || strlen(path)>=sizeof(((struct Image*)0)->path)) return -1;
    FILE* input=fopen(path,"rb");if(!input)return -1;
    long size=-1;
    if(!fseek(input,0,SEEK_END))size=ftell(input);
    if(size<=0 || size>8*1024*1024 || fseek(input,0,SEEK_SET)){fclose(input);return -1;}
    struct Image* image=malloc(sizeof(*image)+(size_t)size);
    if(!image){fclose(input);errno=ENOMEM;return -1;}
    const size_t n=fread(image->bytes,1,(size_t)size,input);++read_count;read_bytes+=n;
    const bool good=n==(size_t)size && !ferror(input);
    const int closed=fclose(input);
    if(!good || closed){free(image);return -1;}
    image->size=(size_t)size;strcpy(image->path,path);
    file->payload=(uint8_t*)image;file->size=size;++image_count;return 0;
}
void esp_elf_close(elf_file_t* file) {
    if(file->payload){assert(image_count);--image_count;}
    free(file->payload); file->payload = NULL; file->size = 0;
}
int esp_elf_init(esp_elf_t* elf) { memset(elf, 0, sizeof(*elf)); return 0; }

/* A distinct inode makes the host loader create fresh globals even for repeated
 * mappings of one source file. The production registry still chooses whether
 * the mapping is admitted; a basename collision never reaches this backend. */
static void* fresh_host_mapping(const struct Image* image) {
    char temporary[] = "/tmp/riscrte-registry-XXXXXX";
    int fd = mkstemp(temporary);
    if (fd < 0) return NULL;
    FILE* output = fdopen(fd, "wb");
    bool ok = output != NULL;
    if (output) {
        if(fwrite(image->bytes,1,image->size,output)!=image->size)ok=false;
        if (fclose(output)) ok = false;
    } else { close(fd); }
    void* handle = ok ? dlopen(temporary, RTLD_NOW | RTLD_LOCAL) : NULL;
    unlink(temporary);
    return handle;
}
int esp_elf_relocate(esp_elf_t* elf, const uint8_t* bytes) {
    ++relocation_count;
    if (fail_relocations) { --fail_relocations; return -EIO; }
    const struct Image* image=(const struct Image*)bytes;
    const char* path = image->path;
    risc_test_native_loading(path);
    void* host = fresh_host_mapping(image);
    if (!host) return -ENOEXEC;
    struct Mapping* mapping = calloc(1, sizeof(*mapping));
    const size_t capacity = sizeof(base_symbols)/sizeof(*base_symbols) + extra_count;
    elf->symtab = calloc(capacity, sizeof(*elf->symtab));
    if (!mapping || !elf->symtab) {
        free(mapping); free(elf->symtab); elf->symtab = NULL; dlclose(host); return -ENOMEM;
    }
    mapping->path = strdup(path);
    if (!mapping->path) {
        free(mapping); free(elf->symtab); elf->symtab = NULL; dlclose(host); return -ENOMEM;
    }
    for (size_t i = 0; i < capacity; ++i) {
        const char* name = i < sizeof(base_symbols)/sizeof(*base_symbols) ? base_symbols[i] :
            extra_symbols[i - sizeof(base_symbols)/sizeof(*base_symbols)];
        void* symbol = dlsym(host, name);
        if (!symbol) continue;
        elf->symtab[elf->num].name = strdup(name);
        assert(elf->symtab[elf->num].name);
        elf->symtab[elf->num++].addr = symbol;
    }
    mapping->elf = elf; mapping->host = host;
    mapping->next = mappings; mappings = mapping; ++mapping_count;
    risc_test_native_loaded(path, host);
    return 0;
}
void esp_elf_deinit(esp_elf_t* elf) {
    struct Mapping** cursor = &mappings;
    while (*cursor && (*cursor)->elf != elf) cursor = &(*cursor)->next;
    if (*cursor) {
        struct Mapping* mapping = *cursor;
        risc_test_native_unloading(mapping->path, mapping->host);
        assert(dlclose(mapping->host) == 0);
        *cursor = mapping->next; free(mapping->path); free(mapping); --mapping_count;
    }
    for (unsigned i = 0; i < elf->num; ++i) free((void*)elf->symtab[i].name);
    free(elf->symtab); memset(elf, 0, sizeof(*elf));
}
