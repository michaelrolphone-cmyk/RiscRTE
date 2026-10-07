#pragma once
/* Provider-scoped, owner-task-only synchronization for ordinary native ELFs.
 * The runtime holds lock state; no compare-and-set targets provider PSRAM BSS.
 * This is a nonblocking reentrancy guard, not a cross-thread mutex or task API.
 * One table belongs to one selected hardware.device instance for its boot
 * session. Tokens are never reused or accepted by a different table. */
#include <stdbool.h>
#include <stdint.h>
#define RISC_PROVIDER_SYNC_API_V1 1u
#define RISC_PROVIDER_SYNC_CAPABILITY "platform.sync"
#define RISC_PROVIDER_SYNC_MAX_LOCKS 2u
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    /* False outside the owner task or while admission is fenced. */
    bool (*is_owner)(void *context);
    /* Create an initially unlocked token; failure clears token_out. */
    bool (*create)(void *context, uint64_t *token_out);
    /* Exactly one bounded attempt; no wait, allocation, callback or I/O. */
    bool (*try_lock)(void *context, uint64_t token);
    /* Owner-only cleanup remains available after admission is fenced.
     * Failed unlock/destroy retains the token and its current state. */
    bool (*unlock)(void *context, uint64_t token);
    bool (*destroy)(void *context, uint64_t token);
} risc_provider_sync_api_v1;
#ifdef __cplusplus
}
#endif
