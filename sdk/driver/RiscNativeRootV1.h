#pragma once
/* Native-only driver-descriptor control. Never a capability interface or an
 * app/ELF import. Access is mediated by the exact Runtime root grant.
 * Memory isolation is not claimed: installed native code is trusted. */
#include "RiscProviderV2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_DRIVER_NATIVE_ROOT_TAG_V1 UINT32_C(0x4e525631)
#define RISC_DRIVER_NATIVE_ROOT_VERSION_V1 1u
#define RISC_NATIVE_ROOT_API_V1 1u
#define RISC_NATIVE_ROOT_REQUEST_MAX_V1 2304u
#define RISC_NATIVE_ROOT_REPLY_MAX_V1 1024u
#define RISC_NATIVE_ROOT_BUDGET_MAX_MS_V1 1000u
enum {
 RISC_NATIVE_ROOT_OK=0, RISC_NATIVE_ROOT_PENDING=1,
 RISC_NATIVE_ROOT_INVALID=-1, RISC_NATIVE_ROOT_BUSY=-2,
 RISC_NATIVE_ROOT_RETAINED=-3
};
enum {
 RISC_NATIVE_ROOT_HEALTHY_V1=1u,
 RISC_NATIVE_ROOT_NATIVE_PARKED_V1=2u,
 RISC_NATIVE_ROOT_NO_APP_BORROW_V1=4u,
 RISC_NATIVE_ROOT_HEALTH_MASK_V1=7u
};
typedef struct {
 uint32_t struct_size,flags;
 uint64_t mapping_context,session_serial;
} risc_native_root_health_v1;
typedef struct {
 uint32_t api_version,struct_size;
 /* Serialized native owner only, outside all graph/stream callbacks. The
  * mapping is the complete nonreused uint64 bind_streams host context.
  * Runtime allocates increasing nonzero session serials; no wrap/reuse.
  * Typed protocol payloads contain copied semantic values, not SDK structures,
  * callbacks or borrowed pointers. Runtime/Module copy caller input/output.
  * A provider must finish its own copy before returning; neither pointer may
  * survive this call. A positive budget <=1000ms is a total deadline.
  * OK/PENDING may return <=reply_capacity bytes. INVALID/BUSY are clean
  * refusals with reply_size zero. Uncertain custody must return RETAINED.
  * No callback may reenter graph lifecycle or acquire another capability. */
 int32_t (*call)(uint64_t mapping_context,uint64_t session_serial,
   uint32_t protocol_tag,uint32_t operation,
   const void* request,uint32_t request_size,uint32_t budget_ms,
   void* reply,uint32_t reply_capacity,uint32_t* reply_size);
 /* Bounded, nonblocking, no network/storage I/O. Report exact mapping and
  * current/last native session serial. NATIVE_PARKED proves only that native
  * session receipts/time/HTTP work are drained; it never proves PHY/task
  * retirement. NO_APP_BORROW includes all endpoint/table/worker routes.
  * Runtime combines this report with exact grant/keep-set/resource checks.
  * OK returns known flags only. BUSY is clean/no output. Uncertain state is
  * RETAINED. Caller outputs are unchanged until the result is verified. */
 int32_t (*health)(uint64_t mapping_context,risc_native_root_health_v1* out);
} risc_native_root_v1;
/* Exclusive tagged suffix after the COMPLETE existing poll prefix. The root
 * owner retains diagnostics, bind_streams and poll. This slot cannot also
 * pretend to be a service or stream-session descriptor. The separate outer
 * stream provider owns its own stream-session suffix and epoch callbacks.
 * bind_streams and quiesce are mandatory; base.struct_size is the FULL size. */
typedef struct {
 risc_driver_poll_v2 poll;
 uint32_t extension_tag,extension_version;
 const risc_native_root_v1* native_root;
} risc_driver_native_root_v1;
#ifdef __cplusplus
}
static_assert(offsetof(risc_driver_native_root_v1,extension_tag)==sizeof(risc_driver_poll_v2),
              "Native root must follow the complete frozen poll prefix");
static_assert(sizeof(risc_native_root_health_v1)==24,"Fixed-width root health layout");
#endif
