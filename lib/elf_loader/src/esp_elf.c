/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <inttypes.h>
#include <fcntl.h>

#include "esp_log.h"
#include "esp_elf.h"
#include "soc/soc_caps.h"

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
#include "hal/cache_ll.h"
#endif

#include "private/elf_platform.h"
#include "private/esp_elf_data_layout.h"
#include "private/esp_privileged_os_cpu.h"

#define stype(_s, _t)               ((_s)->type == (_t))
#define sflags(_s, _f)              (((_s)->flags & (_f)) == (_f))
#define ADDR_OFFSET                 (0x400)

#ifdef CONFIG_ELF_LOADER_NUMBER_SYMBOLS
#define SYMBOL_TABLES_NO            CONFIG_ELF_LOADER_NUMBER_SYMBOLS
#else
#define SYMBOL_TABLES_NO            (32)
#endif

#ifdef CONFIG_ELF_FILE_SYSTEM_BASE_PATH
#define FS_PATH                     CONFIG_ELF_FILE_SYSTEM_BASE_PATH
#else
#define FS_PATH                     "/storage"
#endif

static const char *TAG = "ELF";
static esp_elf_symbol_table_t *g_symbol_tables[SYMBOL_TABLES_NO];
static symbol_resolver current_resolver = elf_find_sym_default;
static portMUX_TYPE resolver_mux = portMUX_INITIALIZER_UNLOCKED;

/* Optional bounded Runtime recorder. No serial output, allocation or clock read
 * is performed by the loader itself. Standalone users need not define the hook.
 * Phase IDs follow Runtime's generic performance convention; failure values are
 * signed loader return codes represented as uint32_t. */
#ifdef __APPLE__
extern void risc_perf_loader_event(uint32_t phase, uint32_t value) __attribute__((weak_import));
#else
extern void risc_perf_loader_event(uint32_t phase, uint32_t value) __attribute__((weak));
#endif

static inline void elf_perf_event(uint32_t phase, uint32_t value)
{
    if (risc_perf_loader_event) risc_perf_loader_event(phase, value);
}

static int esp_elf_open_impl(elf_file_t *file, const char *name);

int esp_elf_open(elf_file_t *file, const char *name)
{
    elf_perf_event(40, 0); /* read begin: includes open, seek, allocation and yields */
    const int result = esp_elf_open_impl(file, name);
    if (result) {
        const int saved_errno = errno;
        elf_perf_event(44, (uint32_t)result);
        errno = saved_errno;
    }
    return result;
}

/**
 * @brief Open and load an ELF file into memory.
 *
 * @param file - Pointer to elf_file_t structure to store loaded file content
 * @param name - Filename (without path) of the ELF file to open
 *
 * @return 0 on success, -1 on failure with errno set. Error cases include:
 *         - Invalid parameters
 *         - Path generation failure
 *         - File open/read errors
 *         - Memory allocation failures
 *
 * @note The actual file path will be constructed as "FS_PATH/name"
 * @note Allocates memory for file content using esp_elf_malloc()
 */
// Core supplies a scoped canonical-app validator. Standalone ports retain
// loose-file loading, but must not silently bypass managed /Apps/id admission.
__attribute__((weak)) bool esp_elf_admit_managed_app(const char *path, const uint8_t *bytes, size_t size)
{
    (void)bytes; (void)size;
    return path && !(strncmp(path, "/sd/Apps/", 9) == 0 && strchr(path + 9, '/'));
}

static int esp_elf_open_impl(elf_file_t *file, const char *name)
{
    ssize_t ret;
    int fd = -1;
    char *file_path;
    off_t size;
    uint8_t *pbuf;

    if (!file || !name) {
        errno = EINVAL;
        return -1;
    }

    /* T5S3: browser supplies an absolute path through the SdFat VFS bridge. */
    ret = name[0] == '/' ? asprintf(&file_path, "%s", name)
                         : asprintf(&file_path, FS_PATH"/%s", name);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to generate path errno=%d", errno);
        return -1;
    }

    fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to open file %s errno=%d", file_path, errno);
        goto errout_open_file;
    }

    size = lseek(fd, 0, SEEK_END);
    if (size == -1) {
        ESP_LOGE(TAG, "Failed to seek file %s errno=%d", file_path, errno);
        goto errout_lseek_end;
    }

    ret = lseek(fd, 0, SEEK_SET);
    if (ret == -1) {
        ESP_LOGE(TAG, "Failed to seek file %s errno=%d", file_path, errno);
        goto errout_lseek_end;
    }

    /* Keep the reader bounded even for ports other than the SD VFS. */
    if (size < 52 || size > 8 * 1024 * 1024) {
        errno = EFBIG;
        goto errout_lseek_end;
    }
    pbuf = esp_elf_malloc(size, false);
    if (!pbuf) {
        ESP_LOGE(TAG, "Failed to malloc %" PRId64 " bytes", (int64_t)size);
        errno = ENOMEM;
        goto errout_lseek_end;
    }

    /* A synchronous media call still needs its own driver timeout; these
     * checkpoints bound repeated work and yield between bounded read requests. */
    const TickType_t read_started = xTaskGetTickCount();
    const TickType_t read_budget = pdMS_TO_TICKS(30000);
    const TickType_t yield_ticks = pdMS_TO_TICKS(2) ? pdMS_TO_TICKS(2) : 1;
    TickType_t yielded_at = read_started;
    size_t since_yield = 0;
    size_t offset = 0, reported_offset = 0;
    TickType_t reported_at = read_started;
    while (offset < (size_t)size) {
        if ((TickType_t)(xTaskGetTickCount() - read_started) >= read_budget) {
            errno = ETIMEDOUT;
            goto errout_read_fs;
        }
        const size_t remaining = (size_t)size - offset;
        const size_t chunk = remaining < 4096 ? remaining : 4096;
        ret = read(fd, pbuf + offset, chunk);
        if (ret != (ssize_t)chunk) {
            ESP_LOGE(TAG, "Failed to read ret=%zd", ret);
            goto errout_read_fs;
        }
        offset += chunk;
        const TickType_t now = xTaskGetTickCount();
        if (offset == (size_t)size || offset - reported_offset >= 256 * 1024 ||
                (TickType_t)(now - reported_at) >= pdMS_TO_TICKS(500)) {
            ESP_LOGD(TAG, "ELF read %u/%u bytes", (unsigned)offset, (unsigned)size);
            reported_offset = offset;
            reported_at = now;
        }
        /* Fast flash reads do not need a forced tick for every 4 KiB. Keep
         * scheduler/watchdog service bounded by bytes or elapsed work, as in
         * code publication. Synchronous reads keep their existing per-call
         * timeout responsibility and every chunk still checks the deadline. */
        since_yield += chunk;
        if (since_yield >= 32 * 1024 || (TickType_t)(now - yielded_at) >= yield_ticks) {
            vTaskDelay(1);
            yielded_at = xTaskGetTickCount();
            since_yield = 0;
        }
        if ((TickType_t)(xTaskGetTickCount() - read_started) >= read_budget) {
            errno = ETIMEDOUT;
            goto errout_read_fs;
        }
    }

    elf_perf_event(41, (uint32_t)size); /* read complete, bytes */
    extern bool esp_elf_validate_file(const uint8_t *, size_t);
    elf_perf_event(45, 0); /* parse existing ELF structure */
    const bool valid = esp_elf_validate_file(pbuf, size);
    if (!valid) {
        ESP_LOGE(TAG, "Unsupported or malformed native application");
        goto errout_read_fs;
    }
    elf_perf_event(46, 0); /* parse complete */
    const int closed = close(fd);
    fd = -1;
    if (closed != 0 || !esp_elf_admit_managed_app(file_path, pbuf, size)) {
        ESP_LOGE(TAG, "Managed application snapshot admission or close failed");
        goto errout_read_fs;
    }
    free(file_path);
    file->payload = pbuf;
    file->size = size;

    return 0;

errout_read_fs:
    esp_elf_free(pbuf);
errout_lseek_end:
    if (fd >= 0) close(fd);
errout_open_file:
    free(file_path);
    return -1;
}

/**
 * @brief Close ELF file and release associated resources.
 *
 * @param file - Pointer to opened elf_file_t structure
 *
 * @note Releases memory allocated by esp_elf_open() for payload data
 * @note Should be called paired with esp_elf_open() to prevent memory leaks
 * @note If file is NULL, this function does nothing (null-safe)
 */
void esp_elf_close(elf_file_t *file)
{
    if (!file) {
        return;
    }

    esp_elf_free(file->payload);
    file->payload = NULL;
    file->size = 0;
}

/**
 * @brief Find symbol address by name.
 *
 * @param sym_name - Symbol name
 *
 * @return Symbol address if success or 0 if failed.
 */
uintptr_t elf_find_sym(const char *sym_name)
{
    if (!sym_name) {
        ESP_LOGE(TAG, "Invalid parameter: sym_name is NULL");
        return 0;
    }

    /* Privileged relocation must never invoke a process-global custom
     * resolver, regardless of when another task installs or replaces it.
     * The default lookup is task-scoped and returns ONLY the fixed OS/CPU
     * inventory and libc while this task owns the privileged scope. Other
     * tasks retain the ordinary application's customizable namespace.
     * Do not modify current_resolver or hold resolver_mux across callbacks. */
    if (esp_elf_privileged_os_cpu_scope_owned_v1()) {
        return elf_find_sym_default(sym_name);
    }

    taskENTER_CRITICAL(&resolver_mux);
    symbol_resolver resolver = current_resolver;
    taskEXIT_CRITICAL(&resolver_mux);
    return resolver(sym_name);
}

#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR

/**
 * @brief Load ELF section.
 *
 * @param elf - ELF object pointer
 * @param pbuf - ELF data buffer
 *
 * @return ESP_OK if success or other if failed.
 */
static int esp_elf_load_section(esp_elf_t *elf, const uint8_t *pbuf)
{
    uint32_t entry;
    uint32_t size = 0;
    uint32_t alignments[ELF_SECS] = {0};
    uint32_t offsets[ELF_SECS] = {0};
    bool seen[ELF_SECS] = {false};
    const unsigned data_sections[] = {ELF_SEC_DATA, ELF_SEC_RODATA, ELF_SEC_DRLRO, ELF_SEC_BSS};
    int failure = -EINVAL;

    const elf32_hdr_t *ehdr = (const elf32_hdr_t *)pbuf;
    const elf32_shdr_t *shdr = (const elf32_shdr_t *)(pbuf + ehdr->shoff);
    const char *shstrab = (const char *)pbuf + shdr[ehdr->shstrndx].offset;

    /* Calculate ELF image size */

    for (uint32_t i = 0; i < ehdr->shnum; i++) {
        const char *name = shstrab + shdr[i].name;

        if (stype(&shdr[i], SHT_PROGBITS) && sflags(&shdr[i], SHF_ALLOC)) {
            if (sflags(&shdr[i], SHF_EXECINSTR) && !strcmp(ELF_TEXT, name)) {
                ESP_LOGD(TAG, ".text   sec addr=0x%08x size=0x%08x offset=0x%08x",
                         shdr[i].addr, shdr[i].size, shdr[i].offset);

                elf->sec[ELF_SEC_TEXT].v_addr  = shdr[i].addr;
                /* Heap allocation is aligned; the virtual section range must stay exact
                 * or a following unaligned .rodata address aliases into .text. */
                elf->sec[ELF_SEC_TEXT].size    = shdr[i].size;
                elf->sec[ELF_SEC_TEXT].offset  = shdr[i].offset;
                if (seen[ELF_SEC_TEXT]) return -EINVAL;
                seen[ELF_SEC_TEXT] = true;
                alignments[ELF_SEC_TEXT] = shdr[i].addralign;

                ESP_LOGD(TAG, ".text   offset is 0x%lx size is 0x%x",
                         elf->sec[ELF_SEC_TEXT].offset,
                         elf->sec[ELF_SEC_TEXT].size);
            } else if (sflags(&shdr[i], SHF_WRITE) && !strcmp(ELF_DATA, name)) {
                ESP_LOGD(TAG, ".data   sec addr=0x%08x size=0x%08x offset=0x%08x",
                         shdr[i].addr, shdr[i].size, shdr[i].offset);

                elf->sec[ELF_SEC_DATA].v_addr  = shdr[i].addr;
                elf->sec[ELF_SEC_DATA].size    = shdr[i].size;
                elf->sec[ELF_SEC_DATA].offset  = shdr[i].offset;
                if (seen[ELF_SEC_DATA]) return -EINVAL;
                seen[ELF_SEC_DATA] = true;
                alignments[ELF_SEC_DATA] = shdr[i].addralign;

                ESP_LOGD(TAG, ".data   offset is 0x%lx size is 0x%x",
                         elf->sec[ELF_SEC_DATA].offset,
                         elf->sec[ELF_SEC_DATA].size);
            } else if (!strcmp(ELF_RODATA, name)) {
                ESP_LOGD(TAG, ".rodata sec addr=0x%08x size=0x%08x offset=0x%08x",
                         shdr[i].addr, shdr[i].size, shdr[i].offset);

                elf->sec[ELF_SEC_RODATA].v_addr  = shdr[i].addr;
                elf->sec[ELF_SEC_RODATA].size    = shdr[i].size;
                elf->sec[ELF_SEC_RODATA].offset  = shdr[i].offset;
                if (seen[ELF_SEC_RODATA]) return -EINVAL;
                seen[ELF_SEC_RODATA] = true;
                alignments[ELF_SEC_RODATA] = shdr[i].addralign;

                ESP_LOGD(TAG, ".rodata offset is 0x%lx size is 0x%x",
                         elf->sec[ELF_SEC_RODATA].offset,
                         elf->sec[ELF_SEC_RODATA].size);
            } else if (!strcmp(ELF_DATA_REL_RO, name)) {
                ESP_LOGD(TAG, ".data.rel.ro sec addr=0x%08x size=0x%08x offset=0x%08x",
                         shdr[i].addr, shdr[i].size, shdr[i].offset);

                elf->sec[ELF_SEC_DRLRO].v_addr  = shdr[i].addr;
                elf->sec[ELF_SEC_DRLRO].size    = shdr[i].size;
                elf->sec[ELF_SEC_DRLRO].offset  = shdr[i].offset;
                if (seen[ELF_SEC_DRLRO]) return -EINVAL;
                seen[ELF_SEC_DRLRO] = true;
                alignments[ELF_SEC_DRLRO] = shdr[i].addralign;

                ESP_LOGD(TAG, ".data.rel.ro offset is 0x%lx size is 0x%x",
                         elf->sec[ELF_SEC_DRLRO].offset,
                         elf->sec[ELF_SEC_DRLRO].size);
            }
        } else if (stype(&shdr[i], SHT_NOBITS) &&
                   sflags(&shdr[i], SHF_ALLOC | SHF_WRITE) &&
                   !strcmp(ELF_BSS, name)) {
            ESP_LOGD(TAG, ".bss    sec addr=0x%08x size=0x%08x offset=0x%08x",
                     shdr[i].addr, shdr[i].size, shdr[i].offset);

            elf->sec[ELF_SEC_BSS].v_addr  = shdr[i].addr;
            elf->sec[ELF_SEC_BSS].size    = shdr[i].size;
            elf->sec[ELF_SEC_BSS].offset  = shdr[i].offset;
            if (seen[ELF_SEC_BSS]) return -EINVAL;
            seen[ELF_SEC_BSS] = true;
            alignments[ELF_SEC_BSS] = shdr[i].addralign;

            ESP_LOGD(TAG, ".bss    offset is 0x%lx size is 0x%x",
                     elf->sec[ELF_SEC_BSS].offset,
                     elf->sec[ELF_SEC_BSS].size);
        }
    }

    /* No .text on image */

    if (!elf->sec[ELF_SEC_TEXT].size) {
        return -EINVAL;
    }

    for (unsigned i = 0; i < ELF_SECS; ++i) {
        if (!elf->sec[i].size) continue;
        if (elf->sec[i].size > UINT32_MAX - elf->sec[i].v_addr) return -EINVAL;
        for (unsigned j = 0; j < i; ++j) {
            if (elf->sec[j].size && elf->sec[i].v_addr < elf->sec[j].v_addr + elf->sec[j].size &&
                elf->sec[j].v_addr < elf->sec[i].v_addr + elf->sec[i].size) return -EINVAL;
        }
    }
    for (unsigned i = 0; i < sizeof(data_sections) / sizeof(data_sections[0]); ++i) {
        const unsigned section = data_sections[i];
        if (elf->sec[section].size &&
            !esp_elf_data_reserve(elf->sec[section].size, alignments[section],
                                  elf->sec[section].v_addr, &size)) return -EINVAL;
    }
    if (elf->sec[ELF_SEC_TEXT].size > ESP_ELF_MAX_IMAGE_BYTES ||
        size > ESP_ELF_MAX_IMAGE_BYTES - elf->sec[ELF_SEC_TEXT].size) return -EINVAL;
    elf->ptext = esp_elf_malloc(elf->sec[ELF_SEC_TEXT].size, true);
    if (!elf->ptext) {
        ESP_LOGE(TAG, "Failed to malloc %"PRIu32" bytes for text section",
                 (uint32_t)elf->sec[ELF_SEC_TEXT].size);
        return -ENOMEM;
    }
    if (size) {
        elf->pdata = esp_elf_malloc(size, false);
        if (!elf->pdata) {
            ESP_LOGE(TAG, "Failed to malloc %"PRIu32" bytes for data section", size);
            failure = -ENOMEM;
            goto layout_failed;
        }
        uint32_t cursor = 0;
        for (unsigned i = 0; i < sizeof(data_sections) / sizeof(data_sections[0]); ++i) {
            const unsigned section = data_sections[i];
            if (!elf->sec[section].size) continue;
            if (!esp_elf_data_place((uintptr_t)elf->pdata, cursor, size,
                                    elf->sec[section].size, alignments[section],
                                    elf->sec[section].v_addr, &offsets[section])) goto layout_failed;
            elf->sec[section].addr = (uintptr_t)elf->pdata + offsets[section];
            cursor = offsets[section] + elf->sec[section].size;
        }
    }

    /* Dump ".text" from ELF to executable space memory */

    elf->sec[ELF_SEC_TEXT].addr = (Elf32_Addr)elf->ptext;
    memcpy(elf->ptext, pbuf + elf->sec[ELF_SEC_TEXT].offset,
           elf->sec[ELF_SEC_TEXT].size);

#ifdef CONFIG_ELF_LOADER_SET_MMU
    if (esp_elf_arch_init_mmu(elf)) {
        failure = -EIO;
        goto layout_failed;
    }
#endif

    /**
     * Dump ".data", ".rodata" and ".bss" from ELF to R/W space memory.
     *
     * Todo: Dump ".rodata" to rodata section by MMU/MPU.
     */

    for (unsigned i = 0; i < sizeof(data_sections) / sizeof(data_sections[0]); ++i) {
        const unsigned section = data_sections[i];
        if (!elf->sec[section].size) continue;
        uint8_t *destination = elf->pdata + offsets[section];
        if (section == ELF_SEC_BSS) memset(destination, 0, elf->sec[section].size);
        else memcpy(destination, pbuf + elf->sec[section].offset, elf->sec[section].size);
    }

    /* Set ELF entry */

    entry = ehdr->entry + elf->sec[ELF_SEC_TEXT].addr -
            elf->sec[ELF_SEC_TEXT].v_addr;

#ifdef CONFIG_ELF_LOADER_CACHE_OFFSET
    elf->entry = (void *)elf_remap_text(elf, (uintptr_t)entry);
#else
    elf->entry = (void *)entry;
#endif

    return 0;

layout_failed:
    esp_elf_free(elf->pdata);
    esp_elf_free(elf->ptext);
    elf->pdata = NULL;
    elf->ptext = NULL;
    elf->entry = NULL;
    for (unsigned i = 0; i < ELF_SECS; ++i) elf->sec[i].addr = 0;
    return failure;
}

#else

/**
 * @brief Load ELF segment.
 *
 * @param elf - ELF object pointer
 * @param pbuf - ELF data buffer
 *
 * @return ESP_OK if success or other if failed.
 */
static int esp_elf_load_segment(esp_elf_t *elf, const uint8_t *pbuf)
{
    uint32_t size;
    bool first_segment = false;
    Elf32_Addr vaddr_s = 0;
    Elf32_Addr vaddr_e = 0;

    const elf32_hdr_t *ehdr = (const elf32_hdr_t *)pbuf;
    const elf32_phdr_t *phdr = (const elf32_phdr_t *)(pbuf + ehdr->phoff);

    for (int i = 0; i < ehdr->phnum; i++) {
        if (phdr[i].type != PT_LOAD) {
            continue;
        }

        if (phdr[i].memsz < phdr[i].filesz) {
            ESP_LOGE(TAG, "Invalid segment[%d], memsz: %d, filesz: %d",
                     i, phdr[i].memsz, phdr[i].filesz);
            return -EINVAL;
        }

        if (first_segment == true) {
            vaddr_s = phdr[i].vaddr;
            vaddr_e = phdr[i].vaddr + phdr[i].memsz;
            first_segment = true;
            if (vaddr_e < vaddr_s) {
                ESP_LOGE(TAG, "Invalid segment[%d], vaddr: 0x%x, memsz: %d",
                         i, phdr[i].vaddr, phdr[i].memsz);
                return -EINVAL;
            }
        } else {
            if (phdr[i].vaddr < vaddr_e) {
                ESP_LOGE(TAG, "Invalid segment[%d], should not overlap, vaddr: 0x%x, vaddr_e: 0x%x",
                         i, phdr[i].vaddr, vaddr_e);
                return -EINVAL;
            }

            if (phdr[i].vaddr > vaddr_e + ADDR_OFFSET) {
                ESP_LOGI(TAG, "Too much padding before segment[%d], padding: %d",
                         i, phdr[i].vaddr - vaddr_e);
            }

            vaddr_e = phdr[i].vaddr + phdr[i].memsz;
            if (vaddr_e < phdr[i].vaddr) {
                ESP_LOGE(TAG, "Invalid segment[%d], address overflow, vaddr: 0x%x, vaddr_e: 0x%x",
                         i, phdr[i].vaddr, vaddr_e);
                return -EINVAL;
            }
        }

        ESP_LOGD(TAG, "LOAD segment[%d], vaddr: 0x%x, memsize: 0x%08x",
                 i, phdr[i].vaddr, phdr[i].memsz);
    }

    size = vaddr_e - vaddr_s;
    if (size == 0) {
        return -EINVAL;
    }

    elf->svaddr = vaddr_s;
    elf->psegment = esp_elf_malloc(size, true);
    if (!elf->psegment) {
        return -ENOMEM;
    }

    memset(elf->psegment, 0, size);

    /* Dump "PT_LOAD" from ELF to memory space */

    for (int i = 0; i < ehdr->phnum; i++) {
        if (phdr[i].type == PT_LOAD) {
            memcpy(elf->psegment + phdr[i].vaddr - vaddr_s,
                   (uint8_t *)pbuf + phdr[i].offset, phdr[i].filesz);
            ESP_LOGD(TAG, "Copy segment[%d], mem_addr: %p, size: 0x%08x",
                     i, (void *)((uint8_t *)elf->psegment + phdr[i].vaddr - vaddr_s), phdr[i].filesz);
        }
    }

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
    cache_ll_writeback_all(CACHE_LL_LEVEL_INT_MEM, CACHE_TYPE_DATA, CACHE_LL_ID_ALL);
#endif

    elf->entry = (void *)((uint8_t *)elf->psegment + ehdr->entry - vaddr_s);

    return 0;
}
#endif

/**
 * @brief Override the internal symbol resolver.
 * The default resolver is based on static lists that are determined by KConfig.
 * This override allows for an arbitrary implementation.
 *
 * @param resolver the resolver function
 */
void elf_set_symbol_resolver(symbol_resolver resolver)
{
    if (!resolver) {
        ESP_LOGE(TAG, "Invalid resolver: cannot set NULL resolver");
        return;
    }

    taskENTER_CRITICAL(&resolver_mux);
    current_resolver = resolver;
    taskEXIT_CRITICAL(&resolver_mux);
}

/**
 * @brief Reset the symbol resolver to the default (static tables from KConfig).
 *
 * Equivalent to elf_set_symbol_resolver(elf_find_sym_default).
 */
void elf_reset_symbol_resolver(void)
{
    elf_set_symbol_resolver(elf_find_sym_default);
}

/**
 * @brief Map symbol's address of ELF to physic space.
 *
 * @param elf - ELF object pointer
 * @param sym - ELF symbol address
 *
 * @return ESP_OK if success or other if failed.
 */
uintptr_t esp_elf_map_sym(esp_elf_t *elf, uintptr_t sym)
{
    for (int i = 0; i < ELF_SECS; i++) {
        if ((sym >= elf->sec[i].v_addr) &&
                (sym < (elf->sec[i].v_addr + elf->sec[i].size))) {
            return sym - elf->sec[i].v_addr + elf->sec[i].addr;
        }
    }

    return 0;
}

/**
 * @brief Initialize ELF object.
 *
 * @param elf - ELF object pointer
 *
 * @return ESP_OK if success or other if failed.
 */
int esp_elf_init(esp_elf_t *elf)
{
    ESP_LOGI(TAG, "ELF loader version: %d.%d.%d", ELF_LOADER_VER_MAJOR, ELF_LOADER_VER_MINOR, ELF_LOADER_VER_PATCH);

    if (!elf) {
        return -EINVAL;
    }

    memset(elf, 0, sizeof(esp_elf_t));

    return 0;
}

/**
 * @brief Decode and relocate ELF data.
 *
 * @param elf - ELF object pointer
 * @param pbuf - ELF data buffer
 *
 * @return ESP_OK if success or other if failed.
 */
/* The implementation never handles scope admission. Every public caller,
 * including ordinary dlopen and trusted provider loading, passes this entry.
 * Only the verified provider module has a one-use grant while its task owns
 * privileged import resolution; nested ordinary/same-module loads are denied.
 * All return paths (including partial mapping errors) revoke active state. */
static int esp_elf_relocate_impl(esp_elf_t *elf, const uint8_t *pbuf);

int esp_elf_relocate(esp_elf_t *elf, const uint8_t *pbuf)
{
    elf_perf_event(42, 0); /* map sections/segments, relocate, publish code */
    int result;
    if (!elf || !pbuf) {
        result = -EINVAL;
        goto perf_complete;
    }
    if (!esp_elf_privileged_os_cpu_relocation_enter_v1(elf)) {
        result = -EPERM;
        goto perf_complete;
    }
    result = esp_elf_relocate_impl(elf, pbuf);
#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
    if (result == 0) {
        // Retained by the SDK log sink for decoding dynamically loaded PCs.
        uintptr_t code = elf->sec[ELF_SEC_TEXT].addr;
#ifdef CONFIG_ELF_LOADER_CACHE_OFFSET
        code = elf_remap_text(elf, code);
#endif
        ESP_LOGI(TAG, "Mapped ELF text: runtime=0x%08lx virtual=0x%08lx size=0x%lx",
                 (unsigned long)code, (unsigned long)elf->sec[ELF_SEC_TEXT].v_addr,
                 (unsigned long)elf->sec[ELF_SEC_TEXT].size);
    }
#endif
    if (!esp_elf_privileged_os_cpu_relocation_leave_v1(elf)) result = -EIO;
perf_complete:
    elf_perf_event(43, (uint32_t)result);
    if (result) elf_perf_event(44, (uint32_t)result);
    return result;
}

static int esp_elf_relocate_impl(esp_elf_t *elf, const uint8_t *pbuf)
{
    int ret;

    const elf32_hdr_t *ehdr;
    const elf32_shdr_t *shdr;
    const char *shstrab;
    const elf32_sym_t *symtab;
    const char *strtab;

    if (!elf || !pbuf) {
        return -EINVAL;
    }

    ehdr    = (const elf32_hdr_t *)pbuf;
    shdr    = (const elf32_shdr_t *)(pbuf + ehdr->shoff);
    shstrab = (const char *)pbuf + shdr[ehdr->shstrndx].offset;

    /* Load section or segment to memory space */

#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
    ret = esp_elf_load_section(elf, pbuf);
#else
    ret = esp_elf_load_segment(elf, pbuf);
#endif

    if (ret) {
        ESP_LOGE(TAG, "Error to load elf file, ret=%d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "elf->entry=%p", elf->entry);

    /* Relocation section data */

    for (uint32_t i = 0; i < ehdr->shnum; i++) {
        if (stype(&shdr[i], SHT_RELA)) {
            uint32_t nr_reloc;
            const elf32_rela_t *rela;

            nr_reloc = shdr[i].size / sizeof(elf32_rela_t);
            rela     = (const elf32_rela_t *)(pbuf + shdr[i].offset);
            symtab   = (const elf32_sym_t *)(pbuf + shdr[shdr[i].link].offset);
            strtab   = (const char *)(pbuf + shdr[shdr[shdr[i].link].link].offset);

            ESP_LOGD(TAG, "Section %s has %d symbol tables", shstrab + shdr[i].name, (int)nr_reloc);

            for (int i = 0; i < nr_reloc; i++) {
                int type;
                uintptr_t addr = 0;
                elf32_rela_t rela_buf;

                memcpy(&rela_buf, &rela[i], sizeof(elf32_rela_t));

                const elf32_sym_t *sym = &symtab[ELF_R_SYM(rela_buf.info)];

                type = ELF_R_TYPE(rela_buf.info);
                if (type == STT_COMMON || type == STT_OBJECT || type == STT_SECTION) {
                    const char *comm_name = strtab + sym->name;

                    if (comm_name[0]) {
                        addr = elf_find_sym(comm_name);
#if CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT
                        if (!addr && sym->shndx != SHN_UNDEF) {
#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
                            addr = esp_elf_map_sym(elf, sym->value);
#else
                            addr = (uintptr_t)(elf->psegment + sym->value - elf->svaddr);
#endif
                        }
#endif
                        if (!addr) {
                            ESP_LOGE(TAG, "Can't find common %s", strtab + sym->name);
                            return -ENOSYS;
                        }

                        ESP_LOGD(TAG, "Find common %s addr=%x", comm_name, addr);
                    }
                } else if (type == STT_FILE) {
                    const char *func_name = strtab + sym->name;

                    if (sym->value) {
                        addr = esp_elf_map_sym(elf, sym->value);
                    } else {
                        addr = elf_find_sym(func_name);
                    }

#if CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT
                    if (!addr && sym->shndx != SHN_UNDEF) {
#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
                        addr = esp_elf_map_sym(elf, sym->value);
#else
                        addr = (uintptr_t)(elf->psegment + sym->value - elf->svaddr);
#endif
                    }
#endif
                    if (!addr) {
                        ESP_LOGE(TAG, "Can't find symbol %s", func_name);
                        return -ENOSYS;
                    }

                    ESP_LOGD(TAG, "Find function %s addr=%x", func_name, addr);
                }

                ret = esp_elf_arch_relocate(elf, &rela_buf, sym, addr);
                if (ret) return ret;
            }
#if CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT
        } else {
            if (strcmp((const char *)(shstrab + shdr[i].name), ELF_DYNSYM) == 0) {
                int j;
                uint32_t len;
                uint16_t num = 0;
                elf->num = 0;
                symtab   = (const elf32_sym_t *)(pbuf + shdr[i].offset);
                strtab   = (const char *)(pbuf + shdr[shdr[i].link].offset);
                for (j = 0; j < shdr[i].size / sizeof(elf32_sym_t); j++) {
                    if ((ELF_ST_BIND(symtab[j].info) == STB_GLOBAL) &&
                            (ELF_ST_TYPE(symtab[j].info) == STT_FUNC) &&
                            symtab[j].shndx != SHN_UNDEF) {
                        elf->num++;
                    }
                }

                if (elf->num) {
                    elf->symtab = (esp_symtab_t *)esp_elf_malloc(elf->num * sizeof(esp_symtab_t), false);
                    if (!elf->symtab) {
                        ESP_LOGE(TAG, "Failed to malloc for symbol table");
                        return -ENOMEM;
                    }

                    memset(elf->symtab, 0, elf->num * sizeof(esp_symtab_t));
                }

                for (j = 0; j < shdr[i].size / sizeof(elf32_sym_t); j++) {
                    if ((ELF_ST_BIND(symtab[j].info) == STB_GLOBAL) &&
                            (ELF_ST_TYPE(symtab[j].info) == STT_FUNC) &&
                            symtab[j].shndx != SHN_UNDEF) {
                        len = strlen((const char *)(strtab + symtab[j].name)) + 1;
#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
                        elf->symtab[num].addr =
                            (void *)(elf->ptext + symtab[j].value - elf->sec[ELF_SEC_TEXT].v_addr);
#else
                        elf->symtab[num].addr =
                            (void *)(elf->psegment + symtab[j].value - elf->svaddr);
#endif
#ifdef CONFIG_ELF_LOADER_CACHE_OFFSET
                        /* T5S3: dlsym must return the executable I-bus address. */
                        elf->symtab[num].addr = (void *)elf_remap_text(
                            elf, (uintptr_t)elf->symtab[num].addr);
#endif
                        elf->symtab[num].name = esp_elf_malloc(len, false);
                        if (!elf->symtab[num].name) {
                            ESP_LOGE(TAG, "Failed to malloc for symbol table name");
                            elf->num = num;
                            return -ENOMEM;
                        }

                        memset((void *)elf->symtab[num].name, 0, len);
                        memcpy((void *)elf->symtab[num].name, strtab + symtab[j].name, len);
                        ESP_LOGI(TAG, "elf->symtab[%d], func: %s", num, strtab + symtab[j].name);
                        num++;
                    }
                }
            }
#endif
        }
    }

#ifdef CONFIG_ELF_LOADER_LOAD_PSRAM
    ret = esp_elf_arch_flush(elf);
    if (ret) return ret;
#endif

    return 0;
}

/**
 * @brief Request running relocated ELF function.
 *
 * @param elf  - ELF object pointer
 * @param opt  - Request options
 * @param argc - Arguments number
 * @param argv - Arguments value array
 *
 * @return ESP_OK if success or other if failed.
 */
int esp_elf_request(esp_elf_t *elf, int opt, int argc, char *argv[])
{
    if (!elf || !(elf->entry)) {
        return -EINVAL;
    }

    elf->entry(argc, argv);

    return 0;
}

/**
 * @brief Deinitialize ELF object.
 *
 * @param elf - ELF object pointer
 *
 * @return None
 *
 * @note This function frees all resources allocated by esp_elf_relocate() and
 *       resets the structure to its initial state (same as after esp_elf_init()).
 */
void esp_elf_deinit(esp_elf_t *elf)
{
    if (!elf) {
        return;
    }

#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
    if (elf->pdata) {
        esp_elf_free(elf->pdata);
        elf->pdata = NULL;
    }

    if (elf->ptext) {
        esp_elf_free(elf->ptext);
        elf->ptext = NULL;
    }
#else
    if (elf->psegment) {
        esp_elf_free(elf->psegment);
        elf->psegment = NULL;
    }
#endif

#ifdef CONFIG_ELF_LOADER_SET_MMU
    esp_elf_arch_deinit_mmu(elf);
#endif

#if CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT
    if (elf->symtab) {
        for (int i = 0; i < elf->num; i++) {
            if (elf->symtab[i].name) {
                esp_elf_free(elf->symtab[i].name);
            }
        }

        esp_elf_free(elf->symtab);
        elf->symtab = NULL;
    }

    elf->num = 0;
#endif

    /* Reset structure to initial state (same as esp_elf_init) */
    memset(elf, 0, sizeof(esp_elf_t));
}

/**
 * @brief Print header description information of ELF.
 *
 * @param pbuf - ELF data buffer
 *
 * @return None
 */
void esp_elf_print_ehdr(const uint8_t *pbuf)
{
    const char *s_bits, *s_endian;
    const elf32_hdr_t *hdr = (const elf32_hdr_t *)pbuf;

    switch (hdr->ident[4]) {
    case 1:
        s_bits = "32-bit";
        break;
    case 2:
        s_bits = "64-bit";
        break;
    default:
        s_bits = "invalid bits";
        break;
    }

    switch (hdr->ident[5]) {
    case 1:
        s_endian = "little-endian";
        break;
    case 2:
        s_endian = "big-endian";
        break;
    default:
        s_endian = "invalid endian";
        break;
    }

    if (hdr->ident[0] == 0x7f) {
        ESP_LOGI(TAG, "%-40s %c%c%c", "Class:",                     hdr->ident[1], hdr->ident[2], hdr->ident[3]);
    }

    ESP_LOGI(TAG, "%-40s %s, %s", "Format:",                        s_bits, s_endian);
    ESP_LOGI(TAG, "%-40s %x", "Version(current):",                  hdr->ident[6]);

    ESP_LOGI(TAG, "%-40s %d", "Type:",                              hdr->type);
    ESP_LOGI(TAG, "%-40s %d", "Machine:",                           hdr->machine);
    ESP_LOGI(TAG, "%-40s %x", "Version:",                           hdr->version);
    ESP_LOGI(TAG, "%-40s %x", "Entry point address:",               hdr->entry);
    ESP_LOGI(TAG, "%-40s %x", "Start of program headers:",          hdr->phoff);
    ESP_LOGI(TAG, "%-40s %d", "Start of section headers:",          hdr->shoff);
    ESP_LOGI(TAG, "%-40s 0x%x", "Flags:",                           hdr->flags);
    ESP_LOGI(TAG, "%-40s %d", "Size of this header(bytes):",        hdr->ehsize);
    ESP_LOGI(TAG, "%-40s %d", "Size of program headers(bytes):",    hdr->phentsize);
    ESP_LOGI(TAG, "%-40s %d", "Number of program headers:",         hdr->phnum);
    ESP_LOGI(TAG, "%-40s %d", "Size of section headers(bytes):",    hdr->shentsize);
    ESP_LOGI(TAG, "%-40s %d", "Number of section headers:",         hdr->shnum);
    ESP_LOGI(TAG, "%-40s %d", "Section header string table i:",     hdr->shstrndx);
}

/**
 * @brief Print program header description information of ELF.
 *
 * @param pbuf - ELF data buffer
 *
 * @return None
 */
void esp_elf_print_phdr(const uint8_t *pbuf)
{
    const elf32_hdr_t *ehdr = (const elf32_hdr_t *)pbuf;
    const elf32_phdr_t *phdr = (const elf32_phdr_t *)((size_t)pbuf + ehdr->phoff);

    for (int i = 0; i < ehdr->phnum; i++) {
        ESP_LOGI(TAG, "%-40s %d", "type:",                          phdr->type);
        ESP_LOGI(TAG, "%-40s 0x%x", "offset:",                      phdr->offset);
        ESP_LOGI(TAG, "%-40s 0x%x", "vaddr",                        phdr->vaddr);
        ESP_LOGI(TAG, "%-40s 0x%x", "paddr:",                       phdr->paddr);
        ESP_LOGI(TAG, "%-40s %d", "filesz",                         phdr->filesz);
        ESP_LOGI(TAG, "%-40s %d", "memsz",                          phdr->memsz);
        ESP_LOGI(TAG, "%-40s %d", "flags",                          phdr->flags);
        ESP_LOGI(TAG, "%-40s 0x%x", "align",                        phdr->align);

        phdr = (const elf32_phdr_t *)((size_t)phdr + sizeof(elf32_phdr_t));
    }
}

/**
 * @brief Print section header description information of ELF.
 *
 * @param pbuf - ELF data buffer
 *
 * @return None
 */
void esp_elf_print_shdr(const uint8_t *pbuf)
{
    const elf32_hdr_t *ehdr = (const elf32_hdr_t *)pbuf;
    const elf32_shdr_t *shdr = (const elf32_shdr_t *)((size_t)pbuf + ehdr->shoff);

    for (int i = 0; i < ehdr->shnum; i++) {
        ESP_LOGI(TAG, "%-40s %d", "name:",                          shdr->name);
        ESP_LOGI(TAG, "%-40s %d", "type:",                          shdr->type);
        ESP_LOGI(TAG, "%-40s 0x%x", "flags:",                       shdr->flags);
        ESP_LOGI(TAG, "%-40s %x", "addr",                           shdr->addr);
        ESP_LOGI(TAG, "%-40s %x", "offset:",                        shdr->offset);
        ESP_LOGI(TAG, "%-40s %d", "size",                           shdr->size);
        ESP_LOGI(TAG, "%-40s 0x%x", "link",                         shdr->link);
        ESP_LOGI(TAG, "%-40s %d", "addralign",                      shdr->addralign);
        ESP_LOGI(TAG, "%-40s %d", "entsize",                        shdr->entsize);

        shdr = (const elf32_shdr_t *)((size_t)shdr + sizeof(elf32_shdr_t));
    }
}

/**
 * @brief Print section information of ELF.
 *
 * @param pbuf - ELF data buffer
 *
 * @return None
 */
void esp_elf_print_sec(esp_elf_t *elf)
{
    const char *sec_names[ELF_SECS] = {
        "text", "bss", "data", "rodata"
    };

    for (int i = 0; i < ELF_SECS; i++) {
        ESP_LOGI(TAG, "%s:   0x%08x size 0x%08x", sec_names[i], elf->sec[i].addr, elf->sec[i].size);
    }

    ESP_LOGI(TAG, "entry:  %p", elf->entry);
}

/**
 * @brief Register symbol table to global symbol tables array.
 *
 * @param symbol_table - Pointer to symbol table structure (array of esp_elfsym terminated by ESP_ELFSYM_END)
 *
 * @return 0 if success, -EINVAL if symbol_table is NULL, -EEXIST if already registered, -ENOMEM if no space.
 *
 * @note This function is not thread-safe. External synchronization must be used if calling
 *       this function concurrently from multiple threads.
 */
int esp_elf_register_symbol(esp_elf_symbol_table_t *symbol_table)
{
    if (!symbol_table) {
        return -EINVAL;
    }

    for (int i = 0; i < SYMBOL_TABLES_NO; i++) {
        if (g_symbol_tables[i] == symbol_table) {
            return -EEXIST;
        } else if (g_symbol_tables[i] == NULL) {
            g_symbol_tables[i] = symbol_table;
            return 0;
        }
    }

    return -ENOMEM;
}

/**
 * @brief Unregister symbol table from global symbol tables array.
 *
 * @param symbol_table - Pointer to symbol table structure to remove
 *
 * @return 0 if success, -EINVAL if symbol_table is NULL or symbol table not found.
 *
 * @note This function is not thread-safe. External synchronization must be used if calling
 *       this function concurrently from multiple threads.
 */
int esp_elf_unregister_symbol(esp_elf_symbol_table_t *symbol_table)
{
    if (!symbol_table) {
        return -EINVAL;
    }

    for (int i = 0; i < SYMBOL_TABLES_NO; i++) {
        if (g_symbol_tables[i] == symbol_table) {
            g_symbol_tables[i] = NULL;
            return 0;
        }
    }

    return -EINVAL;
}

/**
 * @brief Find symbol address by symbol name in registered tables.
 *
 * @param sym_name - Symbol name string to search
 *
 * @return Symbol address if found, 0 if not found.
 * @note Search order is registration order (earliest registered first).
 */
// Optional host notification of an actually resolved registered import. The
// privileged resolver never reaches this path; this grants no new symbols.
extern void esp_elf_registered_symbol_used(const void *table, const char *name,
                                           uintptr_t address) __attribute__((weak));

uintptr_t esp_elf_find_symbol(const char *sym_name)
{
    if (!sym_name) {
        return 0;
    }

    esp_elf_symbol_table_t *syms;
    for (int i = 0; i < SYMBOL_TABLES_NO; i++) {
        if (g_symbol_tables[i]) {
            syms = g_symbol_tables[i];
            while (syms->name) {
                if (!strcmp(syms->name, sym_name)) {
                    if (esp_elf_registered_symbol_used)
                        esp_elf_registered_symbol_used(g_symbol_tables[i], sym_name, (uintptr_t)syms->sym);
                    return (uintptr_t)syms->sym;
                }

                syms++;
            }
        }
    }

    return 0;
}
