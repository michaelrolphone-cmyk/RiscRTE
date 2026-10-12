#ifndef RISC_KEY_VALUE_AVAILABILITY_V1_H
#define RISC_KEY_VALUE_AVAILABILITY_V1_H
#include <stdint.h>
#define RISC_KEY_VALUE_AVAILABILITY_CAPABILITY "storage.key-value.availability"
#define RISC_KEY_VALUE_AVAILABILITY_API_V1 1u
enum { RISC_KEY_VALUE_AVAILABILITY_OK=0, RISC_KEY_VALUE_AVAILABILITY_INVALID=-1,
 RISC_KEY_VALUE_AVAILABILITY_CONTEXT=-2 };
enum { RISC_KEY_VALUE_UNAVAILABLE=1, RISC_KEY_VALUE_RECONCILIATION_PENDING=2,
 RISC_KEY_VALUE_AVAILABLE=3, RISC_KEY_VALUE_CONFLICT=4 };
/* Read-only, explicitly granted namespace status. No keys, values, paths or
 * source identity are disclosed. This capability does not grant KV access.
 * UNAVAILABLE/CONFLICT reads use temporary app defaults; durable saves are
 * refused. Never persist defaults as a repair. PENDING means a collected source
 * is waiting for the owner reconciliation boundary: every data grant/draft for
 * this namespace must be released before authority can change. A status grant
 * does not protect a draft. Keep data grants while edits need preservation.
 * Release them only after an explicit save/discard decision or a clean restart
 * of the view/invocation. Observe generation, then reacquire data and reload;
 * never merge or discard edits because a generation changed. Generation is RAM
 * only and meaningful within this Runtime lifetime. AVAILABLE says native
 * settings are authoritative; it does not promise an SD checkpoint exists.
 * Tables and copied callbacks expire at release/revocation. Owner task only. */
typedef struct risc_key_value_availability_state_v1 {
 uint32_t struct_size,namespace_id,availability,reserved;
 uint64_t generation;
} risc_key_value_availability_state_v1;
typedef struct risc_key_value_availability_v1 {
 uint32_t api_version,struct_size;void*context;
 int32_t (*snapshot)(void*,risc_key_value_availability_state_v1*);
} risc_key_value_availability_v1;
#endif
