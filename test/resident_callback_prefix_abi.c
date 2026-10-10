/* The exact pre-.98 callback layout, compiled independently of the new suffix. */
#include <RiscResidentShellV1.h>
typedef struct {
  uint32_t api_version, struct_size;
  void* context;
  int32_t (*dispatch)(void*,const risc_resident_request_v1*,risc_resident_reply_v1*);
  void (*failed)(void*,const risc_resident_failure_v1*);
} resident_callbacks_0196;
#define SAME(field) _Static_assert(offsetof(resident_callbacks_0196,field)==offsetof(risc_resident_callbacks_v1,field),"old callback offset changed: " #field)
SAME(api_version);SAME(struct_size);SAME(context);SAME(dispatch);SAME(failed);
_Static_assert(sizeof(resident_callbacks_0196)==RISC_RESIDENT_CALLBACKS_V1_SIZE,"old callback extent changed");
_Static_assert(RISC_RESIDENT_CALLBACKS_LOADING_V1_SIZE==sizeof(risc_resident_callbacks_v1),"loading suffix must be complete");
