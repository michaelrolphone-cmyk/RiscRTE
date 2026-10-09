#pragma once
#include <stdint.h>

#define RISC_DIAGNOSTIC_SOURCE_CAPABILITY "platform.diagnostic-source"
#define RISC_DIAGNOSTIC_SOURCE_API_V1 1u
#define RISC_DIAGNOSTIC_SOURCE_MAX_SLOTS 9u
/* Buffer capacity includes the trailing NUL, written does not. */
#define RISC_DIAGNOSTIC_SOURCE_TEXT_MAX 1536u
#define RISC_DIAGNOSTIC_SOURCE_RECORD 1
#define RISC_DIAGNOSTIC_SOURCE_ABSENT 0
#define RISC_DIAGNOSTIC_SOURCE_INVALID (-1)

#ifdef __cplusplus
extern "C" {
#endif
/* Optional provider-only, owner-task native snapshot source. Declare this
 * dependency explicitly; it is never an app grant or an ELF import. The table
 * has native boot-session lifetime. No operation mutates or acknowledges the
 * source, grants storage authority, performs I/O or calls Runtime/providers.
 *
 * read copies one complete NUL-terminated record into caller-owned storage.
 * slot is 0..8; capacity is 1..1536. All pointers are required. A present record
 * has written < capacity and a nonzero revision; sequence zero is valid.
 * 1 = copied record, 0 = absent, -1 = invalid arguments/context/native result
 * (including insufficient capacity). Absence/failure clears supplied outputs;
 * invalid capacities clear only out[0] when capacity is nonzero. No pointer
 * into native storage escapes, and copied outputs survive later source changes.
 * Native hooks must be bounded, allocation-free and nonblocking. */
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    int32_t (*read)(void *context, uint32_t slot, char *out, uint32_t capacity,
                    uint32_t *written, uint64_t *sequence, uint32_t *revision);
} risc_diagnostic_source_api_v1;
#ifdef __cplusplus
}
#endif
