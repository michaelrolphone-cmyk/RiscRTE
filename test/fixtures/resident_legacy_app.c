#include <RiscRuntimeV1.h>
#include <RiscResidentShellV1.h>
#ifndef LEGACY_TEST_ROLE
#define LEGACY_TEST_ROLE 0
#endif
extern void test_legacy_map(unsigned);
extern void test_legacy_unmap(unsigned);
extern int test_legacy_init(unsigned);
extern void test_legacy_fini(unsigned);
extern void test_legacy_main(unsigned,unsigned*,const risc_resident_callbacks_v1*,char*,char*);
extern int32_t test_legacy_dispatch(unsigned*,const risc_resident_request_v1*,risc_resident_reply_v1*);
extern void test_legacy_failed(const risc_resident_failure_v1*);
#ifdef RESIDENT_LOADING_TEST
extern int32_t test_legacy_loading(void*,const char*);
#endif
static unsigned visits;
static char source[]="/sd/Books/Original.TXT";
static char receiver[32];
__attribute__((visibility("default"))) const unsigned resident_legacy_test_role=LEGACY_TEST_ROLE;
#if LEGACY_TEST_ROLE == 0 || LEGACY_TEST_ROLE == 1 || LEGACY_TEST_ROLE == 3
__attribute__((visibility("default"))) const risc_resident_app_descriptor_v1_t risc_resident_app_descriptor_v1={
  1,sizeof(risc_resident_app_descriptor_v1_t),
  LEGACY_TEST_ROLE==0?RISC_RESIDENT_ROLE_HOST:RISC_RESIDENT_ROLE_FOREGROUND,0};
#endif
static int32_t dispatch(void* context,const risc_resident_request_v1* request,risc_resident_reply_v1* reply){
  return test_legacy_dispatch(context,request,reply);
}
static void failed(void* context,const risc_resident_failure_v1* failure){
  (void)context;test_legacy_failed(failure);
}
__attribute__((constructor)) static void mapped(void){test_legacy_map(LEGACY_TEST_ROLE);}
__attribute__((destructor)) static void unmapped(void){test_legacy_unmap(LEGACY_TEST_ROLE);}
__attribute__((visibility("default"))) int app_module_init(void){return test_legacy_init(LEGACY_TEST_ROLE);}
__attribute__((visibility("default"))) void app_main(void){
  const risc_resident_callbacks_v1 callbacks={.api_version=1,.context=&visits,.dispatch=dispatch,.failed=failed,
#ifdef RESIDENT_LOADING_TEST
    .struct_size=RISC_RESIDENT_CALLBACKS_LOADING_V1_SIZE,.loading=test_legacy_loading
#else
    .struct_size=RISC_RESIDENT_CALLBACKS_V1_SIZE
#endif
  };
  test_legacy_main(LEGACY_TEST_ROLE,&visits,&callbacks,source,receiver);
}
__attribute__((visibility("default"))) void app_module_fini(void){test_legacy_fini(LEGACY_TEST_ROLE);}
