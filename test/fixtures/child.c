#include <RiscRuntimeV1.h>
__attribute__((visibility("default"))) void app_main(void) {
  const risc_runtime_api_v1* api=risc_runtime_get_api(1);
  if(api) { api->diagnostic("TEST child"); if(api->request_launch("../escape.elf")) api->diagnostic("TEST ERROR traversal"); }
}
