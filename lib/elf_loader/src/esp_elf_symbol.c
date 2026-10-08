/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <reent.h>
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <getopt.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "private/esp_privileged_os_cpu.h"

#if CONFIG_LIBC_PICOLIBC
/*
 * picolibc compatibility wrappers.
 *
 * In picolibc:
 * - __errno is not a function (errno is a _Thread_local int variable).
 * - __getreent() is a macro defined as NULL by ESP-IDF platform reent.h.
 * - _ctype_ is a macro (_ctype_b + _CTYPE_OFFSET) in ctype.h; picolibc
 *   exports the underlying _ctype_b[384] table. Newlib-built ELFs resolve
 *   _ctype_ and index it as (_ctype_ + 1)[c]; _CTYPE_OFFSET is 127.
 */
static int *s_picolibc_errno_func(void)
{
    return &errno;
}

static struct _reent *s_picolibc_getreent_func(void)
{
    return NULL;
}

extern const char _ctype_b[];
#ifndef _CTYPE_OFFSET
#define _CTYPE_OFFSET 127
#endif
static const char * const s_picolibc_ctype_tbl = _ctype_b + _CTYPE_OFFSET;
#endif /* CONFIG_LIBC_PICOLIBC */

#include "rom/ets_sys.h"

#include "esp_log.h"
#include "esp_elf.h"
#include "../../hal/RuntimeImagePressure.h"

#if CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT
#include "private/esp_dlmod.h"
#endif
#include "private/elf_symbol.h"

extern int __ltdf2(double a, double b);
extern unsigned int __fixunsdfsi(double a);
extern int __gtdf2(double a, double b);
extern double __floatunsidf(unsigned int i);
extern double __divdf3(double a, double b);

static const char *TAG = "ELF_SYMBOL";
// Ordinary app relocations receive invocation-owned allocators. Privileged
// provider relocations retain libc/OS allocators for their independent lifetime.
extern uintptr_t native_app_memory_symbol(const char *name);

/** @brief Libc public functions symbols look-up table */

static const struct esp_elfsym g_esp_libc_elfsyms[] = {

    /* string.h
     *
     * Stable baseline for ordinary native applications. Keep fundamental,
     * side-effect-free string/memory helpers here rather than registering
     * them ad hoc in individual app API tables. This same public-libc table is
     * also safe for admitted providers; hardware/OS primitives remain outside
     * it and continue through scoped capability/privileged interfaces.
     */

    ESP_ELFSYM_EXPORT(strerror),
    ESP_ELFSYM_EXPORT(memset),
    ESP_ELFSYM_EXPORT(memcpy),
    ESP_ELFSYM_EXPORT(memmove),
    ESP_ELFSYM_EXPORT(memcmp),
    ESP_ELFSYM_EXPORT(memchr),
    ESP_ELFSYM_EXPORT(strlen),
    ESP_ELFSYM_EXPORT(strcpy),
    ESP_ELFSYM_EXPORT(strncpy),
    ESP_ELFSYM_EXPORT(strcmp),
    ESP_ELFSYM_EXPORT(strncmp),
    ESP_ELFSYM_EXPORT(strchr),
    ESP_ELFSYM_EXPORT(strrchr),
    ESP_ELFSYM_EXPORT(strstr),
    ESP_ELFSYM_EXPORT(strtod),
    ESP_ELFSYM_EXPORT(strtol),
    ESP_ELFSYM_EXPORT(strcspn),
    ESP_ELFSYM_EXPORT(strncat),

    /* stdio.h */

    ESP_ELFSYM_EXPORT(snprintf),

    /* unistd.h */


    /* stdlib.h */

#if RISC_APP_IMAGE_CACHE
    {"malloc", (const void *)&risc_image_malloc},
    {"calloc", (const void *)&risc_image_calloc},
    {"realloc", (const void *)&risc_image_realloc},
#else
    ESP_ELFSYM_EXPORT(malloc),
    ESP_ELFSYM_EXPORT(calloc),
    ESP_ELFSYM_EXPORT(realloc),
#endif
    ESP_ELFSYM_EXPORT(free),

    /* time.h */

    ESP_ELFSYM_EXPORT(clock_gettime),
    ESP_ELFSYM_EXPORT(strftime),

    /* pthread.h */


    /* libc (newlib / picolibc) */
#if CONFIG_LIBC_PICOLIBC
    { "__errno", (void*)s_picolibc_errno_func },
    { "__getreent", (void*)s_picolibc_getreent_func },
    { "_ctype_", (void*)s_picolibc_ctype_tbl },
#elif defined(__HAVE_LOCALE_INFO__) && __HAVE_LOCALE_INFO__
    ESP_ELFSYM_EXPORT(__errno),
    ESP_ELFSYM_EXPORT(__getreent),
    ESP_ELFSYM_EXPORT(__locale_ctype_ptr),
#else
    ESP_ELFSYM_EXPORT(__errno),
    ESP_ELFSYM_EXPORT(__getreent),
    ESP_ELFSYM_EXPORT(_ctype_),
#endif

    /* math */

    ESP_ELFSYM_EXPORT(__ltdf2),
    ESP_ELFSYM_EXPORT(__fixunsdfsi),
    ESP_ELFSYM_EXPORT(__gtdf2),
    ESP_ELFSYM_EXPORT(__floatunsidf),
    ESP_ELFSYM_EXPORT(__divdf3),

    /* getopt.h */

    ESP_ELFSYM_EXPORT(getopt_long),
    ESP_ELFSYM_EXPORT(optind),
    ESP_ELFSYM_EXPORT(opterr),
    ESP_ELFSYM_EXPORT(optarg),
    ESP_ELFSYM_EXPORT(optopt),

    /* setjmp.h */


    ESP_ELFSYM_END
};

/** @brief ESP-IDF public functions symbols look-up table */

static const struct esp_elfsym g_esp_espidf_elfsyms[] = {
    /* Bootstrap runtime exports no networking or direct peripheral API. */
    ESP_ELFSYM_END
};

/**
 * @brief Find symbol address by name.
 *
 * @param sym_name - Symbol name
 *
 * @return Symbol address if success or 0 if failed.
 */
uintptr_t elf_find_sym_default(const char *sym_name)
{
    if (!sym_name) {
        ESP_LOGE(TAG, "Invalid parameter: sym_name is NULL");
        return 0;
    }

    /* This decision is task-specific. While trusted firmware is relocating
     * a privileged provider, ONLY its exact OS/CPU inventory and the compiled
     * public libc table are legal firmware imports. A failed lookup must not
     * fall through to registered symbols, the customer/IDF tables, or another
     * ELF via dlmod_getaddr(). Defined local ELF symbols can still use the
     * existing relocator's own in-image fallback after this returns zero.
     * Other tasks retain the ordinary app resolution path unchanged. */
    const bool privileged_scope = esp_elf_privileged_os_cpu_scope_owned_v1();
    if (privileged_scope) {
        uintptr_t privileged = esp_elf_privileged_os_cpu_lookup_v1(sym_name);
        if (privileged) return privileged;
    }

    if (!privileged_scope) {
        uintptr_t app_memory = native_app_memory_symbol(sym_name);
        if (app_memory) return app_memory;
    }
    esp_elf_symbol_table_t *syms;

#ifdef CONFIG_ELF_LOADER_LIBC_SYMBOLS
    syms = g_esp_libc_elfsyms;
    while (syms->name) {
        if (!strcmp(syms->name, sym_name)) {
            return (uintptr_t)syms->sym;
        }
        syms++;
    }
#else
    syms = g_esp_libc_elfsyms;
    (void)syms;
#endif

    /* In particular, a syntactically valid local symbol named usb_* must
     * not be redirected to a globally registered hardware implementation.
     * The port has no knowledge of which capability the provider implements. */
    if (privileged_scope) return 0;

#ifdef CONFIG_ELF_LOADER_ESPIDF_SYMBOLS
    syms = g_esp_espidf_elfsyms;
    while (syms->name) {
        if (!strcmp(syms->name, sym_name)) {
            return (uintptr_t)syms->sym;
        }
        syms++;
    }
#else
    syms = g_esp_espidf_elfsyms;
    (void)syms;
#endif

#ifdef CONFIG_ELF_LOADER_CUSTOMER_SYMBOLS
    extern const struct esp_elfsym g_customer_elfsyms[];
    syms = g_customer_elfsyms;
    while (syms->name) {
        if (!strcmp(syms->name, sym_name)) {
            return (uintptr_t)syms->sym;
        }
        syms++;
    }
#endif

    uintptr_t sym_addr = esp_elf_find_symbol(sym_name);
    if (sym_addr) {
        return sym_addr;
    }

#if CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT
    return (uintptr_t)dlmod_getaddr(sym_name);
#else
    return 0;
#endif
}
