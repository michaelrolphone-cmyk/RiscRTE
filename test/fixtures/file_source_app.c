#include <RiscRuntimeV1.h>
#include <RiscResidentShellV1.h>
#include <assert.h>
#ifndef SOURCE_ROLE
#define SOURCE_ROLE 0
#endif
extern int source_test_init(unsigned);
extern void source_test_main(unsigned,char*);
extern void source_test_fini(unsigned);
static unsigned initialized=0,entered=0;
static char name[]="Original.OBJ";
#ifndef SOURCE_LEGACY
__attribute__((visibility("default"))) const risc_resident_app_descriptor_v1_t risc_resident_app_descriptor_v1={1,sizeof(risc_resident_app_descriptor_v1_t),SOURCE_ROLE?RISC_RESIDENT_ROLE_FOREGROUND:RISC_RESIDENT_ROLE_HOST,0};
#endif
__attribute__((visibility("default"))) int app_module_init(void){assert(!initialized++ && !entered);return source_test_init(SOURCE_ROLE);}
__attribute__((visibility("default"))) void app_main(void){assert(initialized==1 && !entered++);source_test_main(SOURCE_ROLE,name);}
__attribute__((visibility("default"))) void app_module_fini(void){assert(initialized==1);source_test_fini(SOURCE_ROLE);}
