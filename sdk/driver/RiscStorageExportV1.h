#pragma once
/* Optional exclusive raw-media transaction after the unchanged storage.volume
 * sleep prefix. No filesystem or second transport may run during this lease.
 * Invoke every callback on the provider's normal owner task, never USB/ISR
 * context. Retain the provider and its dependencies until end returns READY or
 * MEDIA_UNAVAILABLE. REFUSED changes no ownership; RETAINED means uncertain
 * custody and forbids retrying I/O, reopening files, or unloading the provider.
 *
 * begin requires zero caller file/directory handles, flushes/pauses logging,
 * checks media sync and unregisters FatFs before returning physical capacity.
 * It may export an unformatted filesystem, but never invents capacity from a
 * partition or BPB. Zero is never a valid token. Tokens are generation-safe
 * across end/start; exhausted generation space must refuse, never wrap.
 *
 * read/write transfer whole blocks only, at most BLOCKS_MAX per call. READY
 * means the entire call completed; any partial/uncertain I/O returns RETAINED
 * and must not be retried. No automatic formatting or write retry is permitted.
 * sync checks media completion. end requires the host to have stopped all I/O
 * (eject or detached endpoint), syncs and remounts, then resumes local logging.
 * READY means usable filesystem; MEDIA_UNAVAILABLE means checked local custody
 * with unusable/absent media, allowing ordinary refresh. Both consume the token.
 * Every failed unlock must report RETAINED, including a refused invocation.
 */
#include "RiscStorageVolumeV1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_STORAGE_EXPORT_TAG 0x53455831u /* SEX1 */
#define RISC_STORAGE_EXPORT_BLOCK_SIZE 512u
#define RISC_STORAGE_EXPORT_BLOCKS_MAX 8u
#define RISC_STORAGE_EXPORT_TOKEN_INVALID UINT64_C(0)
typedef uint64_t risc_storage_export_token_t;
enum {
    RISC_STORAGE_EXPORT_READY = 0,
    RISC_STORAGE_EXPORT_MEDIA_UNAVAILABLE = 1,
    RISC_STORAGE_EXPORT_PREPARING = 2,
    RISC_STORAGE_EXPORT_REFUSED = -1,
    RISC_STORAGE_EXPORT_RETAINED = -2
};
typedef struct {
    risc_storage_volume_api_v1_sleep sleep;
    uint32_t export_tag, export_version;
    int32_t (*export_begin)(void *context, risc_storage_export_token_t *token,
                            uint64_t *block_count, uint32_t *block_size);
    int32_t (*export_read)(void *context, risc_storage_export_token_t token,
                           uint64_t lba, uint32_t count, void *buffer);
    int32_t (*export_write)(void *context, risc_storage_export_token_t token,
                            uint64_t lba, uint32_t count, const void *buffer);
    int32_t (*export_sync)(void *context, risc_storage_export_token_t token);
    int32_t (*export_end)(void *context, risc_storage_export_token_t token);
} risc_storage_volume_api_v1_export;
static inline const risc_storage_volume_api_v1_export *risc_storage_volume_export(
    const risc_storage_volume_api_v1 *api) {
    if (!risc_storage_volume_sleep(api) ||
        api->struct_size < sizeof(risc_storage_volume_api_v1_export)) return NULL;
    const risc_storage_volume_api_v1_export *p = (const risc_storage_volume_api_v1_export *)api;
    return p->export_tag == RISC_STORAGE_EXPORT_TAG && p->export_version == 1u &&
        p->export_begin && p->export_read && p->export_write && p->export_sync &&
        p->export_end ? p : NULL;
}
/* Append-only checked preparation lease. begin_prepare freezes local
 * admission and ordinary log drains without exposing raw media. A nonzero
 * token remains owned through PREPARING and READY; export_end can cancel it
 * between closed transactions. prepare_step performs at most one durable log
 * transaction, or sync/unmount handoff. PREPARING never permits raw I/O.
 * Each step has its own 15-second hard transaction bound; there is no shared
 * backlog deadline that can abort a later transaction. Existing synchronous
 * export_begin may refuse a pending trace tail; preparation-aware consumers
 * use this suffix. REFUSED never proves a nonzero token was consumed.
 */
#define RISC_STORAGE_EXPORT_PREPARE_TAG UINT32_C(0x53585031)
typedef struct {
    risc_storage_volume_api_v1_export base;
    uint32_t prepare_tag, prepare_version;
    int32_t (*begin_prepare)(void *context, risc_storage_export_token_t *token);
    int32_t (*prepare_step)(void *context, risc_storage_export_token_t token,
                           uint64_t *block_count, uint32_t *block_size);
} risc_storage_volume_api_v1_export_prepare;
static inline const risc_storage_volume_api_v1_export_prepare *risc_storage_volume_export_prepare(
    const risc_storage_volume_api_v1 *api) {
    if(!risc_storage_volume_export(api) || api->struct_size<sizeof(risc_storage_volume_api_v1_export_prepare))return NULL;
    const risc_storage_volume_api_v1_export_prepare *p=(const risc_storage_volume_api_v1_export_prepare *)api;
    return p->prepare_tag==RISC_STORAGE_EXPORT_PREPARE_TAG && p->prepare_version==1 && p->begin_prepare && p->prepare_step?p:NULL;
}
#ifdef __cplusplus
}
#endif
