#pragma once
/* Provider-only next-boot request. Content is untrusted and cannot change the
 * installed boot/board/grant policy. No raw flash, owner-NVS or reset authority. */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_PROVISION_REQUEST_CAPABILITY "platform.provision-request"
#define RISC_PROVISION_REQUEST_OK 0
#define RISC_PROVISION_REQUEST_INVALID -1
#define RISC_PROVISION_REQUEST_UNAVAILABLE -2
#define RISC_PROVISION_REQUEST_BUSY -3
#define RISC_PROVISION_REQUEST_IO -4
#define RISC_PROVISION_REQUEST_UNKNOWN -5
#define RISC_PROVISION_REQUEST_STALE -6
#define RISC_PROVISION_REQUEST_NONE 0u
#define RISC_PROVISION_REQUEST_PENDING 1u
#define RISC_PROVISION_REQUEST_CONSUMED 2u
#define RISC_PROVISION_REQUEST_BLOCKED 3u
#define RISC_PROVISION_REQUEST_PROFILE_MAX 16384u
#define RISC_PROVISION_REQUEST_TIME_MAX 384u
typedef struct {
 uint32_t struct_size;
 const void* profile_json;uint32_t profile_size;
 const void* time_json;uint32_t time_size;
 uint8_t expected_active_store_sha256[32];
} risc_provision_request_v1;
typedef struct {
 uint32_t struct_size,state,store_abi,firmware_size;
 uint8_t active_store_sha256[32],firmware_sha256[32];
 uint8_t request_sha256[32],profile_sha256[32];
 char runtime_version[32],layout[32];
 uint8_t target_store_sha256[32]; // queued immutable image digest; zero for file-stream profiles
} risc_provision_request_status_v1;
typedef struct {
 uint32_t api_version,struct_size;void*context;
 int32_t (*status)(void*,risc_provision_request_status_v1*);
 /* Copies and validates before persistence. Repeating identical profile/time
  * on the same source is idempotent. Different pending requests are refused.
  * UNKNOWN requires read-only reconciliation; never erase or blindly retry. */
 int32_t (*queue)(void*,const risc_provision_request_v1*,uint8_t request_sha256[32]);
} risc_provision_request_api_v1;
#ifdef __cplusplus
}
#endif
