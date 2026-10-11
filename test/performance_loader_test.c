/* Production esp_elf.c/validator test. Only RTOS, allocator, architecture and
 * privilege scope operations are substituted; no Xtensa instructions execute. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "esp_elf.h"
#include "private/elf_platform.h"
#include "freertos/task.h"

#if !TEST_ABSENT_HOOK
static struct { uint32_t phase, value, tick; } events[32];
#endif
static unsigned event_count, allocations, relocations, publications, delays;
static TickType_t ticks;
static bool fail_read, fail_allocation, allow_enter = true, allow_leave = true;
static int arch_result, publish_result;
static unsigned delay_step = 1;
static unsigned read_step, read_calls;
static size_t since_yield, max_between_yields;
#if !TEST_ABSENT_HOOK
void risc_perf_loader_event(uint32_t phase, uint32_t value) {
    assert(event_count < 32);
    events[event_count].phase = phase;
    events[event_count].value = value;
    events[event_count++].tick = ticks;
}
#endif
TickType_t xTaskGetTickCount(void) { return ticks; }
void vTaskDelay(TickType_t n) {
    ticks += n * delay_step; ++delays;
    if(since_yield>max_between_yields)max_between_yields=since_yield;
    since_yield=0;
}
ssize_t risc_test_read(int fd, void *bytes, size_t n) {
    if (fail_read) return 0;
    const ssize_t result=read(fd, bytes, n);++read_calls;ticks+=read_step;
    if(result>0)since_yield+=(size_t)result;
    return result;
}
void *esp_elf_malloc(uint32_t n, bool exec) {
    (void)exec;
    if (fail_allocation) return NULL;
    void *p = malloc(n);
    if (p) ++allocations;
    return p;
}
void esp_elf_free(void *p) { if (p) { assert(allocations); --allocations; free(p); } }
void esp_elf_registered_symbol_used(const void *table, const char *name, uintptr_t addr) {
    (void)table; (void)name; (void)addr;
}
uintptr_t elf_find_sym_default(const char *name) { (void)name; return 1; }
bool esp_elf_privileged_os_cpu_scope_owned_v1(void) { return false; }
bool esp_elf_privileged_os_cpu_relocation_enter_v1(const void *module) {
    (void)module; return allow_enter;
}
bool esp_elf_privileged_os_cpu_relocation_leave_v1(const void *module) {
    (void)module; return allow_leave;
}
int esp_elf_arch_relocate(esp_elf_t *elf, const elf32_rela_t *rela,
                         const elf32_sym_t *sym, uint32_t addr) {
    (void)elf; (void)rela; (void)sym; (void)addr;
    ++relocations; ++ticks; return arch_result;
}
int esp_elf_arch_flush(esp_elf_t *elf) {
    (void)elf; ++publications; ticks += 3; return publish_result;
}
static void reset(void) {
    event_count = 0; ticks = 0; delays = 0;read_calls=0;
    since_yield=max_between_yields=0;
}
static void expect(const uint32_t *phases, unsigned n) {
#if TEST_ABSENT_HOOK
    (void)phases; (void)n; assert(event_count == 0);
#else
    assert(event_count == n);
    for (unsigned i = 0; i < n; ++i) {
        assert(events[i].phase == phases[i]);
        if (i) assert(events[i].tick >= events[i-1].tick);
    }
#endif
}
#define EXPECT(...) do { const uint32_t p[] = {__VA_ARGS__}; expect(p, sizeof(p)/sizeof(p[0])); } while (0)
static void expect_relocation(elf_file_t *file, int expected) {
    esp_elf_t elf;
    assert(esp_elf_init(&elf) == 0);
    reset();
    assert(esp_elf_relocate(&elf, file->payload) == expected);
    if (expected) { EXPECT(42, 43, 44); } else { EXPECT(42, 43); }
#if !TEST_ABSENT_HOOK
    assert(events[1].value == (uint32_t)expected);
    if (expected) assert(events[2].value == (uint32_t)expected);
#endif
    esp_elf_deinit(&elf);
    assert(allocations == 1); /* Original input file remains owned. */
}
int main(int argc, char **argv) {
    assert(argc == 4);
    elf_file_t file = {0};
    reset();
    assert(esp_elf_open(&file, argv[1]) == 0);
    EXPECT(40, 41, 45, 46);
    assert(delays == file.size / (32 * 1024));
#if !TEST_ABSENT_HOOK
    assert(events[1].value == file.size);
    assert(events[1].tick - events[0].tick == delays);
#endif
    expect_relocation(&file, 0);
    assert(relocations && publications == 1);
#if !TEST_ABSENT_HOOK
    assert(events[1].tick > events[0].tick); /* Work lies inside boundary. */
#endif
    arch_result = -ENOSYS;
    expect_relocation(&file, -ENOSYS);
    arch_result = 0;
    publish_result = -EIO;
    expect_relocation(&file, -EIO);
    publish_result = 0;
    allow_enter = false;
    expect_relocation(&file, -EPERM);
    allow_enter = true;
    allow_leave = false;
    expect_relocation(&file, -EIO);
    allow_leave = true;
    fail_allocation = true;
    expect_relocation(&file, -ENOMEM);
    fail_allocation = false;
    esp_elf_close(&file);
    assert(allocations == 0);

    reset();
    assert(esp_elf_open(&file, "/nonexistent/riscrte-performance-test.elf") == -1);
    assert(errno == ENOENT);
    EXPECT(40, 44);
    reset(); fail_read = true;
    assert(esp_elf_open(&file, argv[1]) == -1);
    EXPECT(40, 44);
    assert(allocations == 0);
    reset(); fail_read = false; delay_step = 30000;
    assert(esp_elf_open(&file, argv[3]) == -1);
    assert(errno == ETIMEDOUT);
    EXPECT(40, 44);
    assert(allocations == 0);
    reset();delay_step=1;read_step=30000;
    assert(esp_elf_open(&file,argv[1])==-1 && errno==ETIMEDOUT);
    assert(read_calls==1 && allocations==0);EXPECT(40,44);
    read_step=0;
    reset(); delay_step = 1;
    assert(esp_elf_open(&file, argv[2]) == -1);
    EXPECT(40, 41, 45, 44);
    assert(allocations == 0);
    reset();
    assert(esp_elf_relocate(NULL, NULL) == -EINVAL);
    EXPECT(42, 43, 44);
#if TEST_ABSENT_HOOK
    puts("PASS: production loader reads/relocates/fails safely with absent weak trace hook");
#else
    puts("PASS: production loader phase order, bytes, read/yield timing, parse rejection, short read, timeout, relocation/allocation/scope/publication failures");
#endif
    // A 256 KiB valid image: immediate reads yield at 32 KiB; 1 ms/read
    // yields every two chunks; slower 3 ms/read yields after each chunk.
    for(unsigned cost=0;cost<=3;++cost){
        if(cost==2)continue;
        reset();read_step=cost;
        assert(!esp_elf_open(&file,argv[3]));
        assert(file.size==256*1024 && read_calls==64);
        const unsigned expected=cost==0?8:cost==1?32:64;
        assert(delays==expected && max_between_yields<=32*1024 && since_yield==0);
        EXPECT(40,41,45,46);
        printf("Cold 256 KiB read cost=%u ticks/chunk: reads=%u forced_yields=%u max_bytes_between_yields=%zu\n",
               cost,read_calls,delays,max_between_yields);
        esp_elf_close(&file);assert(!allocations);
    }
    reset();read_step=1;ticks=UINT32_MAX-2;
    assert(!esp_elf_open(&file,argv[3]) && delays==32 && max_between_yields==8192);
    esp_elf_close(&file);assert(!allocations);read_step=0;
    puts("PASS: cold-reader byte/time checkpoints, slow-I/O/yield deadline and tick wrap");
    return 0;
}
