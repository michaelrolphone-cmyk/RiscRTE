#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_INSTALLATION_CAPABILITY "platform.installation"
enum {RISC_INSTALL_OK=0,RISC_INSTALL_DENIED=-1,RISC_INSTALL_BUSY=-2,RISC_INSTALL_FULL=-3,RISC_INSTALL_INVALID=-4};
/* Provider-only metadata admission under the owner's installation envelope.
 * No filesystem I/O, ELF execution, hashing or implicit capability grants.
 * Native policy returns the exact launch path after validating declarations.
 * Caller must complete its package validation/storage transaction first.
 * Cached manifests can rehydrate metadata; mutable images still pass the normal
 * loader/import checks on their actual snapshot. Existing bootstrap apps are
 * protected from removal. Provider mutation is a separately opted-in suffix. */
typedef struct {const char*manifest_path;const char*manifest_json;} risc_install_candidate_v1;
typedef struct {int32_t status;char launch_path[193];} risc_install_result_v1;
/* Trusted catalog metadata, after worker image admission and durable journal.
 * Image paths select immutable SD bytes; instance comes from selected board
 * bindings, never a package permission. Native imports remain unavailable. */
typedef struct {const char*manifest_json;const char*image_path;uint64_t instance;} risc_install_provider_v1;
typedef struct {
 uint32_t api_version,struct_size;void*context;
 int32_t (*admit)(void*,const char*relative_manifest,const char*manifest_json,char*launch_path,uint32_t capacity);
 /* check_only=true proves mutability without changing policy. */
 int32_t (*forget)(void*,const char*launch_path,bool check_only);
 /* One serialized publication after complete discovery. Invalid individual
  * packages have result.status != OK. BUSY/OOM rolls back the entire registry.
  * replace=true withdraws dynamic identities absent from the complete scan. */
 int32_t (*commit)(void*,const risc_install_candidate_v1*,uint32_t count,risc_install_result_v1*,bool replace);
 /* Same candidate validation and result paths, with complete registry rollback
  * even on success. Use before persisting a bounded admitted catalog. */
 int32_t (*check)(void*,const risc_install_candidate_v1*,uint32_t count,risc_install_result_v1*,bool replace);
 /* Namespace slots are allocated durably by the trusted catalog owner, never
  * read from SD manifests. Slot zero grants no private storage. Persist the
  * identity/slot ledger (including removed identities) before check=false.
  * The boot envelope maps slots 1..namespace_capacity to a reserved range. */
 uint32_t namespace_capacity;
 int32_t (*namespaced)(void*,const risc_install_candidate_v1*,const uint32_t*slots,uint32_t count,risc_install_result_v1*,bool replace,bool check);
 /* Optional provider publication suffix. Requires boot provider_grants.
  * check_only validates the entire graph and leaves metadata unchanged.
  * Publication is atomic and I/O-free. Busy/live or retained provider slots
  * cannot be replaced. Boot-selected providers can never be replaced here.
  * Caller persists complete provider metadata before publication; cache replay
  * uses this same admission path after restart without SD scans/checksums. */
 int32_t (*providers)(void*,const risc_install_provider_v1*,uint32_t count,bool check_only);
 bool (*available)(void*,const char*capability,uint32_t api,uint64_t instance);
 uint32_t provider_capacity;
 /* Optional SD-first private KV checkpoint bridge, owner-only. Slot is the
  * existing protected namespace ledger slot. No static/shared namespaces.
  * Status 0 cold, 1 clean, 2 dirty, 3 uncertain, UINT32_MAX unavailable.
  * Restore/snapshot/ack use RISC_KEY_VALUE_* statuses. SD bytes are copied by
  * the caller's worker; ack follows verified atomic replacement only. */
 uint32_t settings_capacity;
 uint32_t (*settings_status)(void*,uint32_t slot);
 int32_t (*settings_restore)(void*,uint32_t slot,const char*owner,const void*bytes,uint32_t size,bool missing);
 int32_t (*settings_snapshot)(void*,uint32_t slot,void*bytes,uint32_t capacity,uint32_t*size,uint64_t*generation);
 int32_t (*settings_ack)(void*,uint32_t slot,uint64_t generation);
} risc_installation_v1;
#define RISC_INSTALLATION_PROVIDER_PREFIX_SIZE offsetof(risc_installation_v1,settings_capacity)
#define RISC_INSTALLATION_APP_PREFIX_SIZE offsetof(risc_installation_v1,providers)
#ifdef __cplusplus
}
#endif
