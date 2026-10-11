#define risc_runtime_api_v1 previous_runtime_api_v1
#define risc_runtime_health_v1 previous_runtime_health_v1
#define risc_runtime_capability_v1 previous_runtime_capability_v1
#define risc_runtime_get_api previous_runtime_get_api
#include "fixtures/runtime_api_0176.h"
#undef risc_runtime_api_v1
#undef risc_runtime_health_v1
#undef risc_runtime_capability_v1
#undef risc_runtime_get_api
#undef RISC_RUNTIME_CAPABILITIES_V1_SIZE
#undef RISC_RUNTIME_BOOT_CONFIRM_V1_SIZE
#undef RISC_RUNTIME_RETAIN_INVOCATION_V1_SIZE
#undef RISC_RUNTIME_TRACE_V1_SIZE
#undef RISC_RUNTIME_STREAM_CLIENT_V1_SIZE
#include "RiscRuntimeV1.h"
_Static_assert(offsetof(previous_runtime_api_v1,api_version)==offsetof(risc_runtime_api_v1,api_version),"prefix api_version");
_Static_assert(sizeof(((previous_runtime_api_v1*)0)->api_version)==sizeof(((risc_runtime_api_v1*)0)->api_version),"size api_version");
_Static_assert(offsetof(previous_runtime_api_v1,struct_size)==offsetof(risc_runtime_api_v1,struct_size),"prefix struct_size");
_Static_assert(sizeof(((previous_runtime_api_v1*)0)->struct_size)==sizeof(((risc_runtime_api_v1*)0)->struct_size),"size struct_size");
_Static_assert(offsetof(previous_runtime_api_v1,health)==offsetof(risc_runtime_api_v1,health),"prefix health");
_Static_assert(sizeof(((previous_runtime_api_v1*)0)->health)==sizeof(((risc_runtime_api_v1*)0)->health),"size health");
_Static_assert(offsetof(previous_runtime_api_v1,yield_ms)==offsetof(risc_runtime_api_v1,yield_ms),"prefix yield_ms");
_Static_assert(sizeof(((previous_runtime_api_v1*)0)->yield_ms)==sizeof(((risc_runtime_api_v1*)0)->yield_ms),"size yield_ms");
_Static_assert(offsetof(previous_runtime_api_v1,diagnostic)==offsetof(risc_runtime_api_v1,diagnostic),"prefix diagnostic");
_Static_assert(sizeof(((previous_runtime_api_v1*)0)->diagnostic)==sizeof(((risc_runtime_api_v1*)0)->diagnostic),"size diagnostic");
_Static_assert(offsetof(previous_runtime_api_v1,request_launch)==offsetof(risc_runtime_api_v1,request_launch),"prefix request_launch");
_Static_assert(sizeof(((previous_runtime_api_v1*)0)->request_launch)==sizeof(((risc_runtime_api_v1*)0)->request_launch),"size request_launch");
_Static_assert(offsetof(previous_runtime_api_v1,acquire)==offsetof(risc_runtime_api_v1,acquire),"prefix acquire");
_Static_assert(sizeof(((previous_runtime_api_v1*)0)->acquire)==sizeof(((risc_runtime_api_v1*)0)->acquire),"size acquire");
_Static_assert(offsetof(previous_runtime_api_v1,release)==offsetof(risc_runtime_api_v1,release),"prefix release");
_Static_assert(sizeof(((previous_runtime_api_v1*)0)->release)==sizeof(((risc_runtime_api_v1*)0)->release),"size release");
_Static_assert(offsetof(previous_runtime_api_v1,confirm_boot)==offsetof(risc_runtime_api_v1,confirm_boot),"prefix confirm_boot");
_Static_assert(sizeof(((previous_runtime_api_v1*)0)->confirm_boot)==sizeof(((risc_runtime_api_v1*)0)->confirm_boot),"size confirm_boot");
_Static_assert(offsetof(previous_runtime_api_v1,retain_invocation)==offsetof(risc_runtime_api_v1,retain_invocation),"prefix retain_invocation");
_Static_assert(sizeof(((previous_runtime_api_v1*)0)->retain_invocation)==sizeof(((risc_runtime_api_v1*)0)->retain_invocation),"size retain_invocation");
_Static_assert(offsetof(previous_runtime_api_v1,trace)==offsetof(risc_runtime_api_v1,trace),"prefix trace");
_Static_assert(sizeof(((previous_runtime_api_v1*)0)->trace)==sizeof(((risc_runtime_api_v1*)0)->trace),"size trace");
_Static_assert(offsetof(previous_runtime_api_v1,stream_client)==offsetof(risc_runtime_api_v1,stream_client),"prefix stream_client");
_Static_assert(sizeof(((previous_runtime_api_v1*)0)->stream_client)==sizeof(((risc_runtime_api_v1*)0)->stream_client),"size stream_client");
_Static_assert(sizeof(previous_runtime_api_v1)==RISC_RUNTIME_STREAM_CLIENT_V1_SIZE,"complete old table");
_Static_assert(offsetof(risc_runtime_api_v1,request_default)==sizeof(previous_runtime_api_v1),"append-only Home");
