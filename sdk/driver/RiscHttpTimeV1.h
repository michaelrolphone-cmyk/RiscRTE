#pragma once
/* Optional append-only platform.http-client@1 suffix. Sampling supplies UTC
 * only to this HTTP session's TLS date checks; no wall clock is read or changed. */
#include <RiscHttpClientV1.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_HTTP_TIME_TAG_V1 0x48544d31u
#define RISC_HTTP_TIME_VERSION_V1 1u
#define RISC_HTTP_TIME_SERVER_MAX 253u
typedef struct {
    risc_http_client_v1 base;
    uint32_t time_tag;
    uint32_t time_version;
    /* Requires utc_seconds=0 and one explicit bounded DNS hostname.
     * Use ordinary open for valid nonzero UTC. The request and
     * server are copied before return; no caller pointer/callback is retained.
     * The returned handle uses base.read/info/close during sampling and TLS.
     * Sampling counts against the original request deadline. Close must finish
     * before releasing the provider/radio; uncertain cleanup retains ownership.
     * SNTP is unauthenticated: source/nonce validation prevents unrelated or
     * stale replies, but does not establish a trusted time authority. TLS still
     * verifies the chain, hostname and dates against this session's UTC. */
    int32_t (*open_with_time)(void*, const risc_http_request_v1*,
                             const char* explicit_server, uint64_t*);
} risc_http_client_time_v1;
static inline const risc_http_client_time_v1 *risc_http_client_time_from_v1(
        const risc_http_client_v1 *base) {
    if (!base || base->api_version != RISC_HTTP_CLIENT_API_V1 ||
        base->struct_size < sizeof(risc_http_client_time_v1) ||
        !base->open || !base->read || !base->info || !base->close) return NULL;
    const risc_http_client_time_v1 *api = (const risc_http_client_time_v1 *)base;
    return api->time_tag == RISC_HTTP_TIME_TAG_V1 &&
        api->time_version == RISC_HTTP_TIME_VERSION_V1 &&
        api->open_with_time ? api : NULL;
}
#ifdef __cplusplus
}
#endif
