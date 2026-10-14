#include <RiscRuntimeV1.h>
#include <RiscResidentShellV1.h>
extern bool removable_is_resident(void);
extern void removable_private_data(const risc_runtime_api_v1*);
extern void removable_resident_entry(const risc_runtime_api_v1*);
#if defined(REMOVABLE_DEFAULT) || defined(REMOVABLE_PREVIOUS)
__attribute__((visibility("default"))) const risc_resident_app_descriptor_v1_t risc_resident_app_descriptor_v1={1,sizeof(risc_resident_app_descriptor_v1_t),
#ifdef REMOVABLE_DEFAULT
RISC_RESIDENT_ROLE_HOST,0};
#else
RISC_RESIDENT_ROLE_FOREGROUND,0};
#endif
#endif
extern unsigned removable_event(unsigned,unsigned);
extern const char* removable_launch_name(void);
#ifndef IMAGE_REVISION
#define IMAGE_REVISION 1
#endif
static unsigned counter=IMAGE_REVISION;
__attribute__((visibility("default"))) void app_main(void) {
#ifdef REMOVABLE_DEFAULT
  const risc_runtime_api_v1* rt=risc_runtime_get_api(1);
  if(removable_is_resident()){removable_resident_entry(rt);return;}
  if(removable_event(0,IMAGE_REVISION))rt->request_launch(removable_launch_name());
#elif defined(REMOVABLE_PREVIOUS)
  removable_event(6,counter++);
#else
  removable_private_data(risc_runtime_get_api(1));
  removable_event(1,counter++);
#endif
}
