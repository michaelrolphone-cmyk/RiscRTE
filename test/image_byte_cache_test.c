/* Real file reader, validator, relocator and module registry. Architecture
 * relocation, cache publication, allocator and RTOS are host substitutions. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "esp_elf.h"
#include "esp_dlfcn.h"
#include "private/esp_dlmod.h"
#include "private/elf_platform.h"
#include "freertos/task.h"

static unsigned opens, seeks, reads, parses, relocations, publications, allocations, yields;
static size_t bytes;
static TickType_t ticks;
static int fail_allocation_at=-1, arch_result;
static bool forbid_reads;
static struct {void *pointer; size_t size;} owned[256];
static size_t live_bytes;
void risc_perf_loader_event(uint32_t phase, uint32_t value) {
    (void)value;
    if (phase == 45) ++parses;
    if (phase == 42) ++relocations;
}
TickType_t xTaskGetTickCount(void) { return ticks; }
void vTaskDelay(TickType_t n) { ticks += n; ++yields; }
ssize_t risc_test_read(int fd, void *data, size_t n) {
    assert(!forbid_reads);
    const ssize_t result = read(fd, data, n);
    ++reads;
    if (result > 0) bytes += result;
    return result;
}
int risc_test_open(const char *path, int flags, ...) { ++opens; return open(path, flags); }
off_t risc_test_lseek(int fd, off_t offset, int how) { ++seeks; return lseek(fd,offset,how); }
void *esp_elf_malloc(uint32_t n, bool exec) {
    (void)exec;
    if (fail_allocation_at == 0) { fail_allocation_at=-1; return NULL; }
    if (fail_allocation_at > 0) --fail_allocation_at;
    void *p = malloc(n);
    if (p) {
        assert(allocations < sizeof(owned)/sizeof(owned[0]));
        owned[allocations].pointer=p;owned[allocations].size=n;
        ++allocations;live_bytes+=n;
    }
    return p;
}
void esp_elf_free(void *p) {
    if (!p) return;
    unsigned i=0;while(i<allocations && owned[i].pointer!=p)++i;
    assert(i<allocations);live_bytes-=owned[i].size;owned[i]=owned[--allocations];free(p);
}
void esp_elf_registered_symbol_used(const void *table, const char *name, uintptr_t addr) {
    (void)table; (void)name; (void)addr;
}
uintptr_t elf_find_sym_default(const char *name) { (void)name; return 1; }
bool esp_elf_privileged_os_cpu_scope_owned_v1(void) { return false; }
bool esp_elf_privileged_os_cpu_relocation_enter_v1(const void *module) { (void)module; return true; }
bool esp_elf_privileged_os_cpu_relocation_leave_v1(const void *module) { (void)module; return true; }
int esp_elf_arch_relocate(esp_elf_t *elf, const elf32_rela_t *rela,
                         const elf32_sym_t *sym, uint32_t addr) {
    (void)elf; (void)rela; (void)sym; (void)addr;
    const int result=arch_result;arch_result=0;return result;
}
int esp_elf_arch_flush(esp_elf_t *elf) { (void)elf; ++publications; return 0; }
static void reset_counts(void) { opens=seeks=reads=parses=relocations=publications=yields=0;bytes=0; }
static void load(esp_dl_image_cache **cache, const char *path) {
    void *module=esp_dlopen_cached_instance(cache,path);
    assert(module && dlsym(module,"app_main"));
    assert(dlclose(module)==0);
}
static void report(const char *mode) {
    printf("%s default/app/default/app/default: opens=%u seeks=%u reads=%u bytes=%zu parses=%u relocations=%u publications=%u read_yields=%u\n",
           mode,opens,seeks,reads,bytes,parses,relocations,publications,yields);
}
int main(int argc, char **argv) {
    assert(argc == 5);
    const char *paths[] = {argv[1], argv[2], argv[1], argv[2], argv[1]};
    for (unsigned i = 0; i < sizeof(paths)/sizeof(paths[0]); ++i) {
        void *module = esp_dlopen_instance(paths[i]);
        assert(module && dlsym(module, "app_main"));
        assert(dlclose(module) == 0);
        assert(allocations == 0);
    }
    assert(parses == 5 && relocations == 5 && publications == 5);
    report("baseline");
#ifdef TEST_NO_PSRAM
    assert(!esp_dl_image_cache_create() && !allocations);
    load(NULL,argv[1]);assert(!allocations);
    puts("PASS: non-PSRAM target refuses cache allocation and preserves ordinary loading");
    return 0;
#endif
    reset_counts();
    esp_dl_image_cache *cache=esp_dl_image_cache_create();assert(cache);
    const size_t overhead=live_bytes;
    for (unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);++i) {
        forbid_reads=i>=2;load(&cache,paths[i]);
        assert(allocations==1+(i?2:1));
    }
    forbid_reads=false;
    assert(opens==2 && seeks==4 && parses==2 && relocations==5 && publications==5);
    report("cached");
#ifdef TEST_COUNTS_ONLY
    esp_dl_image_cache_destroy(cache);assert(!allocations);return 0;
#endif

    // A live independent mapping has no dependency on cache custody. Mutating
    // its data cannot mutate the immutable bytes used for subsequent mappings.
    struct dlmod_slist_t *first=esp_dlopen_cached_instance(&cache,argv[4]);assert(first);
    assert(first->elf->sec[ELF_SEC_BSS].size>=64 && first->elf->sec[ELF_SEC_DATA].size>=4);
    assert(*(uint32_t *)first->elf->sec[ELF_SEC_DATA].addr==0x12345678);
    memset((void *)first->elf->sec[ELF_SEC_BSS].addr,0xa5,first->elf->sec[ELF_SEC_BSS].size);
    memset((void *)first->elf->sec[ELF_SEC_DATA].addr,0xff,first->elf->sec[ELF_SEC_DATA].size);
    forbid_reads=true;
    struct dlmod_slist_t *second=esp_dlopen_cached_instance(&cache,argv[4]);assert(second && second!=first);
    forbid_reads=false;
    assert(first->elf->pdata!=second->elf->pdata && first->elf->ptext!=second->elf->ptext);
    assert(*(uint32_t *)second->elf->sec[ELF_SEC_DATA].addr==0x12345678);
    for(size_t i=0;i<second->elf->sec[ELF_SEC_BSS].size;++i)
        assert(((uint8_t *)second->elf->sec[ELF_SEC_BSS].addr)[i]==0);
    esp_dl_image_cache_destroy(cache);
    assert(dlsym(first,"app_main") && dlmod_validate_handle(first));
    assert(dlsym(second,"app_module_init") && dlsym(second,"app_module_fini"));
    assert(dlclose(second)==0);
    assert(dlclose(first)==0 && allocations==0);

    // New store session at the identical mount/path never inherits byte entries.
    reset_counts();cache=esp_dl_image_cache_create();assert(cache);load(&cache,argv[1]);
    assert(opens==1 && parses==1);esp_dl_image_cache_destroy(cache);assert(!allocations);

    // Cache allocation failure preserves the ordinary loader.
    fail_allocation_at=0;assert(!esp_dl_image_cache_create());load(NULL,argv[1]);assert(!allocations);
    cache=esp_dl_image_cache_create();assert(cache);
    fail_allocation_at=0;load(&cache,argv[1]);assert(!cache && !allocations);

    // Every loader allocation site can fail without leaking an input/mapping.
    // Failures with reclaimable cached inputs fall back once and disable reuse.
    for(int fail=0;fail<12;++fail) {
        cache=esp_dl_image_cache_create();assert(cache);load(&cache,argv[1]);
        fail_allocation_at=fail;
        void *module=esp_dlopen_cached_instance(&cache,argv[2]);
        fail_allocation_at=-1;
        if(module)assert(!dlclose(module));
        esp_dl_image_cache_destroy(cache);assert(!allocations && !live_bytes);
    }
    cache=esp_dl_image_cache_create();load(&cache,argv[1]);load(&cache,argv[2]);
    arch_result=-ENOMEM;load(&cache,argv[1]);assert(!cache && allocations==0);
    reset_counts();load(&cache,argv[1]);load(&cache,argv[1]);assert(parses==2);
    esp_dl_image_cache_destroy(cache);assert(!allocations);
    cache=esp_dl_image_cache_create();load(&cache,argv[1]);
    arch_result=-EIO;assert(!esp_dlopen_cached_instance(&cache,argv[1]));
    assert(allocations==1);load(&cache,argv[1]);esp_dl_image_cache_destroy(cache);assert(!allocations);

    // Distinct exact paths, including identical basenames, occupy distinct
    // entries. The fifth image evicts the least recently used of four.
    cache=esp_dl_image_cache_create();
    char path[256];
    for(unsigned i=0;i<5;++i){snprintf(path,sizeof(path),"%s/%u/default.elf",argv[3],i);load(&cache,path);}
    assert(allocations==1+ESP_DL_IMAGE_CACHE_ENTRIES);
    reset_counts();snprintf(path,sizeof(path),"%s/0/default.elf",argv[3]);load(&cache,path);assert(parses==1);
    esp_dl_image_cache_destroy(cache);assert(!allocations);

    // A 1 MiB byte budget is independent of entry count. Oversized images are
    // still loaded normally but never retained; failures cannot add entries.
    cache=esp_dl_image_cache_create();
    for(unsigned i=0;i<2;++i){snprintf(path,sizeof(path),"%s/large%u.elf",argv[3],i);load(&cache,path);assert(live_bytes<=overhead+ESP_DL_IMAGE_CACHE_BYTES);}
    assert(allocations==2);
    snprintf(path,sizeof(path),"%s/oversize.elf",argv[3]);reset_counts();load(&cache,path);load(&cache,path);
    assert(parses==2 && live_bytes<=overhead+ESP_DL_IMAGE_CACHE_BYTES);
    snprintf(path,sizeof(path),"%s/malformed.elf",argv[3]);
    reset_counts();errno=ENOMEM;
    assert(!esp_dlopen_cached_instance(&cache,path));
    assert(opens==1 && cache && allocations==2); /* No stale-errno retry/eviction. */
    assert(!esp_dlopen_cached_instance(&cache,"/nonexistent/image.elf"));
    esp_dl_image_cache_destroy(cache);assert(!allocations && !live_bytes);
    puts("PASS: exact-path/session custody, independent mappings, allocation fallback, failed relocation, bounded LRU entries/bytes, malformed/missing and oversized images");
}
