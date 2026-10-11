#ifndef RISC_BOUND_KEY_VALUE_V1_H
#define RISC_BOUND_KEY_VALUE_V1_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_BOUND_KEY_VALUE_CAPABILITY "storage.key-value.bound"
#define RISC_BOUND_KEY_VALUE_API_V1 1u
#define RISC_BOUND_KEY_VALUE_KEY_MAX 15u
#define RISC_BOUND_KEY_VALUE_BLOB_MAX 64u
#define RISC_BOUND_KEY_VALUE_OK 0
#define RISC_BOUND_KEY_VALUE_NOT_FOUND (-1)
#define RISC_BOUND_KEY_VALUE_BUFFER_SMALL (-2)
#define RISC_BOUND_KEY_VALUE_INVALID (-3)
#define RISC_BOUND_KEY_VALUE_CONTEXT (-4)
#define RISC_BOUND_KEY_VALUE_IO (-5)
#define RISC_BOUND_KEY_VALUE_BUSY (-6) /* No I/O began; preserve cached data and retry later. */
/* Provider-only storage.key-value.bound@1. The exact boot driver selection
 * authorizes 1..8 distinct keys, each with a namespace and read/read-write
 * access. Callers supply the same key, never a namespace. Keys are 1..15 ASCII
 * [a-z0-9_.-]; values are opaque 1..64-byte blobs. No enumeration, deletion,
 * aliases, transactions, filesystem, format, migration or default policy.
 * Calls require this provider's admitted start/active lifetime on the Runtime
 * owner task; an active foreground app is not required. Unlisted keys and
 * writes to read-only keys return CONTEXT without touching the backend.
 * Authority is revoked BEFORE failed-start diagnostics, quiesce or stop.
 * Failed cleanup retains code and tables but never restores authority. Table
 * pointers expire at revocation. Copied callback/context pairs return CONTEXT
 * after revoke or Runtime replacement; generation tokens are never reused.
 * This is trusted-native lifecycle enforcement, not memory isolation.
 * get: out_size is mandatory and is zero on error except BUFFER_SMALL, which
 * reports required size. NULL/0 probes size; NULL/nonzero is INVALID. No error
 * exposes partial data. Backend missing is NOT_FOUND; invalid/oversized blobs
 * or other backend errors are IO. SDK-hidden lookup faults may appear missing.
 * put: OK means backend commit plus exact readback succeeded. IO is uncertain:
 * the new value may or may not have persisted, with no rollback promise.
 * Encodings, validation, safe defaults and any application timing policy belong
 * to the ELF. Synchronous backend calls have no hard latency guarantee. */
typedef struct risc_bound_key_value_v1 {
  uint32_t api_version;
  uint32_t struct_size;
  void* context;
  int32_t (*get)(void* context, const char* key, void* buffer,
                 uint32_t capacity, uint32_t* out_size);
  int32_t (*put)(void* context, const char* key, const void* data, uint32_t size);
} risc_bound_key_value_v1;
#ifdef __cplusplus
}
#endif
#endif
