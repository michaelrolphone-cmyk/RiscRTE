#pragma once
#include "RiscInstallationV1.h"
/* Trusted catalog only. Media values are durable authority in the protected
 * catalog, never fields accepted from public package manifests. The owner must
 * journal the selected medium before the first mutation and replay it exactly.
 * Zero is the legacy selected volume; one is the explicitly selected internal
 * volume. No lookup fallback occurs during load or failed I/O. */
#define RISC_INSTALLATION_MEDIA_TAG UINT32_C(0x494d5631)
enum {RISC_INSTALL_MEDIA_SD=0,RISC_INSTALL_MEDIA_INTERNAL=1};
typedef struct {
 risc_installation_v1 base;
 uint32_t media_tag,media_version,media_mask;
 int32_t (*namespaced_media)(void*,const risc_install_candidate_v1*,const uint32_t*,const uint32_t*,uint32_t,risc_install_result_v1*,bool,bool);
 int32_t (*providers_media)(void*,const risc_install_provider_v1*,const uint32_t*,uint32_t,bool);
} risc_installation_media_v1;
static inline const risc_installation_media_v1*risc_installation_media(const risc_installation_v1*a){
 if(!a||a->api_version!=1||a->struct_size<sizeof(risc_installation_media_v1))return NULL;
 const risc_installation_media_v1*m=(const risc_installation_media_v1*)a;
 return m->media_tag==RISC_INSTALLATION_MEDIA_TAG&&m->media_version==1&&m->namespaced_media&&m->providers_media?m:NULL;
}

/* Native live-loader reservation, not a cached in-use flag. The trusted catalog
 * holds the token through durable filesystem reconciliation and checked worker
 * session end. Failed/uncertain cleanup must retain the token and owner.
 * All instances of the exact identity are fenced against activation, including
 * dependency activation. Boot identities are never mutable. No grants change. */
#define RISC_INSTALLATION_DRIVER_GATE_TAG UINT32_C(0x49444731)
typedef struct {
 risc_installation_media_v1 base;
 uint32_t driver_gate_tag,driver_gate_version;
 int32_t (*reserve_driver)(void*,const char*id,uint32_t medium,uint64_t*token);
 int32_t (*release_driver)(void*,uint64_t token);
 /* Withdraw exact reserved identity after durable uninstall intent. Stable
  * slots remain tombstones for same-identity replay; dependents reject. */
 int32_t (*forget_driver)(void*,uint64_t token);
} risc_installation_driver_gate_v1;
static inline const risc_installation_driver_gate_v1*risc_installation_driver_gate(const risc_installation_v1*a){
 if(!risc_installation_media(a)||a->struct_size<sizeof(risc_installation_driver_gate_v1))return NULL;
 const risc_installation_driver_gate_v1*g=(const risc_installation_driver_gate_v1*)a;
 return g->driver_gate_tag==RISC_INSTALLATION_DRIVER_GATE_TAG&&g->driver_gate_version==1&&g->reserve_driver&&g->release_driver&&g->forget_driver?g:NULL;
}
