#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_RESIDENT_SHELL_API_V1 1u
#define RISC_RESIDENT_ROLE_HOST 1u
#define RISC_RESIDENT_ROLE_FOREGROUND 2u
#define RISC_RESIDENT_DESCRIPTOR_SYMBOL "risc_resident_app_descriptor_v1"
/* Export this literal data object, with default visibility, from converted
 * applications. The owner boot policy must independently select the role. */
typedef struct {
  uint32_t api_version, struct_size, role, reserved;
} risc_resident_app_descriptor_v1_t;
enum {
  RISC_RESIDENT_OK=0, RISC_RESIDENT_BUSY=1, RISC_RESIDENT_EXIT=2,
  RISC_RESIDENT_HANDOFF=3, RISC_RESIDENT_NO_PENDING=4,
  RISC_RESIDENT_DENIED=-1, RISC_RESIDENT_INVALID=-2,
  RISC_RESIDENT_INCOMPATIBLE=-3, RISC_RESIDENT_FAILED=-4,
  RISC_RESIDENT_RETAINED=-5
};
enum {
  RISC_RESIDENT_CHECKPOINT_POLL=1, RISC_RESIDENT_CHECKPOINT_CONTROLS=2,
  RISC_RESIDENT_CHECKPOINT_ALARM=3, RISC_RESIDENT_CHECKPOINT_USB=4,
  RISC_RESIDENT_CHECKPOINT_SLEEP=5, RISC_RESIDENT_CHECKPOINT_EXIT=6,
  RISC_RESIDENT_CHECKPOINT_POLICY=7
};
#define RISC_RESIDENT_POLL_ACTIVITY 1u
#define RISC_RESIDENT_POLL_INHIBIT_IDLE 2u
#define RISC_RESIDENT_POLL_INHIBIT_POLICY 4u
#define RISC_RESIDENT_REPLY_REDRAW 1u
#define RISC_RESIDENT_REPLY_CONFIGURATION_CHANGED 2u
#define RISC_RESIDENT_REPLY_POLICY_REQUEST 4u
/* Request flags are valid only for POLL. ACTIVITY is latched physical input;
 * INHIBIT_IDLE suppresses inactivity handling; INHIBIT_POLICY prevents policy
 * work while capture or another app-owned operation is live. A lightweight
 * POLL preserves child input/focus. Clear latched activity only after OK;
 * BUSY leaves pending client state intact. Unknown/non-POLL flags are invalid.
 * A successful non-INHIBIT_POLICY POLL may reply POLICY_REQUEST. Before the
 * corresponding zero-flag POLICY checkpoint, settle writable/borrowed buffers,
 * capture, input and app-owned operations, pause background work and close
 * child touch/focus. Defer the call while unsafe. Runtime requires a pending
 * invocation-bound request; BUSY preserves it. OK POLICY, EXIT or teardown
 * consumes it; OK POLL with INHIBIT_POLICY cancels it. POLICY_REQUEST on a
 * non-POLL, inhibited or non-OK reply is an invalid host response. After clean
 * POLICY OK/BUSY, restore child input/focus and neutralize inherited input.
 * Retention allows no further provider calls. Deep sleep still requires clean
 * foreground exit; POLICY grants no terminal sleep/reset authority. */
typedef struct {
  uint32_t struct_size, reason, flags, reserved;
} risc_resident_request_v1;
typedef struct {
  uint32_t struct_size, flags;
} risc_resident_reply_v1;
enum {
  RISC_RESIDENT_FAILURE_NONE=0, RISC_RESIDENT_FAILURE_LOAD=1,
  RISC_RESIDENT_FAILURE_ABI=2, RISC_RESIDENT_FAILURE_INIT=3,
  RISC_RESIDENT_FAILURE_CLEANUP=4, RISC_RESIDENT_FAILURE_RETAINED=5,
  RISC_RESIDENT_FAILURE_PRIOR_RESET=6, RISC_RESIDENT_FAILURE_ALLOCATION=7
};
typedef struct {
  uint32_t struct_size, kind;
  int32_t status;
  uint32_t native_reason;
  uint64_t invocation;
  char application[193], detail[192];
} risc_resident_failure_v1;
typedef struct {
  uint32_t struct_size;
  int32_t status;
  uint64_t invocation;
  risc_resident_failure_v1 failure;
} risc_resident_result_v1;
typedef struct {
  uint32_t api_version, struct_size;
  void* context;
  /* Runs only at an explicit, settled foreground checkpoint, under host
   * authority. Complete presentation/restoration and neutralize consumed input
   * before returning OK. BUSY means no focus/state change was committed.
   * EXIT requests clean foreground return. Any uncertain cleanup must call
   * retain_invocation and return RETAINED; no further UI/provider work is safe. */
  int32_t (*dispatch)(void*,const risc_resident_request_v1*,risc_resident_reply_v1*);
  /* Optional notification after a failed child has been completely cleaned up.
   * Terminal retention never invokes this callback. */
  void (*failed)(void*,const risc_resident_failure_v1*);
  /* Optional complete suffix. Runs before each admitted foreground load or
   * destructive legacy handoff, after the previous child has fully unloaded,
   * under the live host's authority. relative_path is a copied NUL-terminated
   * boot-store path (at most 192 bytes), borrowed only for this call. Present
   * and settle the complete loading frame before OK; Runtime supplies no
   * percentage or first-frame completion signal. BUSY refuses this load cleanly
   * without a failed callback and discards pending chain/file-open state.
   * RETAINED (or any other result) fences both invocations
   * and prevents further loading/UI/provider calls. No recursive launch, exit
   * request or terminal sleep is permitted in this callback. */
  int32_t (*loading)(void*,const char* relative_path);
} risc_resident_callbacks_v1;
#define RISC_RESIDENT_CALLBACKS_V1_SIZE offsetof(risc_resident_callbacks_v1, loading)
#define RISC_RESIDENT_CALLBACKS_LOADING_V1_SIZE (offsetof(risc_resident_callbacks_v1, loading) + sizeof(((risc_resident_callbacks_v1*)0)->loading))
typedef struct risc_resident_client_v1 {
  uint32_t api_version, struct_size;
  uint64_t invocation;
  uint32_t role, reserved;
  int32_t (*register_shell)(uint64_t,const risc_resident_callbacks_v1*);
  /* Host entry only; one synchronous admitted foreground chain. Foreground
   * descriptors are required and host memory remains live. No recursive launch
   * from dispatch. An explicitly admitted resident_shell.legacy path instead
   * queues a destructive handoff and returns HANDOFF after clean child cleanup:
   * return app_main promptly, without restoring focus/rendering. Only after
   * successful host fini/grant/memory/unload does the legacy ELF start.
   * On fresh host entry, pass NULL after register_shell to resume a copied
   * foreground/file.open continuation from a legacy invocation. NO_PENDING
   * means no continuation (invocation=0); otherwise ordinary results apply.
   * Retention always returns RETAINED, never HANDOFF. */
  int32_t (*run_foreground)(uint64_t,const char*,risc_resident_result_v1*);
  /* Foreground entry only. Call after releasing writable frames and borrowed
   * buffers and settling app/provider operations; never from arbitrary yield.
   * Runtime cannot inspect opaque provider-owned surfaces. */
  int32_t (*checkpoint)(uint64_t,const risc_resident_request_v1*,risc_resident_reply_v1*);
  /* Host dispatch only. The foreground must return at the next checkpoint;
   * this is cooperative and does not terminate a native stack. */
  int32_t (*request_foreground_exit)(uint64_t);
  /* Copies the most recent current-session failure, or a read-only prior-reset
   * record from the native port. Never causes cleanup, I/O, reboot or recovery.
   * Available to the current host even after terminal retention. */
  bool (*last_failure)(uint64_t,risc_resident_failure_v1*);
} risc_resident_client_v1;
#ifdef __cplusplus
}
#endif
