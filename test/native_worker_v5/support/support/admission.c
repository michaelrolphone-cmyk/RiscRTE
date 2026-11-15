
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "private/esp_privileged_elf.h"
#include "private/esp_privileged_os_cpu.h"
#include "private/esp_privileged_imports.h"
#include "private/esp_privileged_manifest_imports.h"
#include "private/elf_symbol.h"
uintptr_t native_app_memory_symbol(const char*s){(void)s;return 0;}
uintptr_t esp_elf_find_symbol(const char*s){return !strcmp(s,"registered_not_allowed")?123:0;}
int __ltdf2(double a,double b){return a<b;}
unsigned __fixunsdfsi(double a){return (unsigned)a;}
int __gtdf2(double a,double b){return a>b;}
double __floatunsidf(unsigned a){return a;}
double __divdf3(double a,double b){return a/b;}
/* Darwin does not treat ELF-style weak definitions as weak imports.
 * Explicit disabled sink stubs preserve every production diagnostic check. */
const uint32_t risc_provider_diagnostic_build_abi_v1=1;
static uint32_t diagnostic_api;
uint32_t risc_provider_diagnostic_abi_v1(void){return diagnostic_api;}
int risc_provider_diagnostic_printf(const char *format,...){(void)format;return -1;}
int risc_provider_diagnostic_puts(const char *s){(void)s;return -1;}
int risc_provider_diagnostic_putchar(int c){(void)c;return -1;}
