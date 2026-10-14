#include <RiscResidentShellV1.h>
#ifndef ATOMIC_APP_CHILD
#define ATOMIC_APP_CHILD 0
#endif
#ifdef ATOMIC_RESIDENT
__attribute__((visibility("default")))
const risc_resident_app_descriptor_v1_t risc_resident_app_descriptor_v1={1,sizeof(risc_resident_app_descriptor_v1_t),ATOMIC_APP_CHILD?RISC_RESIDENT_ROLE_FOREGROUND:RISC_RESIDENT_ROLE_HOST,0};
#endif
extern void test_atomic_app(unsigned,unsigned);
__attribute__((visibility("default"))) int app_module_init(void){test_atomic_app(0,ATOMIC_APP_CHILD);return 0;}
__attribute__((visibility("default"))) void app_main(void){test_atomic_app(1,ATOMIC_APP_CHILD);}
__attribute__((visibility("default"))) void app_module_fini(void){test_atomic_app(2,ATOMIC_APP_CHILD);}
