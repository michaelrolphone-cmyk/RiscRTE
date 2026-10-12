#pragma once
#include <RiscStoragePackageAsyncV1.h>

/* Optional, additive suffix. Every async/package/download prefix is frozen. */
#define RISC_STORAGE_SESSION_ADMISSION_TAG UINT32_C(0x53414431)
#define RISC_STORAGE_SESSION_ADMISSION_VERSION 1u
typedef struct {
    risc_storage_volume_api_v1_download base;
    uint32_t admission_tag, admission_version;
    /* Warm admission is an owner-task, serialized, metadata-only transaction.
     * It may follow completed ordinary file/directory/export/sleep use. It
     * must refuse live ordinary handles/writers/directories, an active async
     * session/operation, retained custody and export/sleep/lifecycle ownership
     * without closing, draining, remounting or changing that custody. On any
     * refusal *session is zero; all provider/session state remains unchanged.
     *
     * Native caller prerequisite: first attach an idle selected raw mailbox,
     * verify no selected Sync lock is held, and fence new ordinary native
     * calls. Keep that fence and provider/dependencies pinned until checked
     * async end returns OK. PENDING/BUSY/RETAINED do not release ownership.
     * The provider calls Sync is_owner exactly once and never locks/unlocks
     * the ordinary operation guard here; native attachment and owner-task
     * serialization supply that exclusion. Success publishes the provider's
     * ordinary-call session fence before return. Existing public observation
     * remains UNAVAILABLE throughout the owned session, including idle gaps.
     * Old providers without this suffix cannot attest safe warm admission.
     */
    int32_t (*begin_checked)(void *context, uint64_t *session);
} risc_storage_volume_api_v1_admission;

static inline const risc_storage_volume_api_v1_admission *
risc_storage_volume_session_admission(const risc_storage_volume_api_v1 *volume) {
    if (!risc_storage_volume_download(volume) ||
        volume->struct_size < sizeof(risc_storage_volume_api_v1_admission)) return NULL;
    const risc_storage_volume_api_v1_admission *api =
        (const risc_storage_volume_api_v1_admission *)volume;
    return api->admission_tag == RISC_STORAGE_SESSION_ADMISSION_TAG &&
        api->admission_version == RISC_STORAGE_SESSION_ADMISSION_VERSION &&
        api->begin_checked ? api : NULL;
}
