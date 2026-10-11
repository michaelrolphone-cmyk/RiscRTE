#include <RiscResidentShellV1.h>
#ifndef FAILURE_APP_ROLE
#define FAILURE_APP_ROLE 0
#endif
extern int failure_test_init(unsigned);
extern void failure_test_main(unsigned);
extern void failure_test_fini(unsigned);
#if FAILURE_APP_ROLE != 2
__attribute__((visibility("default"))) const risc_resident_app_descriptor_v1_t risc_resident_app_descriptor_v1={
  1,sizeof(risc_resident_app_descriptor_v1_t),FAILURE_APP_ROLE?RISC_RESIDENT_ROLE_FOREGROUND:RISC_RESIDENT_ROLE_HOST,0};
#endif
__attribute__((visibility("default"))) int app_module_init(void){return failure_test_init(FAILURE_APP_ROLE);}
__attribute__((visibility("default"))) void app_main(void){failure_test_main(FAILURE_APP_ROLE);}
__attribute__((visibility("default"))) void app_module_fini(void){failure_test_fini(FAILURE_APP_ROLE);}
