#include <RiscRuntimeV1.h>
#include <RiscResidentShellV1.h>
#ifndef RESIDENT_POLICY_ROLE
#define RESIDENT_POLICY_ROLE 0
#endif
extern void test_policy_map(unsigned);
extern void test_policy_unmap(unsigned);
extern int test_policy_init(unsigned);
extern void test_policy_fini(unsigned);
extern void test_policy_main(unsigned,unsigned*,const risc_resident_callbacks_v1*);
extern int32_t test_policy_dispatch(void*,const risc_resident_request_v1*,risc_resident_reply_v1*);
static unsigned visits;
__attribute__((visibility("default"))) const risc_resident_app_descriptor_v1_t risc_resident_app_descriptor_v1={
  1,sizeof(risc_resident_app_descriptor_v1_t),
  RESIDENT_POLICY_ROLE==0?RISC_RESIDENT_ROLE_HOST:RISC_RESIDENT_ROLE_FOREGROUND,0};
static int32_t dispatch(void* context,const risc_resident_request_v1* request,risc_resident_reply_v1* reply){
  return test_policy_dispatch(context,request,reply);
}
__attribute__((constructor)) static void mapped(void){test_policy_map(RESIDENT_POLICY_ROLE);}
__attribute__((destructor)) static void unmapped(void){test_policy_unmap(RESIDENT_POLICY_ROLE);}
__attribute__((visibility("default"))) int app_module_init(void){return test_policy_init(RESIDENT_POLICY_ROLE);}
__attribute__((visibility("default"))) void app_main(void){
  const risc_resident_callbacks_v1 callbacks={1,sizeof(callbacks),&visits,dispatch,0,0};
  test_policy_main(RESIDENT_POLICY_ROLE,&visits,&callbacks);
}
__attribute__((visibility("default"))) void app_module_fini(void){test_policy_fini(RESIDENT_POLICY_ROLE);}
