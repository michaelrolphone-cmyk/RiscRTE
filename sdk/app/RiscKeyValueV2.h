#ifndef RISC_KEY_VALUE_V2_H
#define RISC_KEY_VALUE_V2_H
#include "RiscKeyValueV1.h"
#define RISC_KEY_VALUE_API_V2 2u
#define RISC_KEY_VALUE_V2_BLOB_MAX 2048u
/* storage.key-value@2 has the same function-table layout, namespace authority,
 * key grammar, status codes, lifecycle and uncertain-put semantics as @1.
 * Its sole extension is opaque values of 1..2048 bytes. Explicit @2 admission
 * is required; @1 remains capped at 64 bytes even on a larger backend.
 * Each value is independently committed and exactly read back. There are no
 * cross-key transactions, enumeration, deletion, schema or migration policies.
 * The backend must advertise this bound before @2 can be admitted. */
typedef risc_key_value_v1 risc_key_value_v2;
#endif
