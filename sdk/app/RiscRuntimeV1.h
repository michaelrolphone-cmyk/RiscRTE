#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_RUNTIME_API_V1 1u
struct risc_stream_client_v1;
struct risc_resident_client_v1;
struct risc_failure_evidence_client_v1;
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
const risc_runtime_api_v1* risc_runtime_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif
