#pragma once
/* Invocation-bound byte streams; the Runtime owns provider-session custody. */
#include "RiscRuntimeV1.h"
#include "RiscStreamResultV1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_STREAM_CLIENT_API_V1 1u
#define RISC_STREAM_CHUNK_V1 512u
#define RISC_STREAM_BUFFER_MAX_V1 4096u
#define RISC_STREAM_CONTROL_MAX_MS_V1 1000u
typedef uint64_t risc_stream_session_t;
typedef uint64_t risc_stream_client_handle_t;
typedef struct {
  uint32_t struct_size, reserved;
  risc_stream_session_t session;
  risc_stream_client_handle_t rx, tx;
} risc_stream_opened_v1;
typedef struct {
  uint32_t struct_size, rights, capacity, buffered, high_water;
  int32_t terminal;
  uint64_t bytes_read, bytes_written;
} risc_stream_client_info_v1;
/* All methods are owner-task/invocation bound. Open also requires the exact
 * live capability grant; session/stream handles are Runtime-issued opaque IDs.
 * Control budgets are 1..1000 ms; requests/replies are copied and <=512 bytes.
 * Read/write initialize count to zero and transfer at most min(request,512).
 * Partial progress is OK; empty/full is AGAIN; drained graceful RX is EOF.
 * Writes count accepted queue bytes, never a physical flush guarantee.
 * RETAINED requires prompt return without cleanup, polling, frees or retries. */
typedef struct risc_stream_client_v1 {
  uint32_t api_version, struct_size;
  uint64_t context; /* Runtime-minted, unique current invocation, never an owner input. */
  int32_t (*open)(uint64_t context, const risc_runtime_capability_v1 *grant,
      const void *request, uint32_t request_size, uint32_t budget_ms,
      risc_stream_opened_v1 *out);
  int32_t (*call)(uint64_t context, risc_stream_session_t session,
      const void *request, uint32_t request_size, uint32_t budget_ms,
      void *reply, uint32_t reply_capacity, uint32_t *reply_size);
  int32_t (*close)(uint64_t context, risc_stream_session_t session,
      uint32_t budget_ms);
  int32_t (*read)(uint64_t context, risc_stream_client_handle_t stream,
      void *data, uint32_t capacity, uint32_t *count);
  int32_t (*write)(uint64_t context, risc_stream_client_handle_t stream,
      const void *data, uint32_t length, uint32_t *count);
  int32_t (*info)(uint64_t context, risc_stream_client_handle_t stream,
      risc_stream_client_info_v1 *out);
} risc_stream_client_v1;
/* Obtain a copied table using Runtime.stream_client. A table copy never extends
 * its invocation. No API accepts raw provider session or endpoint IDs. */
#ifdef __cplusplus
}
#endif
