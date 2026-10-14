#pragma once
#include <RiscStoragePackageAsyncV1.h>
#define RISC_INTERNAL_PACKAGE_VOLUME_CAPABILITY "platform.package-volume"
/* Boot-selected native service only. The volume is confined to an internal
 * package subtree, never the active boot store, settings root or raw flash.
 * No implicit registration: the native image must explicitly select this API.
 * Caller retains this capability until volume operations and handles drain. */
typedef struct {
 uint32_t api_version,struct_size;void*context;
 const risc_storage_volume_api_v1*volume;
 bool (*exit_safe)(void*);
} risc_internal_package_volume_v1;
/* Download suffix v2 supports a copied free-space query. No writes or reused
 * native-only capacity receipt. Result.data contains uint64_t available bytes.
 * It is a conservative observation, not a reservation: write failures retain
 * the durable medium, never trigger a second destination. */
#define RISC_STORAGE_PACKAGE_FREE_BYTES 109u
