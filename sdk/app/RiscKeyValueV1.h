#ifndef RISC_KEY_VALUE_V1_H
#define RISC_KEY_VALUE_V1_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_KEY_VALUE_CAPABILITY "storage.key-value"
#define RISC_KEY_VALUE_API_V1 1u
#define RISC_KEY_VALUE_KEY_MAX 15u
#define RISC_KEY_VALUE_BLOB_MAX 64u
#define RISC_KEY_VALUE_OK 0
#define RISC_KEY_VALUE_NOT_FOUND (-1)
#define RISC_KEY_VALUE_BUFFER_SMALL (-2)
#define RISC_KEY_VALUE_INVALID (-3)
#define RISC_KEY_VALUE_CONTEXT (-4)
#define RISC_KEY_VALUE_IO (-5)
/* storage.key-value@1. Explicit positive boot-grant instance_id selects the
 * namespace; applications cannot supply a namespace. Keys are 1..15 ASCII
 * characters in [a-z0-9_.-]; values are opaque 1..64-byte blobs. There is no
 * deletion, enumeration, filesystem, format, migration or defaults policy.
 * Calls require the active invocation's owner task and a live grant. All table
 * pointers become invalid at release/revocation. A copied callback/context pair
 * also becomes invalid and must return CONTEXT (generation tokens never reused).
 * This is lifecycle enforcement for trusted native code, not memory isolation.
 * get: out_size is mandatory; set to 0 on error except BUFFER_SMALL, which gives
 * the required size. NULL buffer with capacity 0 is a size probe; a present
 * value returns BUFFER_SMALL. NULL with nonzero capacity is INVALID. No error
 * returns partial data. SDK-reported missing is NOT_FOUND; returned invalid or
 * oversized blobs and SDK-reported errors other than NOT_FOUND are IO. Faults
 * or type mismatches hidden by SDK lookup/recovery can appear as missing.
 * put: OK means backend commit plus exact readback succeeded. IO may mean the
 * new value persisted or did not persist; it NEVER promises the old value.
 * App encoding, schema versions, validation and fail-closed defaults are app
 * responsibilities. No durability across erase/reflash is promised. */
typedef struct risc_key_value_v1 {
  uint32_t api_version;
  uint32_t struct_size;
  void* context;
  int32_t (*get)(void* context, const char* key, void* buffer,
                 uint32_t capacity, uint32_t* out_size);
  int32_t (*put)(void* context, const char* key, const void* data, uint32_t size);
} risc_key_value_v1;
#ifdef __cplusplus
}
#endif
#endif
