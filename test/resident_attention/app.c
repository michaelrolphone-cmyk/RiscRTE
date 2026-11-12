#include <RiscResidentShellV1.h>
#ifndef ATTENTION_ROLE
#define ATTENTION_ROLE 0
#endif
extern void attention_entry(unsigned);
extern void attention_fini(unsigned);
__attribute__((visibility("default"))) const risc_resident_app_descriptor_v1_t risc_resident_app_descriptor_v1={1,sizeof(risc_resident_app_descriptor_v1_t),ATTENTION_ROLE?RISC_RESIDENT_ROLE_FOREGROUND:RISC_RESIDENT_ROLE_HOST,0};
__attribute__((visibility("default"))) int app_module_init(void){return 0;}
__attribute__((visibility("default"))) void app_main(void){attention_entry(ATTENTION_ROLE);}
__attribute__((visibility("default"))) void app_module_fini(void){attention_fini(ATTENTION_ROLE);}
