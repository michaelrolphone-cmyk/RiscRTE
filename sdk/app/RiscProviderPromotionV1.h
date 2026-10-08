#pragma once
#include <stdint.h>
#define RISC_PROVIDER_PROMOTION_CAPABILITY "runtime.provider-promotion"
#define RISC_PROVIDER_PROMOTION_API_V1 1u
#ifdef __cplusplus
extern "C" {
#endif
enum { RISC_PROVIDER_PROMOTION_OK=0, RISC_PROVIDER_PROMOTION_ALREADY_READY=1,
       RISC_PROVIDER_PROMOTION_CONTEXT=-1, RISC_PROVIDER_PROMOTION_BUSY=-2,
       RISC_PROVIDER_PROMOTION_FAILED=-3, RISC_PROVIDER_PROMOTION_RETAINED=-4 };
/* Explicit instance-zero grant to the configured default app only. Calls require
 * owner task, app_main, a live grant and safe native/graph custody. promote pins
 * every already validated selected provider for this boot session, in dependency
 * order. No selector, import, reload, policy change or demotion is available.
 * FAILED preserves successful prefix pins; retry resumes there. RETAINED fences
 * further calls and app teardown until restart. Copy the table, never retain the
 * borrowed table pointer after release; copied stale contexts always reject. */
typedef struct risc_provider_promotion_api_v1 {
 uint32_t api_version, struct_size;
 void* context;
 int32_t (*promote)(void*);
} risc_provider_promotion_api_v1;
#ifdef __cplusplus
}
#endif
