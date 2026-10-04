/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <sys/errno.h>
#include "esp_idf_version.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "soc/soc.h"
#if CONFIG_IDF_TARGET_ESP32S31
#include "esp32s31/rom/cache.h"
#include "soc/cache_reg.h"
#endif
#include "private/elf_platform.h"

#if CONFIG_ELF_LOADER_LOAD_PSRAM && CONFIG_IDF_TARGET_ESP32S3 && \
    CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR && CONFIG_ELF_LOADER_CACHE_OFFSET
#define ELF_S3_RANGE_CACHE_SYNC 1
#include "esp32s3/rom/cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Cache_WriteBack_Addr must include Espressif's CACHE-126 workaround for
 * unaligned boundary lines shared with another allocation. RiscRTE uses 4.4.7. */
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(4, 4, 6) || \
    (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0) && ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 4)) || \
    (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0) && ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 1, 1))
#error "ELF PSRAM publication requires the ESP32-S3 cache writeback fix"
#endif
static DRAM_ATTR portMUX_TYPE s_elf_cache_lock = portMUX_INITIALIZER_UNLOCKED;
#endif

#ifdef CONFIG_ELF_LOADER_LOAD_PSRAM
#ifdef CONFIG_IDF_TARGET_ESP32S3
#define OFFSET_TEXT_VALUE   (SOC_IROM_LOW - SOC_DROM_LOW)
#endif
#endif

/**
 * @brief Allocate block of memory.
 *
 * @param n - Memory size in byte
 * @param exec - True: memory can run executable code; False: memory can R/W data
 *
 * @return Memory pointer if success or NULL if failed.
 */
void *esp_elf_malloc(uint32_t n, bool exec)
{
    uint32_t caps;

#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
#ifdef CONFIG_ELF_LOADER_LOAD_PSRAM
    caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
#ifdef MALLOC_CAP_EXEC
    caps = exec ? MALLOC_CAP_EXEC : MALLOC_CAP_8BIT;
#else
    caps = MALLOC_CAP_8BIT | MALLOC_CAP_32BIT;
#endif
#endif
#else
#ifdef CONFIG_ELF_LOADER_LOAD_PSRAM
    caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
    caps = MALLOC_CAP_8BIT;
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    caps |= MALLOC_CAP_CACHE_ALIGNED;
#endif
#endif

    return heap_caps_malloc(n, caps);
}

/**
 * @brief Free block of memory.
 *
 * @param ptr - memory block pointer allocated by "esp_elf_malloc"
 *
 * @return None
 */
void esp_elf_free(void *ptr)
{
    heap_caps_free(ptr);
}

/**
 * @brief Remap symbol from ".data" to ".text" section.
 *
 * @param elf  - ELF object pointer
 * @param sym  - ELF symbol table
 *
 * @return Remapped symbol value
 */
#ifdef CONFIG_ELF_LOADER_CACHE_OFFSET
uintptr_t elf_remap_text(esp_elf_t *elf, uintptr_t sym)
{
    uintptr_t mapped_sym;
    esp_elf_sec_t *sec = &elf->sec[ELF_SEC_TEXT];

    if ((sym >= sec->addr) &&
            (sym < (sec->addr + sec->size))) {
#ifdef CONFIG_ELF_LOADER_SET_MMU
        mapped_sym = sym + elf->text_off;
#else
        mapped_sym = sym + OFFSET_TEXT_VALUE;
#endif
    } else {
        mapped_sym = sym;
    }

    return mapped_sym;
}
#endif

/**
 * @brief Publish relocated code before any task can execute it.
 */
#ifdef CONFIG_ELF_LOADER_LOAD_PSRAM
int IRAM_ATTR esp_elf_arch_flush(esp_elf_t *elf)
{
#ifdef ELF_S3_RANGE_CACHE_SYNC
    if (!elf) return -EINVAL;
    const uintptr_t data = elf->sec[ELF_SEC_TEXT].addr;
    const size_t size = elf->sec[ELF_SEC_TEXT].size;
    const uintptr_t code = elf_remap_text(elf, data);
    // IDF 4.4.6 labels the narrower flash DROM window separately from
    // executable external RAM. The explicit CAM port uses that older SDK.
#if defined(RISCRTE_PROFILE_HEADLESS) && ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(4, 4, 6)
    const uintptr_t data_low = SOC_EXTRAM_DATA_LOW, data_high = SOC_EXTRAM_DATA_HIGH;
#else
    const uintptr_t data_low = SOC_DROM_LOW, data_high = SOC_DROM_HIGH;
#endif
    if (!size || data < data_low || data >= data_high ||
        size > data_high - data || code < SOC_IROM_LOW ||
        code >= SOC_IROM_HIGH || size > SOC_IROM_HIGH - code) return -EINVAL;

    /* Never write back unrelated live PSRAM: the old WriteBack_All path ran
     * with interrupts enabled, bypassing the SDK's CACHE-126 protection and
     * racing LCD ISR state. Only this unpublished text allocation needs a
     * D-bus writeback. Data sections already use the shared coherent D-cache.
     * Invalidate its I-bus alias too: a freed driver's address can be reused
     * while its previous instructions are still cached. Suspending/resuming
     * the flash caches does not invalidate those instructions. */
    const size_t chunk_max = 4096;
    const size_t yield_bytes = 32 * 1024;
    TickType_t checkpoint = xTaskGetTickCount();
    const TickType_t yield_ticks = pdMS_TO_TICKS(2) ? pdMS_TO_TICKS(2) : 1;
    size_t since_yield = 0;
    for (size_t offset = 0; offset < size;) {
        const size_t chunk = size - offset < chunk_max ? size - offset : chunk_max;
        portENTER_CRITICAL(&s_elf_cache_lock);
        int rc = Cache_WriteBack_Addr(data + offset, chunk);
        if (!rc) rc = Cache_Invalidate_Addr(code + offset, chunk);
        portEXIT_CRITICAL(&s_elf_cache_lock);
        if (rc) return -EIO;
        offset += chunk;
        since_yield += chunk;
        if (offset < size && (since_yield >= yield_bytes ||
            (TickType_t)(xTaskGetTickCount() - checkpoint) >= yield_ticks)) {
            vTaskDelay(1);
            checkpoint = xTaskGetTickCount();
            since_yield = 0;
        }
    }
    return 0;
#else
    (void)elf;
    extern void spi_flash_disable_interrupts_caches_and_other_cpu(void);
    extern void spi_flash_enable_interrupts_caches_and_other_cpu(void);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)

#if CONFIG_IDF_TARGET_ESP32S31
    /* ESP32-S31: Ranged cache APIs (Cache_WriteBack_Addr/Cache_Invalidate_Addr)
     * cause intermittent Instruction access faults (MCAUSE=0x01,
     * MEPC=0x00000000) during long-term ELF execution from PSRAM, likely
     * related to unaligned addr/size (e.g. seg_size=0x136a8, cache_line=64B).
     * Use full D-writeback + I-invalidate like other targets. */
    Cache_WriteBack_All(CACHE_MAP_L1_DCACHE);
    spi_flash_disable_interrupts_caches_and_other_cpu();
    Cache_Invalidate_All(CACHE_MAP_L1_ICACHE_MASK);
    spi_flash_enable_interrupts_caches_and_other_cpu();
    REG_CLR_BIT(CACHE_L1_ICACHE_CTRL_REG, CACHE_L1_ICACHE_SHUT_IBUS1);
#else
    extern void Cache_WriteBack_All(void);
    Cache_WriteBack_All();
    spi_flash_disable_interrupts_caches_and_other_cpu();
    spi_flash_enable_interrupts_caches_and_other_cpu();
#endif
#else
    void esp_spiram_writeback_cache(void);

    esp_spiram_writeback_cache();
    spi_flash_disable_interrupts_caches_and_other_cpu();
    spi_flash_enable_interrupts_caches_and_other_cpu();
#endif
    return 0;
#endif /* ELF_S3_RANGE_CACHE_SYNC */
}
#endif
