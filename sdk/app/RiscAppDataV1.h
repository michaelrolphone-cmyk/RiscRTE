#ifndef RISC_APP_DATA_V1_H
#define RISC_APP_DATA_V1_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_APP_DATA_CAPABILITY "storage.app-data"
/* Same table, explicit boot-scoped single-file shared authority. A grant
 * additionally requires "file": an exact valid basename. Each namespace can
 * be shared only by explicitly admitted apps; ordinary app-data ownership and
 * all revision, quota, lifetime and retention rules remain unchanged. */
#define RISC_SHARED_DATA_CAPABILITY "storage.shared-data"
#define RISC_APP_DATA_API_V1 1u
#define RISC_APP_DATA_NAME_MAX 48u
#define RISC_APP_DATA_FILE_MAX 65536u
#define RISC_APP_DATA_FILES_MAX 4u
#define RISC_APP_DATA_NAMESPACE_MAX 131072u
#define RISC_APP_DATA_OK 0
#define RISC_APP_DATA_NOT_FOUND (-1)
#define RISC_APP_DATA_BUFFER_SMALL (-2)
#define RISC_APP_DATA_INVALID (-3)
#define RISC_APP_DATA_CONTEXT (-4)
#define RISC_APP_DATA_UNAVAILABLE (-5)
#define RISC_APP_DATA_IO (-6)
#define RISC_APP_DATA_NO_SPACE (-7)
#define RISC_APP_DATA_COMMIT_UNKNOWN (-8)
#define RISC_APP_DATA_RETAINED (-9)
#define RISC_APP_DATA_STALE (-10)
#define RISC_APP_DATA_BUSY (-11) /* No I/O began; retry at a later settled tick. */
/* Complete ordinary files, independent of the immutable installed store and NVS.
 * Explicit positive boot-grant instance_id binds one namespace. Callers cannot
 * supply a namespace, native path, partition, executable or format operation.
 * Names: 1..48 ASCII [A-Za-z0-9._-], first character alphanumeric. Empty files
 * are valid. Bounds are engineering limits of this version, not app schemas.
 * Four committed files and 128 KiB committed bytes per namespace; one private
 * stage of at most 64 KiB is additional atomic-write headroom. Quota is a limit,
 * not reserved capacity: a shared full physical volume returns NO_SPACE.
 * stat/read: mandatory size output is zero on error except BUFFER_SMALL. A
 * NULL/0 read is a probe: nonempty files return BUFFER_SMALL and exact size;
 * empty files return OK/0. A nonzero capacity requires a buffer. Read never
 * returns partial output, even on short I/O, cleanup failure or corruption.
 * NOT_FOUND means confirmed file absence on a ready mounted volume only.
 * replace: OK means stage sync/close, exact verification, atomic replacement
 * and destination verification succeeded. Prior content survives all failures
 * before rename. COMMIT_UNKNOWN means old OR complete new file may persist;
 * callers must reload, not assume rollback. RETAINED means cleanup is uncertain:
 * all further I/O, app teardown and sleep must stop until explicit restart.
 * No automatic retry, defaults, JSON/schema migration or persistence through
 * erase/full-device reflash is promised. Synchronous bounded chunks yield;
 * deadlines cannot interrupt a stalled filesystem or flash operation.
 * Revisions are opaque mount-session tokens. stat/read report a nonzero token
 * for a present file, including an empty one; absence reports zero. read and
 * replace require the expected token. replace(0) physically rechecks absence.
 * Tokens are never reused within a mount, including release/reacquire. A write
 * to any namespace conservatively invalidates all earlier tokens. Any ambiguous
 * rename also invalidates them. Reboot requires a fresh stat/read. STALE never
 * mutates committed data or returns partial output.
 * Owner task, live generation and safe native state are required on every call.
 * Copied callbacks fail CONTEXT after release/revocation; no pointers survive.
 */
typedef struct risc_app_data_v1 {
  uint32_t api_version, struct_size;
  void* context;
  int32_t (*stat)(void*,const char*,uint32_t*,uint64_t*);
  int32_t (*read)(void*,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*);
  int32_t (*replace)(void*,const char*,uint64_t,const void*,uint32_t);
} risc_app_data_v1;
#ifdef __cplusplus
}
#endif
#endif
