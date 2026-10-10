#include <RiscRuntimeV1.h>
#include <RiscResidentShellV1.h>
#ifndef RESIDENT_APP_ROLE
#define RESIDENT_APP_ROLE 1
#endif
extern int test_resident_init(unsigned);
extern void test_resident_main(unsigned,unsigned*);
extern void test_resident_fini(unsigned);
static unsigned visits;
#ifndef RESIDENT_NO_DESCRIPTOR
__attribute__((visibility("default"))) const risc_resident_app_descriptor_v1_t risc_resident_app_descriptor_v1={
  1,sizeof(risc_resident_app_descriptor_v1_t),
  RESIDENT_APP_ROLE==0?RISC_RESIDENT_ROLE_HOST:RISC_RESIDENT_ROLE_FOREGROUND,0};
#endif
__attribute__((visibility("default"))) int app_module_init(void){return test_resident_init(RESIDENT_APP_ROLE);}
#ifndef RESIDENT_NO_ENTRY
__attribute__((visibility("default"))) void app_main(void){test_resident_main(RESIDENT_APP_ROLE,&visits);}
#endif
__attribute__((visibility("default"))) void app_module_fini(void){test_resident_fini(RESIDENT_APP_ROLE);}

#ifdef RESIDENT_LOADING_TEST
extern void test_resident_map(unsigned);
extern void test_resident_unmap(unsigned);
__attribute__((constructor)) static void mapped(void){test_resident_map(RESIDENT_APP_ROLE);}
__attribute__((destructor)) static void unmapped(void){test_resident_unmap(RESIDENT_APP_ROLE);}
#endif
