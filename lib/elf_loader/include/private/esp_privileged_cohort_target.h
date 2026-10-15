#pragma once
/* Actual native resident boundary. Keep synchronized with the pinned producer
 * SDK probe; unrelated partition/settings/product options are intentionally
 * absent. There is no HAL structure passed between provider and resident. */
#include <stddef.h>
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "hal/cache_ll.h"
#include "soc/soc_caps.h"
#if !CONFIG_IDF_TARGET_ESP32S3 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(6,1,0)
#error "Unqualified private OS/CPU ABI 2 target or SDK"
#endif
#if __GNUC__ != 15 || __GNUC_MINOR__ != 2 || __GNUC_PATCHLEVEL__ != 0
#error "Unqualified private OS/CPU ABI 2 compiler"
#endif
#if CONFIG_FREERTOS_UNICORE || CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE || CONFIG_FREERTOS_SMP
#error "Private OS/CPU ABI 2 requires the qualified dual-core non-SMP port"
#endif
#if CONFIG_LOG_VERSION != 1 || CONFIG_LOG_DEFAULT_LEVEL != 3 || CONFIG_LOG_MAXIMUM_LEVEL != 3 || !CONFIG_LOG_TIMESTAMP_SOURCE_RTOS
#error "Private OS/CPU ABI 2 logging configuration changed"
#endif
#if !CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND
#error "Private OS/CPU ABI 2 requires the SDK PSRAM atomic workaround"
#endif
#ifdef __cplusplus
#define RISC_COHORT_ASSERT static_assert
#define RISC_COHORT_ALIGN alignof
#else
#define RISC_COHORT_ASSERT _Static_assert
#define RISC_COHORT_ALIGN _Alignof
#endif
/* Absent SDK bools are zero without changing SDK macros in this unit. */
#ifdef CONFIG_FREERTOS_UNICORE
#define RISC_COHORT_UNICORE CONFIG_FREERTOS_UNICORE
#else
#define RISC_COHORT_UNICORE 0
#endif
#ifdef CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE
#define RISC_COHORT_SINGLE_CORE CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE
#else
#define RISC_COHORT_SINGLE_CORE 0
#endif
#ifdef CONFIG_FREERTOS_SMP
#define RISC_COHORT_SMP CONFIG_FREERTOS_SMP
#else
#define RISC_COHORT_SMP 0
#endif
#ifdef SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
#define RISC_COHORT_MEMORY_CACHED SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
#else
#define RISC_COHORT_MEMORY_CACHED 0
#endif
#define RISC_NATIVE_COHORT_VALUE(name, expression, expected) \
    RISC_COHORT_ASSERT((expression) == (expected), "native ABI 2 profile: " #name);
#include "privileged_native_cohort_v2.def"
#undef RISC_NATIVE_COHORT_VALUE
#undef RISC_COHORT_ALIGN
#undef RISC_COHORT_ASSERT
#undef RISC_COHORT_UNICORE
#undef RISC_COHORT_SINGLE_CORE
#undef RISC_COHORT_SMP
#undef RISC_COHORT_MEMORY_CACHED
