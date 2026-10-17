#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_RUNTIME_API_V1 1u
enum { RISC_RUNTIME_CHECKPOINT_OK=0, RISC_RUNTIME_CHECKPOINT_BUSY=1,
       RISC_RUNTIME_CHECKPOINT_CONTEXT=-1, RISC_RUNTIME_CHECKPOINT_RETAINED=-2 };
struct risc_stream_client_v1;
struct risc_resident_client_v1;
struct risc_failure_evidence_client_v1;
enum { RISC_RUNTIME_SLEEP_OK=0, RISC_RUNTIME_SLEEP_BUSY=1,
       RISC_RUNTIME_SLEEP_MEDIA_UNAVAILABLE=2, RISC_RUNTIME_SLEEP_CONTEXT=-1,
       RISC_RUNTIME_SLEEP_RETAINED=-2, RISC_RUNTIME_SLEEP_UNSUPPORTED=-3 };
struct risc_diagnostic_checkpoint_client_v1;
/* Minimal headless runtime service. Append-only. Available only on the active
 * app owner task, from module init through fini. Native apps are trusted code.
 * No pointers/callbacks/tasks may outlive app_main/fini. */
typedef struct {
  uint32_t struct_size;
  uint32_t uptime_ms, free_heap, app_address;
  uint8_t mac[6];
  char target[96];
} risc_runtime_health_v1;
/* App-scoped opaque grant. Zero-initialize and set struct_size before acquire.
 * The interface has its capability's canonical version/size header. Consumers
 * must check that header against the complete table they use. */
typedef struct {
  uint32_t struct_size, slot, generation;
  const void* api;
} risc_runtime_capability_v1;
/* Copied recoverable child failure. No borrowed memory or media authority. */
#define RISC_LAUNCH_FAILURE_EXTERNAL_IMAGE 1u
typedef struct {
  uint32_t struct_size, flags;
  char application[128], detail[160];
} risc_runtime_launch_failure_v1;
typedef struct {
  uint32_t api_version, struct_size;
  bool (*health)(risc_runtime_health_v1* out);
  /* Clamp 1..50; ordinary yields poll providers. An already-retained current
   * owner may only use the port's raw scheduler delay, with no provider work. */
  void (*yield_ms)(uint32_t milliseconds);
  bool (*diagnostic)(const char* line); /* one-way, max 255 bytes, newline added */
  /* Copies one normalized relative .elf path under the configured boot store.
   * True queues a handoff: return from app_main immediately. Current ELF is
   * finalized and unmapped before the next load. A second request is denied.
   * Failure to unload/quiesce blocks handoff. Child return/load failure reloads the configured default from scratch.
   * Default return enters Idle; default failure enters Error. */
  bool (*request_launch)(const char* relative_elf);
  /* Append-only capability extension. Allowed only by the active app manifest
   * and boot grant policy. instance_id=0 requires a unique authorized provider;
   * nonzero selects that exact hardware instance. No registry-order fallback.
   * Release frames/sessions/operations through their capability before release
   * or app return. Remaining grants are revoked before app memory/image teardown;
   * all pointers become invalid on release or end of this app invocation. */
  bool (*acquire)(const char* capability, uint32_t version, uint64_t instance_id,
                  risc_runtime_capability_v1* out);
  bool (*release)(risc_runtime_capability_v1* grant);
  /* Optional paired-bank health acknowledgement. Only the configured default
   * app, while executing app_main after successful initialization, may call.
   * Call after successful startup/first frame; never on error, exit or sleep.
   * Older runtime tables lack this suffix. Repeated calls are idempotent. */
  bool (*confirm_boot)(void);
  /* Optional terminal invocation fence for capability-local uncertain cleanup.
   * Owner task only, during active init/main/fini. True immediately revokes app
   * authority and provider storage access, discards handoff, and retains current
   * images, allocations and provider custody until restart, without cleanup or
   * polling. Return promptly without further provider calls or frees. This does
   * not enter sleep, restart, add authority or inspect an opaque capability.
   * Repeated owner calls for this same current retained invocation return true.
   * False means no current owner invocation or a reentrant promotion call.
   * A cached yield_ms remains scheduler-only on ports supporting retained delay.
   * Older tables lack this suffix; check its size before reading the pointer. */
  bool (*retain_invocation)(void);
  /* Optional bounded performance tracing; see RiscPerformanceV1.h. Owner task
   * only. Returns the accepted interaction ID, or zero if disabled/rejected or
   * an uncorrelated phase. Check table size before reading this suffix. */
  uint32_t (*trace)(uint32_t interaction_id, uint32_t phase, uint32_t value);
  /* Copies a generic stream client for the current owner invocation. The
   * existing explicit capability grant is still required to open a session. */
  bool (*stream_client)(struct risc_stream_client_v1* out);
  /* Optional explicit Home handoff. Owner task, app_main only, clean custody,
   * and no pending request. Queues the configured default without a path or
   * additional capability authority. Return immediately after true. A file
   * receiver may use this to leave its caller; only successful entry/fini and
   * complete grant cleanup suppress that normal return. Older tables lack
   * this suffix. Check struct_size before reading the pointer. */
  bool (*request_default)(void);
  /* Optional explicitly owner-selected resident host/foreground lifecycle.
   * Copies a tagged client; unavailable to legacy profiles, admitted legacy
   * invocations, and during init/fini.
   * See RiscResidentShellV1.h. */
  bool (*resident_shell)(struct risc_resident_client_v1* out);
  /* Optional copied native failure evidence, configured default/host only.
   * No storage or hardware authority. Reading never acknowledges presentation.
   * Older tables lack this suffix. See RiscFailureEvidenceV1.h. */
  bool (*failure_evidence)(struct risc_failure_evidence_client_v1* out);
  /* Optional copied, invocation-scoped RAM diagnostic checkpoint.
   * Check table size before reading; see RiscDiagnosticCheckpointV1.h. */
  bool (*diagnostic_checkpoint_client)(struct risc_diagnostic_checkpoint_client_v1* out);
  /* Optional inexpensive boot-local clock, wrapping modulo 2^32 milliseconds.
   * Active owner only, including init/fini. No heap query, hardware identity,
   * provider polling or I/O. Check struct_size before reading this suffix. */
  bool (*monotonic_ms)(uint32_t* out);
  /* Read-only native host cancellation. A true result remains true until the
   * host completes teardown; reading is not acknowledgement. Finish bounded
   * cleanup and return from app_main; do not queue another app. Native recovery
   * may restart when cleanup/nonresponse exceeds its board-selected deadline.
   * Defaults/Home retain their normal short-key behavior. Check suffix size. */
  bool (*exit_requested)(void);
  /* Configured default, app_main only, healthy owner. Copies and consumes one
   * recoverable child failure after complete teardown. False leaves output and
   * pending evidence unchanged. No record is published across retained custody. */
  bool (*take_launch_failure)(risc_runtime_launch_failure_v1* out);
  /* Read-only: true only during the first configured-default app_main of
   * this Runtime session. False for child apps, init/fini and later returns.
   * Does not classify hardware wake; clients use retained-wake for that. */
  bool (*initial_default)(void);
  /* Optional settled native service boundary, app_main owner only. Call only
   * after presentation/other operations complete and before starting new work.
   * May perform bounded synchronous storage I/O; NEVER call from a provider,
   * pending-frame/BUSY wait, ordinary yield helper, init/fini or callback.
   * Runtime rechecks resource custody and temporarily revokes app entry while
   * dispatching. BUSY means no work admitted; retry at a later settled boundary.
   * RETAINED requires prompt return with no further provider calls or cleanup.
   * This grants no file authority. Older tables omit this optional suffix. */
  int32_t (*service_checkpoint)(void);
  /* Optional invocation-owned native-file sleep transaction. Call prepare only
   * at a settled app_main boundary, before wake/pad holds; all native file
   * handles/transfers must be idle. Runtime keeps providers mapped and freezes
   * native-file dispatch. A successful token must be resumed by this invocation
   * after ordinary sleep refusal/Light wake and AFTER restoring wake/pad holds.
   * Successful Deep entry is terminal. RETAINED forbids all further I/O/cleanup.
   * This grants no file authority and never relaxes CPU readiness checks. */
  int32_t (*sleep_prepare)(uint64_t *token);
  int32_t (*sleep_resume)(uint64_t token);
} risc_runtime_api_v1;
#define RISC_RUNTIME_CAPABILITIES_V1_SIZE (offsetof(risc_runtime_api_v1, release) + sizeof(((risc_runtime_api_v1*)0)->release))
#define RISC_RUNTIME_BOOT_CONFIRM_V1_SIZE (offsetof(risc_runtime_api_v1, confirm_boot) + sizeof(((risc_runtime_api_v1*)0)->confirm_boot))
#define RISC_RUNTIME_RETAIN_INVOCATION_V1_SIZE (offsetof(risc_runtime_api_v1, retain_invocation) + sizeof(((risc_runtime_api_v1*)0)->retain_invocation))
#define RISC_RUNTIME_TRACE_V1_SIZE (offsetof(risc_runtime_api_v1, trace) + sizeof(((risc_runtime_api_v1*)0)->trace))
#define RISC_RUNTIME_STREAM_CLIENT_V1_SIZE (offsetof(risc_runtime_api_v1, stream_client) + sizeof(((risc_runtime_api_v1*)0)->stream_client))
#define RISC_RUNTIME_DEFAULT_REQUEST_V1_SIZE (offsetof(risc_runtime_api_v1, request_default) + sizeof(((risc_runtime_api_v1*)0)->request_default))
#define RISC_RUNTIME_RESIDENT_SHELL_V1_SIZE (offsetof(risc_runtime_api_v1, resident_shell) + sizeof(((risc_runtime_api_v1*)0)->resident_shell))
#define RISC_RUNTIME_FAILURE_EVIDENCE_V1_SIZE (offsetof(risc_runtime_api_v1, failure_evidence) + sizeof(((risc_runtime_api_v1*)0)->failure_evidence))
#define RISC_RUNTIME_DIAGNOSTIC_CHECKPOINT_V1_SIZE (offsetof(risc_runtime_api_v1, diagnostic_checkpoint_client) + sizeof(((risc_runtime_api_v1*)0)->diagnostic_checkpoint_client))
#define RISC_RUNTIME_MONOTONIC_V1_SIZE (offsetof(risc_runtime_api_v1, monotonic_ms) + sizeof(((risc_runtime_api_v1*)0)->monotonic_ms))
#define RISC_RUNTIME_EXIT_REQUESTED_V1_SIZE (offsetof(risc_runtime_api_v1, exit_requested) + sizeof(((risc_runtime_api_v1*)0)->exit_requested))
#define RISC_RUNTIME_LAUNCH_FAILURE_V1_SIZE (offsetof(risc_runtime_api_v1, take_launch_failure) + sizeof(((risc_runtime_api_v1*)0)->take_launch_failure))
#define RISC_RUNTIME_INITIAL_DEFAULT_V1_SIZE (offsetof(risc_runtime_api_v1, initial_default) + sizeof(((risc_runtime_api_v1*)0)->initial_default))
#define RISC_RUNTIME_SERVICE_CHECKPOINT_V1_SIZE (offsetof(risc_runtime_api_v1, service_checkpoint) + sizeof(((risc_runtime_api_v1*)0)->service_checkpoint))
#define RISC_RUNTIME_SLEEP_V1_SIZE (offsetof(risc_runtime_api_v1, sleep_resume) + sizeof(((risc_runtime_api_v1*)0)->sleep_resume))
const risc_runtime_api_v1* risc_runtime_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif
