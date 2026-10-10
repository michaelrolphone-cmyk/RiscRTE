#define risc_runtime_api_v1 old_runtime_api
#define risc_runtime_health_v1 old_runtime_health
#define risc_runtime_capability_v1 old_runtime_capability
#define risc_runtime_get_api old_runtime_get_api
#include "fixtures/failure_evidence_runtime_0192.h"
#undef risc_runtime_api_v1
#undef risc_runtime_health_v1
#undef risc_runtime_capability_v1
#undef risc_runtime_get_api
#include <RiscRuntimeV1.h>
#include <RiscFailureEvidenceV1.h>
#define SAME(field) _Static_assert(offsetof(old_runtime_api,field)==offsetof(risc_runtime_api_v1,field),"old offset " #field); _Static_assert(sizeof(((old_runtime_api*)0)->field)==sizeof(((risc_runtime_api_v1*)0)->field),"old size " #field)
SAME(api_version);SAME(struct_size);SAME(health);SAME(yield_ms);SAME(diagnostic);
SAME(request_launch);SAME(acquire);SAME(release);SAME(confirm_boot);
SAME(retain_invocation);SAME(trace);SAME(stream_client);SAME(request_default);SAME(resident_shell);
_Static_assert(sizeof(old_runtime_api)==offsetof(risc_runtime_api_v1,failure_evidence),"exact frozen .92 prefix");
_Static_assert(sizeof(old_runtime_api)==RISC_RUNTIME_RESIDENT_SHELL_V1_SIZE,"old table size remains resident prefix");
_Static_assert(sizeof(risc_runtime_api_v1)==RISC_RUNTIME_FAILURE_EVIDENCE_V1_SIZE,"one suffix only");
_Static_assert(sizeof(old_runtime_health)==sizeof(risc_runtime_health_v1),"health unchanged");
_Static_assert(sizeof(old_runtime_capability)==sizeof(risc_runtime_capability_v1),"grant unchanged");
int main(void){return 0;}
