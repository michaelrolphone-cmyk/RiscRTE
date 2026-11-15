#pragma once
/* Additive native-only terminal request, not an ELF import or app capability.
 * The complete frozen NRV1 descriptor and interface layouts stay unchanged. */
#include "RiscNativeRootV1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_DRIVER_NATIVE_TERMINAL_TAG_V1 UINT32_C(0x4e545631)
#define RISC_DRIVER_NATIVE_TERMINAL_VERSION_V1 1u
#define RISC_NATIVE_TERMINAL_API_V1 1u
#define RISC_NATIVE_TERMINAL_BUDGET_MAX_MS_V1 1000u
/* Fixed C++ underlying type makes every int32 result representable, including
 * unknown values that the host must reject without undefined behavior. */
typedef enum
#ifdef __cplusplus
 : int32_t
#endif
{
 RISC_NATIVE_TERMINAL_ACKNOWLEDGED_V1=0,
 RISC_NATIVE_TERMINAL_PENDING_V1=1,
 RISC_NATIVE_TERMINAL_REFUSED_V1=-1,
 RISC_NATIVE_TERMINAL_RETAINED_V1=-2
} risc_native_terminal_result_v1;
typedef struct {
 uint32_t api_version,struct_size;
 /* Exact mapping with its occupied native owner grant still held. Invoked at
  * most once per mapping, outside graph/stream/poll callbacks. Request no new
  * work and cooperative shutdown within the positive total deadline <=1000ms.
  * No graph reentry or capability acquisition. ACKNOWLEDGED/PENDING acknowledge
  * only the request, NOT task/PHY retirement, quiescence, joined workers or
  * permission to unmap. REFUSED is clean; uncertain custody is RETAINED.
  * Runtime retains on either failure, without interpreting provider protocols. */
 risc_native_terminal_result_v1 (*prepare_close)(uint64_t mapping_context,
                                                uint32_t budget_ms);
} risc_native_terminal_v1;
typedef struct {
 risc_driver_native_root_v1 root;
 uint32_t terminal_tag,terminal_version;
 const risc_native_terminal_v1* terminal;
} risc_driver_native_terminal_v1;
/* The base struct_size covers the complete extended descriptor. Legacy exact
 * NRV1 has no terminal request and must already be worker-safe to unload. */
#ifdef __cplusplus
}
static_assert(offsetof(risc_driver_native_terminal_v1,terminal_tag)==sizeof(risc_driver_native_root_v1),
              "Terminal follows the complete frozen NRV1 prefix");
static_assert(sizeof(risc_native_terminal_result_v1)==sizeof(int32_t),
              "Terminal result ABI is signed 32-bit");
#else
_Static_assert(sizeof(risc_native_terminal_result_v1)==sizeof(int32_t),
               "Terminal result ABI is signed 32-bit");
_Static_assert((risc_native_terminal_result_v1)-1 < 0,
               "Terminal result ABI must preserve negative statuses");
#endif
