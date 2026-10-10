/* Target-only primitives for host composition; never executed as hardware. */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "freertos/task.h"
extern void* diagnostic_test_task(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void){return diagnostic_test_task();}
TickType_t xTaskGetTickCount(void){return 1;}
void vTaskDelay(TickType_t ticks){(void)ticks;}
int* __errno(void){return &errno;}
struct _reent; struct _reent* __getreent(void){return NULL;}
const char _ctype_[1]={0};
int __ltdf2(double a,double b){return a<b;}
unsigned __fixunsdfsi(double a){return (unsigned)a;}
int __gtdf2(double a,double b){return a>b;}
double __floatunsidf(unsigned a){return a;}
double __divdf3(double a,double b){return a/b;}
void esp_elf_registered_symbol_used(const void* table,const char* name,uintptr_t address){(void)table;(void)name;(void)address;}
uintptr_t native_app_memory_symbol(const char* name){(void)name;return 0;}
void* dlmod_getaddr(const char* name){(void)name;return NULL;}
