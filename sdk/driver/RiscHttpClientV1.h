#pragma once
/* Provider-only HTTPS transport. No policy URLs, catalog, credentials, file
 * paths or installation behavior live in this interface. A single bounded
 * GET session is copied into native ownership; no caller pointer is retained.
 * The caller must finish close before releasing its provider or radio session. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_HTTP_CLIENT_API_V1 1u
#define RISC_HTTP_CLIENT_CAPABILITY "platform.http-client"
#define RISC_HTTP_URL_MAX 2048u
#define RISC_HTTP_CHUNK_MAX 512u
#define RISC_HTTP_MAX_BYTES (8u*1024u*1024u)
enum {
  RISC_HTTP_OK=0, RISC_HTTP_AGAIN=1, RISC_HTTP_EOF=2,
  RISC_HTTP_INVALID=-1, RISC_HTTP_BUSY=-2, RISC_HTTP_NETWORK=-3,
  RISC_HTTP_MEMORY=-4, RISC_HTTP_TRANSPORT=-5, RISC_HTTP_TIMEOUT=-6,
  RISC_HTTP_SIZE=-7, RISC_HTTP_STATUS=-8, RISC_HTTP_CLOSED=-9,
  RISC_HTTP_RETAINED=-10, RISC_HTTP_CLOCK=-11
};
typedef struct {
  uint32_t struct_size;
  const char *url;                 /* https:// only; copied before return */
  uint32_t max_bytes;              /* 1..8 MiB, enforced before delivery */
  uint32_t timeout_ms;             /* 1..300000, includes redirects */
  uint64_t utc_seconds;            /* configured UTC, 2024..2099 inclusive */
} risc_http_request_v1;
typedef struct {
  uint32_t struct_size;
  int32_t status_code;
  uint32_t received_bytes;
  int64_t content_length;          /* -1 for a bounded chunked response */
} risc_http_response_v1;
typedef struct {
  uint32_t api_version,struct_size;
  void *context;
  int32_t (*open)(void*,const risc_http_request_v1*,uint64_t*);
  int32_t (*read)(void*,uint64_t,void*,uint32_t,uint32_t*);
  int32_t (*info)(void*,uint64_t,risc_http_response_v1*);
  /* Idempotent for the last closed handle; failure retains native ownership. */
  int32_t (*close)(void*,uint64_t);
} risc_http_client_v1;
#ifdef __cplusplus
}
#endif
