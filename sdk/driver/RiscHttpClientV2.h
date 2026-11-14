#pragma once
/* Additive provider-only HTTP ABI. All request bytes are copied by open.
 * advance performs one bounded transport step; read drains the copied chunk.
 * A negative terminal result after sent_bytes > 0 is an uncertain remote outcome.
 * close must succeed before freeing the provider/radio; RETAINED is terminal.
 * Redirects are deliberately disabled, including authenticated GET requests. */
#include "RiscHttpClientV1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_HTTP_CLIENT_API_V2 2u
#define RISC_HTTP_V2_HEADERS_MAX 8u
#define RISC_HTTP_V2_HEADER_BYTES_MAX 2048u
#define RISC_HTTP_V2_BODY_MAX 16384u
#define RISC_HTTP_V2_RESPONSE_MAX 32768u
/* POST_PUBLIC resolves and rejects non-public addresses before connecting to
 * the checked address with the original TLS hostname. Older runtimes reject
 * this method, so callback credentials never fall back to unrestricted POST. */
enum { RISC_HTTP_GET=1, RISC_HTTP_POST=2, RISC_HTTP_POST_PUBLIC=3 };
enum { RISC_HTTP_PHASE_TIME=1, RISC_HTTP_PHASE_CONNECT=2,
       RISC_HTTP_PHASE_SEND=3, RISC_HTTP_PHASE_RECEIVE=4, RISC_HTTP_PHASE_TERMINAL=5 };
typedef struct { const char *name, *value; } risc_http_header_v2;
typedef struct {
  uint32_t struct_size, method;
  const char *url;
  const risc_http_header_v2 *headers;
  uint32_t header_count;
  const void *body;
  uint32_t body_bytes, max_response_bytes, timeout_ms;
  uint64_t utc_seconds;
  const char *time_server; /* explicit SNTP bootstrap when utc_seconds == 0 */
  uint32_t redirect_policy; /* must be zero: never forward credentials */
} risc_http_request_v2;
typedef struct {
  uint32_t struct_size, phase;
  int32_t status_code, outcome;
  uint32_t sent_bytes, received_bytes;
  int64_t content_length;
  uint32_t cleanup_retained;
  uint64_t utc_seconds;
  char etag[513], last_modified[129], content_type[129], retry_after[65];
} risc_http_response_v2;
typedef struct {
  uint32_t api_version, struct_size;
  void *context;
  int32_t (*open)(void*,const risc_http_request_v2*,uint64_t*);
  int32_t (*advance)(void*,uint64_t);
  int32_t (*read)(void*,uint64_t,void*,uint32_t,uint32_t*);
  int32_t (*info)(void*,uint64_t,risc_http_response_v2*);
  int32_t (*close)(void*,uint64_t);
} risc_http_client_v2;
#ifdef __cplusplus
}
#endif
