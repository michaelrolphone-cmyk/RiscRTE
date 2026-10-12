#ifndef RISC_KEY_VALUE_ATOMIC_V1_H
#define RISC_KEY_VALUE_ATOMIC_V1_H
/* Experimental, explicitly admitted namespace-scoped native operation API.
 * Independent of storage.key-value@1/@2. No SD acknowledgement is implied.
 * Owner calls are serialized. Backend I/O runs only on the native worker.
 * submit copies all inputs before returning OK; zero token on any refusal.
 * get is revision-checked and Busy whenever the worker owns the core.
 * cancel requests cancellation; collect is always required and can return a
 * successful committed receipt when cancellation lost the publication race.
 * A terminal receipt is consumed once. Never automatically replay Uncertain.
 */
#include <stdint.h>
#define RISC_KEY_VALUE_ATOMIC_CAPABILITY "storage.key-value.atomic"
#define RISC_KEY_VALUE_ATOMIC_API_V1 1u
#define RISC_KEY_VALUE_ATOMIC_NATIVE_DURABLE 1u
#define RISC_KEY_VALUE_ATOMIC_COPIED_ASYNC 2u
enum { RISC_ATOMIC_OK=0, RISC_ATOMIC_PENDING=1, RISC_ATOMIC_CANCELLED=2,
 RISC_ATOMIC_NOT_FOUND=-1, RISC_ATOMIC_INVALID=-2, RISC_ATOMIC_CAPACITY=-3,
 RISC_ATOMIC_IO=-4, RISC_ATOMIC_UNCERTAIN=-5, RISC_ATOMIC_CONFLICT=-6,
 RISC_ATOMIC_STALE=-7, RISC_ATOMIC_BUFFER_SMALL=-8, RISC_ATOMIC_BUSY=-9,
 RISC_ATOMIC_RETAINED=-10 };
enum { RISC_ATOMIC_PUT=1, RISC_ATOMIC_DELETE=2 };
typedef struct risc_key_value_atomic_state_v1 {
 uint32_t struct_size,flags,namespace_id,max_mutations,max_value_bytes,max_group_bytes,max_intent_bytes,reserved;
 uint64_t revision;
} risc_key_value_atomic_state_v1;
typedef struct risc_key_value_atomic_mutation_v1 {
 const char *key;uint32_t kind;const void *value;uint32_t size;
} risc_key_value_atomic_mutation_v1;
typedef struct risc_key_value_atomic_receipt_v1 {
 uint32_t struct_size;int32_t status;uint64_t revision,operation_id;
} risc_key_value_atomic_receipt_v1;
typedef struct risc_key_value_atomic_v1 {
 uint32_t api_version,struct_size;void *context;
 int32_t (*state)(void*,risc_key_value_atomic_state_v1*);
 int32_t (*get)(void*,uint64_t,const char*,void*,uint32_t,uint32_t*);
 int32_t (*submit)(void*,uint64_t,const risc_key_value_atomic_mutation_v1*,uint32_t,const void*,uint32_t,uint64_t*);
 int32_t (*collect)(void*,uint64_t,risc_key_value_atomic_receipt_v1*);
 int32_t (*cancel)(void*,uint64_t);
} risc_key_value_atomic_v1;
#endif
