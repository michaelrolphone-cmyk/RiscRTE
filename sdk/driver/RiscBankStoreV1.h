#pragma once
/* Provider-only, bounded paired firmware/bootstrap-store transaction. The native
 * owner chooses partitions and app paths; no caller-supplied partition/offset. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_BANK_STORE_CAPABILITY "platform.bank-store"
#define RISC_BANK_STORE_API_V1 1u
/* Legacy paired-layout default only, not the current layout selection.
 * Transactions must use the trusted status.store_abi of the active layout. */
#define RISC_BANK_STORE_ABI 1u
#define RISC_BANK_CHUNK_MAX 4096u
#define RISC_BANK_MANIFEST_MAX 4096u
#define RISC_BANK_APP_MAX 2097152u
#define RISC_BANK_OK 0
#define RISC_BANK_NOT_FOUND 1
#define RISC_BANK_INVALID -1
#define RISC_BANK_STATE -2
#define RISC_BANK_IO -3
#define RISC_BANK_INTEGRITY -4
#define RISC_BANK_UNAVAILABLE -5
#define RISC_BANK_TIMEOUT -6
#define RISC_BANK_RETAINED -7
#define RISC_BANK_IDLE 0u
#define RISC_BANK_COPY_FIRMWARE 1u
#define RISC_BANK_COPY_STORE 2u
#define RISC_BANK_RECEIVING 3u
#define RISC_BANK_VERIFY_FIRMWARE 4u
#define RISC_BANK_VERIFY_STORE 5u
#define RISC_BANK_READY 6u
#define RISC_BANK_ACTIVATED 7u
#define RISC_BANK_FAILED 8u
#define RISC_BANK_VERIFY_CLONE 9u
#define RISC_BANK_ACTIVATION_UNKNOWN 10u
#define RISC_BANK_REVERIFY_STORE 11u
/* Image digest is over exactly size bytes. Transactions require the current
 * status.store_abi and exact active-store digest, preventing a stale or
 * cross-layout selection. The function-table API version remains independent
 * of the paired storage/layout ABI (legacy1, explicit app-data layout2). */
typedef struct {
    uint32_t struct_size, size, store_abi;
    uint8_t sha256[32], active_store_sha256[32];
} risc_bank_image_v1;
typedef struct {
    uint32_t struct_size, state, active_bank, destination_bank;
    uint32_t done, total, firmware_capacity, store_capacity;
    int32_t error;
    uint8_t active_store_sha256[32];
    uint32_t store_abi, app_count;
    char runtime_version[32], layout[32];
} risc_bank_status_v1;
/* Owner-published cohort metadata is also present as /bootfs/cohort.json.
 * No strings may be truncated; source_revision is forty lowercase hex digits. */
typedef struct {
    uint32_t struct_size;
    char product[65], version[32], source_repo[128], source_revision[41];
} risc_bank_cohort_status_v1;
typedef struct {
    uint32_t struct_size, store_abi, firmware_size, store_size;
    uint8_t sha256[32], firmware_sha256[32], store_sha256[32], active_store_sha256[32];
    char product[65], version[32], runtime_version[32], source_repo[128], source_revision[41];
} risc_bank_cohort_v1;
typedef struct {
    uint32_t api_version, struct_size;
    void* context;
    bool (*status)(void*, risc_bank_status_v1*);
    int32_t (*begin_firmware)(void*, const risc_bank_image_v1*, uint64_t*);
    /* id is an existing immutable boot-policy app identity. Manifest may change
     * only version and descriptive fields; native owner validates all authority
     * fields and only writes that policy's existing ELF and manifest paths. */
    int32_t (*begin_app)(void*, const char* id, const void* manifest, uint32_t manifest_size,
                         const risc_bank_image_v1*, uint64_t*);
    /* One sector of cloning/readback per step; caller cooperates between calls. */
    int32_t (*step)(void*, uint64_t, risc_bank_status_v1*);
    /* Sequential copied bytes, 1..4096 per call, only in RECEIVING state. */
    int32_t (*write)(void*, uint64_t, const void*, uint32_t);
    /* Ends receive and starts independent readback/validation. Keep stepping
     * through VERIFY states until READY before explicitly activating. */
    int32_t (*finish)(void*, uint64_t);
    int32_t (*activate)(void*, uint64_t);
    /* Failed cleanup retains authority/resources. Activated transactions cannot
     * be aborted: boot selection is durable, complete the restart handoff. */
    int32_t (*abort)(void*, uint64_t);
    /* Requires ACTIVATED or ACTIVATION_UNKNOWN, closed transport/radio and safe
     * native resources. Unknown selection preserves both banks until restart.
     * False means no restart was requested. No generic reset is exported. */
    bool (*restart)(void*, uint64_t);
    /* Current admitted app manifest, index 0..app_count-1; no compiled catalog.
     * Copies <=4096 bytes; actual_size excludes any NUL (none is promised). */
    int32_t (*get_app)(void*, uint32_t index, void* manifest, uint32_t capacity,
                       uint32_t* actual_size);
    /* Optional append-only suffix. The original v1 prefix ends at get_app.
     * A cohort streams firmware_size native bytes then store_size bootfs bytes
     * through write; finish/step/activate/abort/restart keep their v1 meanings.
     * Native admission preserves the selected layout, hardware and all existing
     * persistent namespace owners. No NVS/app-data byte is an update target. */
    int32_t (*cohort_status)(void*, risc_bank_cohort_status_v1*);
    int32_t (*begin_cohort)(void*, const risc_bank_cohort_v1*, uint64_t*);
} risc_bank_store_v1;
#define RISC_BANK_STORE_V1_PREFIX_SIZE offsetof(risc_bank_store_v1, cohort_status)
#ifdef __cplusplus
}
#endif
