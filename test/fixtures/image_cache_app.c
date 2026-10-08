#include <RiscRuntimeV1.h>
#include <assert.h>
extern unsigned image_cache_event(unsigned,unsigned);
static unsigned initialized=17,zeroed;
#ifndef IMAGE_REVISION
#define IMAGE_REVISION 1
#endif
__attribute__((visibility("default"))) int app_module_init(void) {
  assert(initialized==17 && zeroed==0);initialized=29;zeroed=1;
#ifdef IMAGE_CACHE_CHILD
  image_cache_event(6,IMAGE_REVISION);
#else
  image_cache_event(5,IMAGE_REVISION);
#endif
  return image_cache_event(1,0)?-1:0;
}
__attribute__((visibility("default"))) void app_main(void) {
  assert(initialized==29 && zeroed==1);++zeroed;
  const risc_runtime_api_v1* api=risc_runtime_get_api(1);assert(api);
#ifdef IMAGE_CACHE_CHILD
  const unsigned action=image_cache_event(3,0);
  if(action==1)assert(api->retain_invocation());
#else
  const unsigned action=image_cache_event(2,0);
  if(action==1)assert(api->request_launch("child.elf"));
  if(action==2)assert(api->request_launch("loose.elf"));
  if(action==3)assert(api->request_launch("missing.elf"));
#endif
}
__attribute__((visibility("default"))) void app_module_fini(void) {
  assert(initialized==29 && zeroed==2);image_cache_event(4,0);
  initialized=91;zeroed=77;
}
