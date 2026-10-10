#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_FAILURE_EVIDENCE_API_V1 1u
#define RISC_FAILURE_EVIDENCE_FRAMES 8u
#define RISC_FAILURE_EVIDENCE_HISTORY 8u
enum { RISC_FAILURE_EVIDENCE_OK=0, RISC_FAILURE_EVIDENCE_NONE=1,
  RISC_FAILURE_EVIDENCE_DENIED=-1, RISC_FAILURE_EVIDENCE_INVALID=-2,
  RISC_FAILURE_EVIDENCE_STALE=-3 };
enum { RISC_FAILURE_RESET_ONLY=1, RISC_FAILURE_NATIVE_PANIC=2, RISC_FAILURE_RETENTION=3 };
enum { RISC_FAILURE_PENDING=1u, RISC_FAILURE_REGISTERS=2u,
  RISC_FAILURE_CONTEXT=4u, RISC_FAILURE_ABORT=8u };
enum { RISC_FAILURE_STACK_UNAVAILABLE=0, RISC_FAILURE_STACK_COMPLETE=1,
  RISC_FAILURE_STACK_LIMIT=2, RISC_FAILURE_STACK_INVALID=3,
  RISC_FAILURE_STACK_UNSUPPORTED=4 };
enum { RISC_FAILURE_PHASE_LOAD=1, RISC_FAILURE_PHASE_INIT=2,
  RISC_FAILURE_PHASE_MAIN=3, RISC_FAILURE_PHASE_FINI=4,
  RISC_FAILURE_PHASE_CLEANUP=5, RISC_FAILURE_PHASE_HANDOFF=6,
  RISC_FAILURE_PHASE_RETAINED=7, RISC_FAILURE_PHASE_IDLE=8 };
typedef struct { uint32_t pc, sp; } risc_failure_frame_v1;
typedef struct { uint32_t sequence, phase; uint64_t invocation; } risc_failure_step_v1;
/* Copied numeric evidence, never an SDK frame or borrowed pointer. application
 * identifies the last committed Runtime activity, not necessarily fault origin.
 * Raw exception PC is separate from backtrace PCs (SDK call-site convention).
 * Only frame_count/history_count entries are valid. Missing evidence stays
 * explicitly unavailable. RTC persistence does not survive power loss reliably. */
typedef struct {
  uint32_t api_version, struct_size;
  uint32_t record_boot, record_sequence;
  uint32_t current_reset_reason, captured_reset_hint, kind, flags;
  int32_t status;
  uint32_t core, exception, pseudo_excause, pc, sp, a0, ps, exccause, excvaddr;
  uint32_t phase, role;
  uint64_t invocation;
  char application[193], detail[192];
  uint8_t firmware_sha256[32];
  uint32_t frame_count, stack_status, history_count;
  risc_failure_frame_v1 frames[RISC_FAILURE_EVIDENCE_FRAMES];
  risc_failure_step_v1 history[RISC_FAILURE_EVIDENCE_HISTORY];
} risc_failure_evidence_v1;
typedef struct risc_failure_evidence_client_v1 {
  uint32_t api_version, struct_size;
  uint64_t invocation;
  /* Read is non-consuming, including repeated reads and failed presentation.
   * A retained current host may read its already-copied record, but cannot ack.
   * Stale tokens and calls from a different invocation are rejected. */
  int32_t (*read)(uint64_t invocation, risc_failure_evidence_v1* out);
  /* Only the healthy, active configured default/host may acknowledge its exact
   * record after confirmed presentation. This neither resets nor clears the
   * evidence. It suppresses pending presentation across fresh host invocations
   * and, for valid RTC records, subsequent supported warm resets. Idempotent. */
  int32_t (*acknowledge)(uint64_t invocation, uint32_t record_boot, uint32_t record_sequence);
} risc_failure_evidence_client_v1;
#ifdef __cplusplus
}
#endif
