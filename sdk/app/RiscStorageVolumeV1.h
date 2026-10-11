#pragma once
/* Generic removable filesystem volume capability. RiscRTE core treats this
 * interface as opaque; the provider owns the media transport and filesystem. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define RISC_STORAGE_VOLUME_API_V1 1u
#define RISC_STORAGE_VOLUME_NAME_MAX 128u

typedef uint32_t risc_storage_dir_t;
typedef uint32_t risc_storage_file_t;
#define RISC_STORAGE_DIR_INVALID 0u
#define RISC_STORAGE_FILE_INVALID 0u

typedef struct {
    char name[RISC_STORAGE_VOLUME_NAME_MAX];
    uint64_t size;
    uint8_t is_directory;
} risc_storage_dirent_v1;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    /* Poll/remount media state. True means the provider serviced the request;
     * call ready() separately to distinguish "no media" from provider error. */
    bool (*refresh)(void *context);
    bool (*ready)(void *context);
    bool (*label)(void *context, char *out, size_t capacity);
    bool (*stat)(void *context, const char *path, uint64_t *size_out,
                 bool *is_directory_out);
    risc_storage_dir_t (*dir_open)(void *context, const char *path);
    bool (*dir_next)(void *context, risc_storage_dir_t directory,
                     risc_storage_dirent_v1 *entry);
    void (*dir_close)(void *context, risc_storage_dir_t directory);
    risc_storage_file_t (*file_open_read)(void *context, const char *path,
                                          uint64_t *size_out);
    size_t (*file_read)(void *context, risc_storage_file_t file,
                        void *buffer, size_t capacity);
    /* Creation is exclusive: existing destinations are rejected. */
    risc_storage_file_t (*file_open_write)(void *context, const char *path);
    size_t (*file_write)(void *context, risc_storage_file_t file,
                         const void *buffer, size_t size);
    /* commit=false removes the incomplete destination and releases clusters. */
    bool (*file_close)(void *context, risc_storage_file_t file, bool commit);
    bool (*remove)(void *context, const char *path);
    bool (*last_error)(void *context, char *out, size_t capacity);
} risc_storage_volume_api_v1;

/* Optional, prefix-compatible extension. Check base.struct_size before access.
 * V1 consumers keep the exact layout above; API/version identity stays 1.
 * All handles are provider-owned, generation checked, and revoked on unmount.
 * dir_next false is EOF only when handle_error(directory=true) returns zero.
 * rename must reject an existing destination; callers own staging/rollback.
 * file_open handles are ordinary files: close(commit=false) still closes them;
 * abort/remove semantics apply only to the legacy exclusive file_open_write.
 * Each read/write transfers at most IO_MAX; callers loop and inspect errors.
 * Seek does not extend files. APPEND applies to every write, not just open.
 * A failed checked close retains ownership; callers must retain the provider.
 * Providers serialize operations and bound work/time with scheduler yields.
 */
enum {
    RISC_STORAGE_OPEN_READ = 1u, RISC_STORAGE_OPEN_WRITE = 2u,
    RISC_STORAGE_OPEN_CREATE = 4u, RISC_STORAGE_OPEN_TRUNCATE = 8u,
    RISC_STORAGE_OPEN_EXCLUSIVE = 16u, RISC_STORAGE_OPEN_APPEND = 32u
};
#define RISC_STORAGE_VOLUME_PATH_MAX 512u
#define RISC_STORAGE_VOLUME_IO_MAX 4096u

typedef struct {
    risc_storage_volume_api_v1 base;
    risc_storage_file_t (*file_open)(void *context, const char *path, uint32_t flags);
    bool (*file_seek)(void *context, risc_storage_file_t file, uint64_t offset);
    bool (*file_info)(void *context, risc_storage_file_t file, uint64_t *size, uint64_t *offset);
    bool (*file_sync)(void *context, risc_storage_file_t file);
    bool (*dir_rewind)(void *context, risc_storage_dir_t directory);
    bool (*dir_close_checked)(void *context, risc_storage_dir_t directory);
    uint32_t (*handle_error)(void *context, uint32_t handle, bool directory);
    bool (*mkdir)(void *context, const char *path);
    bool (*rename)(void *context, const char *source, const char *destination);
} risc_storage_volume_api_v1_ext;

/* Optional power-down barrier. Retains the mapped provider and read handles;
 * this is NOT permission to unload it. prepare rejects writers/uncertain I/O,
 * finishes synchronous media work, and blocks every subsequent volume operation.
 * cancel is legal only BEFORE the board changes pins, rails or media. Actual
 * deep sleep restarts the process and reacquires a fresh provider generation.
 * A failed prepare leaves normal admission unchanged and permits a checked retry.
 */
typedef struct {
    risc_storage_volume_api_v1_ext volume;
    bool (*prepare_power_down)(void *context);
    bool (*cancel_power_down)(void *context);
} risc_storage_volume_api_v1_power;

/* Optional terminal commit after the existing reversible prepare/cancel pair.
 * The provider owns rail/pin changes. It stays mapped with frozen read handles;
 * successful commit is terminal until reset, so cancel MUST then fail. No
 * generic runtime code may substitute a raw board power write. */
#define RISC_STORAGE_POWER_COMMIT_TAG 0x53504331u /* SPC1 */
typedef struct {
    risc_storage_volume_api_v1_power power;
    uint32_t extension_tag, extension_version;
    bool (*commit_power_down)(void *context);
} risc_storage_volume_api_v1_power_commit;
static inline const risc_storage_volume_api_v1_power_commit *risc_storage_volume_power_commit(
    const risc_storage_volume_api_v1 *api) {
    if (!api || api->api_version != RISC_STORAGE_VOLUME_API_V1 ||
        api->struct_size < sizeof(risc_storage_volume_api_v1_power_commit)) return NULL;
    const risc_storage_volume_api_v1_power_commit *p = (const risc_storage_volume_api_v1_power_commit *)api;
    return p->extension_tag == RISC_STORAGE_POWER_COMMIT_TAG && p->extension_version == 1u &&
           p->commit_power_down ? p : NULL;
}

/* Optional zero-handle sleep transaction. The entire terminal-commit prefix is
 * unchanged. This suffix does NOT make legacy commit_power_down reversible.
 * prepare_sleep rejects all open files/directories, syncs media, and freezes
 * admission. commit_sleep unmounts before changing provider-owned rails/pins.
 * resume_sleep cancels an uncommitted prepare, or checks rail/hold recovery and
 * performs normal bounded media initialization after commit. No FIL/DIR object
 * survives a power cycle; callers reopen normally and old tokens stay invalid.
 *
 * Keep the provider and dependencies mapped throughout. A false prepare/commit
 * is not permission to unload or resume I/O: call resume_sleep to establish the
 * checked outcome. READY means normal usable media; MEDIA_UNAVAILABLE means
 * rails recovered but media is not usable (ready/I/O fail, refresh may retry).
 * REFUSED means this invocation made no transition (busy, wrong owner, or a
 * legacy transaction). RETAINED means uncertain cleanup/ownership: keep every
 * remaining resource and do not admit I/O, unload, or claim recovery.
 *
 * Repeated prepare/commit is allowed in its same new transaction phase; repeated
 * resume after recovery reports current readiness without cycling the card.
 * Legacy and new transactions cannot be mixed. No callback may reset the handle
 * generation counter. Real deep sleep resets and reacquires a fresh provider.
 */
#define RISC_STORAGE_SLEEP_TAG 0x53534c31u /* SSL1 */
enum {
    RISC_STORAGE_SLEEP_READY = 0,
    RISC_STORAGE_SLEEP_MEDIA_UNAVAILABLE = 1,
    RISC_STORAGE_SLEEP_REFUSED = -1,
    RISC_STORAGE_SLEEP_RETAINED = -2
};
typedef struct {
    risc_storage_volume_api_v1_power_commit terminal;
    uint32_t sleep_tag, sleep_version;
    bool (*prepare_sleep)(void *context);
    bool (*commit_sleep)(void *context);
    int32_t (*resume_sleep)(void *context);
} risc_storage_volume_api_v1_sleep;
static inline const risc_storage_volume_api_v1_sleep *risc_storage_volume_sleep(
    const risc_storage_volume_api_v1 *api) {
    if (!risc_storage_volume_power_commit(api) ||
        api->struct_size < sizeof(risc_storage_volume_api_v1_sleep)) return NULL;
    const risc_storage_volume_api_v1_sleep *p = (const risc_storage_volume_api_v1_sleep *)api;
    return p->sleep_tag == RISC_STORAGE_SLEEP_TAG && p->sleep_version == 1u &&
        p->prepare_sleep && p->commit_sleep && p->resume_sleep ? p : NULL;
}

static inline const risc_storage_volume_api_v1_power *risc_storage_volume_power(
    const risc_storage_volume_api_v1 *api) {
    return api && api->api_version == RISC_STORAGE_VOLUME_API_V1 &&
        api->struct_size >= sizeof(risc_storage_volume_api_v1_power)
        ? (const risc_storage_volume_api_v1_power *)api : NULL;
}

static inline const risc_storage_volume_api_v1_ext *risc_storage_volume_extension(
    const risc_storage_volume_api_v1 *api) {
    return api && api->api_version == RISC_STORAGE_VOLUME_API_V1 &&
        api->struct_size >= sizeof(risc_storage_volume_api_v1_ext)
        ? (const risc_storage_volume_api_v1_ext *)api : NULL;
}

#ifdef __cplusplus
}
#endif
