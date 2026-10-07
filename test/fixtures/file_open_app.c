#include <RiscRuntimeV1.h>
#include <T5FileOpenApi.h>
#include <assert.h>
#include <string.h>

#ifndef FILE_APP_ROLE
#define FILE_APP_ROLE 0
#endif
extern int test_file_init(unsigned role);
extern void test_file_fini(unsigned role);
extern void test_file_main(unsigned role,const risc_runtime_api_v1*,char*,char*);
static unsigned initialized,entered;
#ifndef FILE_APP_NO_ENTRY
static char source[T5_FILE_OPEN_PATH_MAX]="/sd/Books/Original.TXT";
static char app_id[T5_FILE_HANDLER_ID_MAX]="viewer";
#endif
__attribute__((visibility("default"))) int app_module_init(void) {
  assert(initialized++==0 && entered==0);
  return test_file_init(FILE_APP_ROLE);
}
__attribute__((visibility("default"))) void app_module_fini(void) {
  assert(initialized==1);
  test_file_fini(FILE_APP_ROLE);
}
#ifndef FILE_APP_NO_ENTRY
__attribute__((visibility("default"))) void app_main(void) {
  assert(initialized==1 && entered++==0);
  assert(!strcmp(source,"/sd/Books/Original.TXT") && !strcmp(app_id,"viewer"));
  const risc_runtime_api_v1* runtime=risc_runtime_get_api(1);
  assert(runtime);
  test_file_main(FILE_APP_ROLE,runtime,source,app_id);
}

#endif
