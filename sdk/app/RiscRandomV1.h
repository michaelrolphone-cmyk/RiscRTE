#pragma once
#include <stdint.h>
#define RISC_RANDOM_CAPABILITY "random.bytes"
#define RISC_RANDOM_MAX_BYTES 256u
enum {RISC_RANDOM_OK=0,RISC_RANDOM_INVALID=-1,RISC_RANDOM_CONTEXT=-2,RISC_RANDOM_UNAVAILABLE=-3};
/* Native random source; explicit app grant, owner task and invocation lifetime.
 * Fills the entire buffer or reports failure. No deterministic fallback. */
typedef struct {uint32_t api_version,struct_size;void* context;int32_t (*fill)(void*,void*,uint32_t);} risc_random_api_v1;
