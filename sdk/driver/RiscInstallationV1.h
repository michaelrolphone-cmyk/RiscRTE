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
 * protected from removal. Driver graphs are not mutated by this interface. */
typedef struct {const char*manifest_path;const char*manifest_json;} risc_install_candidate_v1;
typedef struct {int32_t status;char launch_path[193];} risc_install_result_v1;
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
} risc_installation_v1;
#ifdef __cplusplus
}
#endif
