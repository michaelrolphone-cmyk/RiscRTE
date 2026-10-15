#pragma once
/* Private build-cohort gate, not package authentication or a public ABI.
 * Native CMake pins ESP32-S3, IDF fff9895c82d744c7237be8847347bdd1b07c6643
 * and the qualified GCC15 SDK configuration. ABI 1 contains SDK4 private
 * layouts and must never be admitted merely because its names still link.
 * Future incompatible SDK/configuration cohorts require separate qualification.
 */
#include <stdbool.h>
#include <stdint.h>
#define RISC_PRIVILEGED_OS_CPU_ABI_V1 1u
#define RISC_PRIVILEGED_OS_CPU_ABI_V2 2u
#define RISC_PRIVILEGED_OS_CPU_COHORT_V2 "esp32s3-idf6.1-gcc15.2.0-20251204-v1"
static inline bool esp_elf_privileged_os_cpu_abi_supported(uint32_t abi)
{
#if defined(RISC_NATIVE_ESP_IDF) && RISC_NATIVE_ESP_IDF
    return abi == RISC_PRIVILEGED_OS_CPU_ABI_V2;
#else
    return abi == RISC_PRIVILEGED_OS_CPU_ABI_V1;
#endif
}
