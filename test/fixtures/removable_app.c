#include <RiscRuntimeV1.h>
extern unsigned removable_event(unsigned,unsigned);
extern const char* removable_launch_name(void);
#ifndef IMAGE_REVISION
#define IMAGE_REVISION 1
#endif
static unsigned counter=IMAGE_REVISION;
__attribute__((visibility("default"))) void app_main(void) {
#ifdef REMOVABLE_DEFAULT
  const risc_runtime_api_v1* rt=risc_runtime_get_api(1);
  if(removable_event(0,0))rt->request_launch(removable_launch_name());
#else
  removable_event(1,counter++);
#endif
}
