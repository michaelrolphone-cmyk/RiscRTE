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
#ifndef CURRENT_RUNTIME_HEADER
#define CURRENT_RUNTIME_HEADER "RiscRuntimeV1.h"
#endif
#include CURRENT_RUNTIME_HEADER
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
_Static_assert(sizeof(risc_runtime_api_v1)==legacy_capability_end+12*sizeof(void(*)(void)),"twelve pointer suffixes only");
_Static_assert(offsetof(risc_runtime_api_v1,trace)==RISC_RUNTIME_RETAIN_INVOCATION_V1_SIZE,"trace follows complete retention prefix");

_Static_assert(offsetof(risc_runtime_api_v1,stream_client)==RISC_RUNTIME_TRACE_V1_SIZE,"stream client follows complete trace prefix");
_Static_assert(offsetof(risc_runtime_api_v1,request_default)==RISC_RUNTIME_STREAM_CLIENT_V1_SIZE,"Home follows complete stream prefix");
_Static_assert(offsetof(risc_runtime_api_v1,resident_shell)==RISC_RUNTIME_DEFAULT_REQUEST_V1_SIZE,"resident follows Home");
_Static_assert(offsetof(risc_runtime_api_v1,failure_evidence)==RISC_RUNTIME_RESIDENT_SHELL_V1_SIZE,"evidence follows resident");
_Static_assert(offsetof(risc_runtime_api_v1,diagnostic_checkpoint_client)==RISC_RUNTIME_FAILURE_EVIDENCE_V1_SIZE,"checkpoint follows evidence");
_Static_assert(offsetof(risc_runtime_api_v1,monotonic_ms)==RISC_RUNTIME_DIAGNOSTIC_CHECKPOINT_V1_SIZE,"clock follows checkpoint");
_Static_assert(offsetof(risc_runtime_api_v1,exit_requested)==RISC_RUNTIME_MONOTONIC_V1_SIZE,"host control follows clock");
/* Later optional callbacks retain the complete host-control prefix. */
_Static_assert(offsetof(risc_runtime_api_v1,take_launch_failure)==RISC_RUNTIME_EXIT_REQUESTED_V1_SIZE,"launch failure follows host control");
_Static_assert(offsetof(risc_runtime_api_v1,initial_default)==RISC_RUNTIME_LAUNCH_FAILURE_V1_SIZE,"initial default follows launch failure");
_Static_assert(sizeof(risc_runtime_api_v1)==RISC_RUNTIME_INITIAL_DEFAULT_V1_SIZE,"initial default is final suffix");
_Static_assert(_Generic(((risc_runtime_api_v1*)0)->take_launch_failure,
 bool(*)(risc_runtime_launch_failure_v1*):1,default:0),"launch failure signature");
_Static_assert(_Generic(((risc_runtime_api_v1*)0)->initial_default,
 bool(*)(void):1,default:0),"initial default signature");
