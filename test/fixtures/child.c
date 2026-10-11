#include <RiscRuntimeV1.h>
__attribute__((visibility("default"))) void app_main(void) {
  const risc_runtime_api_v1* api=risc_runtime_get_api(1);
  if(api && api->struct_size>=RISC_RUNTIME_EXIT_REQUESTED_V1_SIZE && api->exit_requested && api->exit_requested()) {
    api->diagnostic("TEST native-exit-read");
    if(api->request_launch("child.elf"))api->diagnostic("TEST child-relaunch-queued");
    return;
  }
  if(api) { api->diagnostic("TEST child"); if(api->request_launch("../escape.elf")) api->diagnostic("TEST ERROR traversal"); }
}
