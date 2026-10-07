/* Compile both exact headers without replacing either consumer's SDK. */
#define risc_runtime_api_v1 legacy_risc_runtime_api_v1
#define risc_runtime_health_v1 legacy_risc_runtime_health_v1
#define risc_runtime_capability_v1 legacy_risc_runtime_capability_v1
#define risc_runtime_get_api legacy_runtime_get_api
#include LEGACY_RUNTIME_HEADER
enum { legacy_capability_end=RISC_RUNTIME_CAPABILITIES_V1_SIZE,legacy_api_version=RISC_RUNTIME_API_V1 };
#undef risc_runtime_api_v1
#undef risc_runtime_health_v1
#undef risc_runtime_capability_v1
#undef risc_runtime_get_api
#undef RISC_RUNTIME_CAPABILITIES_V1_SIZE
#undef RISC_RUNTIME_BOOT_CONFIRM_V1_SIZE
#include "RiscRuntimeV1.h"
#define MEMBER(type,member) \
 _Static_assert(offsetof(legacy_##type,member)==offsetof(type,member),#type "." #member " offset"); \
 _Static_assert(sizeof(((legacy_##type*)0)->member)==sizeof(((type*)0)->member),#type "." #member " size")
MEMBER(risc_runtime_health_v1,struct_size);MEMBER(risc_runtime_health_v1,uptime_ms);
MEMBER(risc_runtime_health_v1,free_heap);MEMBER(risc_runtime_health_v1,app_address);
MEMBER(risc_runtime_health_v1,mac);MEMBER(risc_runtime_health_v1,target);
MEMBER(risc_runtime_capability_v1,struct_size);MEMBER(risc_runtime_capability_v1,slot);
MEMBER(risc_runtime_capability_v1,generation);MEMBER(risc_runtime_capability_v1,api);
MEMBER(risc_runtime_api_v1,api_version);MEMBER(risc_runtime_api_v1,struct_size);
MEMBER(risc_runtime_api_v1,health);MEMBER(risc_runtime_api_v1,yield_ms);
MEMBER(risc_runtime_api_v1,diagnostic);MEMBER(risc_runtime_api_v1,request_launch);
MEMBER(risc_runtime_api_v1,acquire);MEMBER(risc_runtime_api_v1,release);
_Static_assert(sizeof(legacy_risc_runtime_health_v1)==sizeof(risc_runtime_health_v1),"health unchanged");
_Static_assert(sizeof(legacy_risc_runtime_capability_v1)==sizeof(risc_runtime_capability_v1),"grant unchanged");
_Static_assert(legacy_api_version==RISC_RUNTIME_API_V1,"API version unchanged");
_Static_assert(legacy_capability_end==RISC_RUNTIME_CAPABILITIES_V1_SIZE,"prefix unchanged");
_Static_assert(sizeof(legacy_risc_runtime_api_v1)==legacy_capability_end,"frozen legacy ends at release");
_Static_assert(offsetof(risc_runtime_api_v1,confirm_boot)==legacy_capability_end,"new callback appends only");
_Static_assert(offsetof(risc_runtime_api_v1,retain_invocation)==RISC_RUNTIME_BOOT_CONFIRM_V1_SIZE,"retention appends after complete confirm prefix");
_Static_assert(sizeof(risc_runtime_api_v1)==legacy_capability_end+2*sizeof(void(*)(void)),"two pointer suffixes only");
_Static_assert(sizeof(risc_runtime_api_v1)==RISC_RUNTIME_RETAIN_INVOCATION_V1_SIZE,"retention is the final suffix");
