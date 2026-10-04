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
/* Image digest is over exactly size bytes. Runtime mode additionally requires
 * ABI 1 and the exact current store digest, preventing a stale selection. */
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
} risc_bank_store_v1;
#ifdef __cplusplus
}
#endif
