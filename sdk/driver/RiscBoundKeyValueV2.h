#ifndef RISC_BOUND_KEY_VALUE_V2_H
#define RISC_BOUND_KEY_VALUE_V2_H
#include "RiscBoundKeyValueV1.h"
#define RISC_BOUND_KEY_VALUE_API_V2 2u
#define RISC_BOUND_KEY_VALUE_V2_BLOB_MAX 2048u
/* storage.key-value.bound@2 preserves @1's exact 1..8-key authority map,
 * namespace/read-only enforcement, revocation, table layout and result codes.
 * Explicit @2 dependencies may use 1..2048-byte opaque values; @1 remains
 * capped at64 bytes. No wildcard keys, cross-key transaction or data policy. */
typedef risc_bound_key_value_v1 risc_bound_key_value_v2;
#endif
