/* Production resolver, scope, ELF validation and relocation. The architecture
 * hook observes resolution and lifecycle; it never runs Xtensa instructions. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_elf.h"
#include "private/elf_platform.h"
#include "private/esp_privileged_elf.h"
#include "private/esp_privileged_os_cpu.h"
#include "freertos/task.h"
#include "private/esp_privileged_imports.h"
#include "private/esp_privileged_manifest_imports.h"

#if RISC_NATIVE_ESP_IDF
#define selected_supported esp_elf_privileged_selected_import_supported_v2
#define selected_diagnostic esp_elf_privileged_selected_import_supported_with_diagnostics_v2
#define begin_selected(i,n) esp_elf_privileged_os_cpu_begin_selected_diagnostics_v2(i,n,0)
#define lookup_selected esp_elf_privileged_os_cpu_lookup_v2
#define relocate_selected(e,b,n,i,c) esp_elf_relocate_privileged_selected_diagnostics_v2(e,b,n,i,c,0)
#else
#define selected_supported esp_elf_privileged_selected_import_supported_v1
#define selected_diagnostic esp_elf_privileged_selected_import_supported_with_diagnostics_v1
#define begin_selected esp_elf_privileged_os_cpu_begin_selected_v1
#define lookup_selected esp_elf_privileged_os_cpu_lookup_v1
#define relocate_selected esp_elf_relocate_privileged_selected_v1
#endif

static TaskHandle_t task = (TaskHandle_t)1;
static unsigned allocations, relocated, custom_calls;
static bool inject, fail_arch;
static const uint8_t* active_bytes;
static const char* const selected[] = {"memcpy", "xTaskGetTickCount"};
static const char* const empty[] = {NULL};
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return task; }
TickType_t xTaskGetTickCount(void) { return 1; }
void vTaskDelay(TickType_t n) { (void)n; }
void* esp_elf_malloc(uint32_t n, bool executable) {
    (void)executable; void* out=malloc(n); if(out)++allocations; return out;
}
void esp_elf_free(void* p) { if(p){assert(allocations);--allocations;free(p);} }
int esp_elf_arch_flush(esp_elf_t* elf) { (void)elf; return 0; }
void esp_elf_registered_symbol_used(const void* table,const char* name,uintptr_t addr) {
    (void)table;(void)name;(void)addr;
}
uintptr_t native_app_memory_symbol(const char* name) {
    return !strcmp(name,"malloc") ? 0x1234 : 0;
}
void* dlmod_getaddr(const char* name) { return !strcmp(name,"module_export") ? (void*)0x4567 : NULL; }
static uintptr_t custom(const char* name) { (void)name;++custom_calls;return 0x789a; }
int* __errno(void) { return &errno; }
struct _reent; struct _reent* __getreent(void) { return NULL; }
const char _ctype_[1] = {0};
int __ltdf2(double a,double b){return a<b;}
unsigned __fixunsdfsi(double a){return (unsigned)a;}
int __gtdf2(double a,double b){return a>b;}
double __floatunsidf(unsigned a){return a;}
double __divdf3(double a,double b){return a/b;}

int esp_elf_arch_relocate(esp_elf_t* elf,const elf32_rela_t* rela,
                          const elf32_sym_t* sym,uint32_t address) {
    (void)rela;(void)sym;assert(address);++relocated;
    if(inject) {
        assert(esp_elf_privileged_os_cpu_scope_owned_v1());
        assert(elf_find_sym("memcpy") == (uintptr_t)&memcpy);
        assert(elf_find_sym("xTaskGetTickCount") == (uintptr_t)&xTaskGetTickCount);
        assert(!elf_find_sym("vTaskDelay") && !elf_find_sym("memcmp"));
        assert(!elf_find_sym("registered") && !elf_find_sym("module_export"));
        assert(!elf_find_sym("printf") && !elf_find_sym("puts") && !elf_find_sym("putchar"));
        assert(!esp_elf_privileged_os_cpu_begin_v1());
        assert(!begin_selected(selected,2));
        assert(!esp_elf_privileged_os_cpu_end_v1());
        assert(!esp_elf_privileged_os_cpu_authorize_relocation_v1(elf));
        esp_elf_t nested;
        assert(!esp_elf_init(&nested));
        assert(esp_elf_relocate(&nested,active_bytes)==-EPERM);
        assert(esp_elf_relocate(elf,active_bytes)==-EPERM);
        task=(TaskHandle_t)2;
        assert(!lookup_selected("xTaskGetTickCount"));
        assert(!esp_elf_privileged_os_cpu_relocation_leave_v1(elf));
        assert(!esp_elf_privileged_os_cpu_end_v1());
        assert(esp_elf_relocate(elf,active_bytes)==-EPERM);
        assert(elf_find_sym("xTaskGetTickCount")==0x789a);
        // This different task may still map a different ordinary module.
        // Its process-global resolver must not see this provider's imports.
        inject=false;
        assert(!esp_elf_relocate(&nested,active_bytes));
        esp_elf_deinit(&nested);
        inject=true;
        task=(TaskHandle_t)1;
    }
    return fail_arch && inject ? -ENOSYS : 0;
}
static uint8_t* read_image(const char* dir,const char* name,size_t* size) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s.elf",dir,name);
    FILE* f=fopen(path,"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);
    uint8_t* bytes=malloc((size_t)n);assert(bytes);
    assert(fread(bytes,1,(size_t)n,f)==(size_t)n);assert(!fclose(f));*size=(size_t)n;return bytes;
}
int main(int argc,char** argv) {
    assert(argc==2);
    const char* additions[]={"_frxt_setup_switch","esp_log","esp_rom_delay_us",
        "periph_rcc_enter","periph_rcc_exit","xTaskGetCoreID"};
    for(size_t i=0;i<sizeof(additions)/sizeof(*additions);++i) {
        assert(!elf_find_sym(additions[i]));
        assert(!esp_elf_privileged_selected_import_supported_v1(additions[i]));
        assert(esp_elf_privileged_selected_import_supported_v2(additions[i]) == !!RISC_NATIVE_ESP_IDF);
    }
    assert(!esp_elf_privileged_selected_import_supported_v2("xTaskGenericNotify"));
    size_t cohort_size; uint8_t* cohort_bytes=read_image(argv[1],"selected",&cohort_size);
    esp_elf_t cohort_module;
#if RISC_NATIVE_ESP_IDF
    assert(!esp_elf_privileged_os_cpu_begin_v1());
    assert(!esp_elf_privileged_os_cpu_begin_selected_v1(selected,2));
    assert(esp_elf_relocate_privileged_selected_v1(&cohort_module,cohort_bytes,cohort_size,selected,2)==-ENOTSUP);
    assert(esp_elf_relocate_privileged_verified_v1(&cohort_module,cohort_bytes,cohort_size,selected,2)==-ENOTSUP);
#else
    assert(!esp_elf_privileged_os_cpu_begin_selected_diagnostics_v2(selected,2,0));
    assert(esp_elf_relocate_privileged_selected_diagnostics_v2(&cohort_module,cohort_bytes,cohort_size,selected,2,0)==-ENOTSUP);
#endif
    assert(!allocations && !relocated); free(cohort_bytes);
    assert(selected_supported("xTaskGetTickCount"));
    assert(selected_supported("heap_caps_malloc"));
    assert(selected_supported("memcpy"));
    const char* denied[]={NULL,"","printf","puts","putchar","fputs","pthread_create",
        "memmove","snprintf","usb_device_init","registered","module_export"};
    for(size_t i=0;i<sizeof(denied)/sizeof(*denied);++i)
        assert(!selected_supported(denied[i]));
    assert(!lookup_selected("xTaskGetTickCount"));
    /* An absent linked diagnostic adapter cannot be granted by ABI metadata. */
    assert(esp_elf_privileged_diagnostic_abi_supported_v1(0));
    assert(!esp_elf_privileged_diagnostic_abi_supported_v1(1));
    assert(!esp_elf_privileged_diagnostic_abi_supported_v1(2));
    assert(!selected_diagnostic("puts",1));
    assert(!selected_diagnostic("memcpy",1));
    esp_elf_symbol_table_t table[]={{"registered",(void*)0x3456},ESP_ELFSYM_END};
    assert(!esp_elf_register_symbol(table));
    assert(elf_find_sym("registered")==0x3456);
    assert(elf_find_sym("module_export")==0x4567);
    assert(elf_find_sym("malloc")==0x1234);
    elf_set_symbol_resolver(custom);
    assert(elf_find_sym("anything")==0x789a);
    const char* duplicate[]={"memcpy","memcpy"};
    assert(!begin_selected(duplicate,2));
    const char* console[]={"puts"};
    const char* unsupported[]={"fputs"};
    assert(!begin_selected(console,1));
    assert(begin_selected(selected,2));
    assert(!elf_find_sym("memcpy") && !elf_find_sym("xTaskGetTickCount"));
    esp_elf_t reserved;
    assert(esp_elf_privileged_os_cpu_authorize_relocation_v1(&reserved));
    assert(!elf_find_sym("memcpy"));
    assert(esp_elf_privileged_os_cpu_relocation_enter_v1(&reserved));
    assert(elf_find_sym("memcpy")== (uintptr_t)&memcpy);
    assert(esp_elf_privileged_os_cpu_relocation_leave_v1(&reserved));
    assert(!elf_find_sym("memcpy"));
    assert(!esp_elf_privileged_os_cpu_relocation_enter_v1(&reserved));
    assert(!esp_elf_privileged_os_cpu_authorize_relocation_v1(&reserved));
    assert(esp_elf_privileged_os_cpu_end_v1());
    size_t size;uint8_t* bytes=read_image(argv[1],"selected",&size);active_bytes=bytes;
    esp_elf_t module;inject=true;
    assert(!relocate_selected(&module,bytes,size,selected,2));
    assert(relocated==6 && allocations);esp_elf_deinit(&module);assert(!allocations);
    assert(!esp_elf_privileged_os_cpu_scope_owned_v1());
    unsigned calls=custom_calls;assert(elf_find_sym("memcpy")==0x789a);assert(custom_calls==calls+1);
    fail_arch=true;
    assert(relocate_selected(&module,bytes,size,selected,2)==-ENOSYS);
    assert(!allocations && !esp_elf_privileged_os_cpu_scope_owned_v1());fail_arch=false;
    assert(relocate_selected(&module,bytes,size,empty,0)==-EINVAL);
    bytes[0]=0;assert(relocate_selected(&module,bytes,size,selected,2)==-EINVAL);
    free(bytes);
    const char* variants[]={"hidden","stdio","unsupported","empty"};
    for(unsigned i=0;i<4;++i){
        bytes=read_image(argv[1],variants[i],&size);
        const char* const* imports=i==1?console:i==2?unsupported:i==3?empty:selected;
        const size_t n=(i==1 || i==2)?1:i==3?0:2;
        assert(relocate_selected(&module,bytes,size,imports,n)==(i==3?0:-EINVAL));
        if(i==3)esp_elf_deinit(&module);
        assert(!allocations && !esp_elf_privileged_os_cpu_scope_owned_v1());free(bytes);
    }
#if RISC_NATIVE_ESP_IDF
    /* Resolve every new symbol only in one selected, module-bound grant. */
    assert(esp_elf_privileged_os_cpu_begin_selected_diagnostics_v2(additions,6,0));
    assert(!lookup_selected(additions[0]));
    assert(esp_elf_privileged_os_cpu_authorize_relocation_v1(&reserved));
    assert(esp_elf_privileged_os_cpu_relocation_enter_v1(&reserved));
    for(size_t i=0;i<6;++i) {
        assert(elf_find_sym(additions[i]));
        assert(!esp_elf_privileged_os_cpu_lookup_v1(additions[i]));
    }
    assert(!elf_find_sym("xTaskGetTickCount") && !elf_find_sym("memcpy"));
    assert(esp_elf_privileged_os_cpu_relocation_leave_v1(&reserved));
    assert(!lookup_selected(additions[0]));
    assert(esp_elf_privileged_os_cpu_end_v1());
    bytes=read_image(argv[1],"native-six",&size);
    assert(esp_elf_privileged_manifest_imports_match_v2(bytes,size,additions,6));
    assert(!esp_elf_privileged_manifest_imports_match_v1(bytes,size,additions,6));
    assert(!esp_elf_privileged_manifest_imports_match_v2(bytes,size,additions,5));
    free(bytes);
    bytes=read_image(argv[1],"native-weak-hidden",&size);
    assert(!esp_elf_privileged_imports_valid_v2(bytes,size)); free(bytes);
#endif
    elf_reset_symbol_resolver();assert(elf_find_sym("registered")==0x3456);
    assert(elf_find_sym("malloc")==0x1234);assert(!elf_find_sym("xTaskGetTickCount"));
    assert(!esp_elf_unregister_symbol(table));
    puts("Selected provider low-level: exact imports, real availability, task/one-shot/nested isolation and cleanup PASS");
}
