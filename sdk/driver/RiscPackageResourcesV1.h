#pragma once
#include "RiscStreamProviderV1.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Optional host-table suffix. Existing stream provider layout is unchanged.
 * A provider checks streams.struct_size before casting; the loader binds only
 * this module's admitted installed identity, never a provider-supplied root.
 * Resource streams are read-only and cannot be published as public endpoints.
 * Calls are synchronous, at most512 bytes; revoke denies new work and retires
 * file adapters outside the stream mutex. No ELF callback is retained.
 */
typedef struct {
  risc_stream_provider_v1 streams;
  int32_t (*open_resource)(uint64_t context, const char *relative_name, uint32_t *out);
  int32_t (*read_resource)(uint64_t context, uint32_t handle, void *out, uint32_t capacity, uint32_t *count);
  int32_t (*seek_resource)(uint64_t context, uint32_t handle, uint64_t offset);
  /* Optional suffix. The manager resolves this module's declared import index;
   * only non-executable service packs can supply read-only resource streams. */
  int32_t (*open_import)(uint64_t context, uint32_t import_index, const char *relative_name, uint32_t *out);
} risc_stream_provider_resources_v1;
#ifdef __cplusplus
}
#endif
