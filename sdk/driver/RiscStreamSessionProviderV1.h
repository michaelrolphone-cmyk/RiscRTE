#pragma once
/* Generic provider session adapter, independent of hardware and protocols. */
#include "RiscProviderV2.h"
#include "RiscStreamResultV1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_STREAM_SESSION_PROVIDER_API_V1 1u
#define RISC_DRIVER_STREAM_SESSIONS_TAG_V1 UINT32_C(0x53535631)
#define RISC_DRIVER_STREAM_SESSIONS_VERSION_V1 1u
typedef struct {
  uint32_t struct_size, rx_endpoint, tx_endpoint, reserved;
  uint64_t session; /* Provider-owned; never returned to the app. */
} risc_provider_stream_session_v1;
/* Callbacks execute on the serialized owner task outside queue locks. The
 * positive budget is a TOTAL deadline, at most 1000 ms across all lower calls.
 * Callbacks may not retain request/reply pointers or reenter graph lifecycle.
 * Requests/replies are Runtime-owned copies, at most 512 bytes. */
typedef struct {
  uint32_t api_version, struct_size;
  /* Create a NEW provider session, never adopt an existing raw token. OK must
   * return a nonzero token and distinct READ-only RX / WRITE-only TX endpoints.
   * A clean negative failure leaves all output payload fields zero. RETAINED
   * preserves exact surviving custody (including a possibly zero token); the
   * Runtime fences immediately and never retries suspicious/uncertain cleanup. */
  int32_t (*open)(const void *request, uint32_t request_size,
      uint32_t budget_ms, risc_provider_stream_session_v1 *out);
  int32_t (*call)(uint64_t session, const void *request,
      uint32_t request_size, uint32_t budget_ms,
      void *reply, uint32_t reply_capacity, uint32_t *reply_size);
  /* Only OK proves closure. Stop pumping/revoke use before checked physical
   * closure; retain uncertain resources. Never destroy partial-open queues
   * before that closure is definite. Runtime retires a reserved pair afterward. */
  int32_t (*close)(uint64_t session, uint32_t budget_ms);
} risc_stream_session_provider_v1;
/* Append after the COMPLETE existing poll suffix. Never insert between old
 * last_error, bind_streams, or poll members. poll.streams.driver.struct_size is full.
 * Runtime retains this pointer only while the exact graph grant is pinned.
 */
typedef struct {
  risc_driver_poll_v2 poll;
  uint32_t extension_tag, extension_version;
  const risc_stream_session_provider_v1 *stream_sessions;
} risc_driver_stream_sessions_v2;
#ifdef __cplusplus
}
#endif
