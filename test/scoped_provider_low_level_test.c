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
        assert(!esp_elf_privileged_os_cpu_begin_selected_v1(selected,2));
        assert(!esp_elf_privileged_os_cpu_end_v1());
        assert(!esp_elf_privileged_os_cpu_authorize_relocation_v1(elf));
        esp_elf_t nested;
        assert(!esp_elf_init(&nested));
        assert(esp_elf_relocate(&nested,active_bytes)==-EPERM);
        assert(esp_elf_relocate(elf,active_bytes)==-EPERM);
        task=(TaskHandle_t)2;
        assert(!esp_elf_privileged_os_cpu_lookup_v1("xTaskGetTickCount"));
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
    assert(esp_elf_privileged_selected_import_supported_v1("xTaskGetTickCount"));
    assert(esp_elf_privileged_selected_import_supported_v1("heap_caps_malloc"));
    assert(esp_elf_privileged_selected_import_supported_v1("memcpy"));
    const char* denied[]={NULL,"","printf","puts","putchar","fputs","pthread_create",
        "memmove","snprintf","usb_device_init","registered","module_export"};
    for(size_t i=0;i<sizeof(denied)/sizeof(*denied);++i)
        assert(!esp_elf_privileged_selected_import_supported_v1(denied[i]));
    assert(!esp_elf_privileged_os_cpu_lookup_v1("xTaskGetTickCount"));
    /* An absent linked diagnostic adapter cannot be granted by ABI metadata. */
    assert(esp_elf_privileged_diagnostic_abi_supported_v1(0));
    assert(!esp_elf_privileged_diagnostic_abi_supported_v1(1));
    assert(!esp_elf_privileged_diagnostic_abi_supported_v1(2));
    assert(!esp_elf_privileged_selected_import_supported_with_diagnostics_v1("puts",1));
    assert(!esp_elf_privileged_selected_import_supported_with_diagnostics_v1("memcpy",1));
    esp_elf_symbol_table_t table[]={{"registered",(void*)0x3456},ESP_ELFSYM_END};
    assert(!esp_elf_register_symbol(table));
    assert(elf_find_sym("registered")==0x3456);
    assert(elf_find_sym("module_export")==0x4567);
    assert(elf_find_sym("malloc")==0x1234);
    elf_set_symbol_resolver(custom);
    assert(elf_find_sym("anything")==0x789a);
    const char* duplicate[]={"memcpy","memcpy"};
    assert(!esp_elf_privileged_os_cpu_begin_selected_v1(duplicate,2));
    const char* console[]={"puts"};
    const char* unsupported[]={"fputs"};
    assert(!esp_elf_privileged_os_cpu_begin_selected_v1(console,1));
    assert(esp_elf_privileged_os_cpu_begin_selected_v1(selected,2));
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
    assert(!esp_elf_relocate_privileged_selected_v1(&module,bytes,size,selected,2));
    assert(relocated==6 && allocations);esp_elf_deinit(&module);assert(!allocations);
    assert(!esp_elf_privileged_os_cpu_scope_owned_v1());
    unsigned calls=custom_calls;assert(elf_find_sym("memcpy")==0x789a);assert(custom_calls==calls+1);
    fail_arch=true;
    assert(esp_elf_relocate_privileged_selected_v1(&module,bytes,size,selected,2)==-ENOSYS);
    assert(!allocations && !esp_elf_privileged_os_cpu_scope_owned_v1());fail_arch=false;
    assert(esp_elf_relocate_privileged_selected_v1(&module,bytes,size,empty,0)==-EINVAL);
    bytes[0]=0;assert(esp_elf_relocate_privileged_selected_v1(&module,bytes,size,selected,2)==-EINVAL);
    free(bytes);
    const char* variants[]={"hidden","stdio","unsupported","empty"};
    for(unsigned i=0;i<4;++i){
        bytes=read_image(argv[1],variants[i],&size);
        const char* const* imports=i==1?console:i==2?unsupported:i==3?empty:selected;
        const size_t n=(i==1 || i==2)?1:i==3?0:2;
        assert(esp_elf_relocate_privileged_selected_v1(&module,bytes,size,imports,n)==(i==3?0:-EINVAL));
        if(i==3)esp_elf_deinit(&module);
        assert(!allocations && !esp_elf_privileged_os_cpu_scope_owned_v1());free(bytes);
    }
    elf_reset_symbol_resolver();assert(elf_find_sym("registered")==0x3456);
    assert(elf_find_sym("malloc")==0x1234);assert(!elf_find_sym("xTaskGetTickCount"));
    assert(!esp_elf_unregister_symbol(table));
    puts("Selected provider low-level: exact imports, real availability, task/one-shot/nested isolation and cleanup PASS");
}
