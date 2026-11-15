#pragma once
#include "private/elf_types.h"
#include <string.h>

/* Explicit build-time callback selection, not runtime privilege or port
 * availability. Ordinary builds require no new internal allocator symbols. */
#ifndef RISC_ELF_INTERNAL_PLACEMENT_V1
#define RISC_ELF_INTERNAL_PLACEMENT_V1 0
#endif
#if RISC_ELF_INTERNAL_PLACEMENT_V1 != 0 && RISC_ELF_INTERNAL_PLACEMENT_V1 != 1
#error "RISC_ELF_INTERNAL_PLACEMENT_V1 must be 0 or 1"
#endif

/* New optional private output classes. Input compiler section names are NOT a
 * placement contract. The package generator must collect code and its literal
 * pools together and prove relocation/reachability before selecting this lane. */
#define ESP_ELF_INTERNAL_TAG_V1 UINT32_C(0x49525031)
#define ESP_ELF_INTERNAL_TEXT_V1 0u
#define ESP_ELF_INTERNAL_DATA_V1 1u
#define ESP_ELF_INTERNAL_RODATA_V1 2u
#define ESP_ELF_INTERNAL_BSS_V1 3u
#define ESP_ELF_INTERNAL_PREFIX_V1 ".risc.internal."
static inline int esp_elf_internal_class_v1(const char *name) {
    if (!strcmp(name, ".risc.internal.text")) return ESP_ELF_INTERNAL_TEXT_V1;
    if (!strcmp(name, ".risc.internal.data")) return ESP_ELF_INTERNAL_DATA_V1;
    if (!strcmp(name, ".risc.internal.rodata")) return ESP_ELF_INTERNAL_RODATA_V1;
    if (!strcmp(name, ".risc.internal.bss")) return ESP_ELF_INTERNAL_BSS_V1;
    return -1;
}
static inline bool esp_elf_internal_shape_v1(unsigned slot, const elf32_shdr_t *s) {
    if (!s || slot >= ESP_ELF_INTERNAL_SECTIONS_V1 || !s->size) return false;
    const uint32_t flags = SHF_ALLOC | (slot == ESP_ELF_INTERNAL_TEXT_V1 ? SHF_EXECINSTR :
        slot == ESP_ELF_INTERNAL_RODATA_V1 ? 0u : SHF_WRITE);
    return s->flags == flags && s->type ==
        (slot == ESP_ELF_INTERNAL_BSS_V1 ? SHT_NOBITS : SHT_PROGBITS);
}
static inline bool esp_elf_span_contains_v1(uintptr_t base, size_t length,
                                           uintptr_t address, size_t bytes) {
    return base && length && length <= UINTPTR_MAX - base && bytes &&
        address >= base && address - base < length && bytes <= length - (address - base);
}
static inline const esp_elf_sec_t *esp_elf_loaded_section_v1(const esp_elf_t *elf, unsigned i) {
    if (!elf) return NULL;
    if (i < ELF_SECS) return &elf->sec[i];
    i -= ELF_SECS;
    return elf->internal_v1.tag == ESP_ELF_INTERNAL_TAG_V1 && i < ESP_ELF_INTERNAL_SECTIONS_V1 ?
        &elf->internal_v1.sec[i] : NULL;
}
#ifdef __cplusplus
extern "C" {
#endif
/* Full byte extent is required for relocation writes. No implicit aliasing. */
uintptr_t esp_elf_map_span_v1(esp_elf_t *, uintptr_t virtual_address, size_t bytes);
/* Adapter returns an allocation ownership pointer AND its writable/callable
 * views. Failure leaves all outputs zero, never falls back to external RAM. */
int esp_elf_internal_alloc_text_v1(uint32_t bytes, void **owner,
                                  uintptr_t *writable, uintptr_t *executable);
int esp_elf_internal_alloc_data_v1(uint32_t bytes, void **owner);
bool esp_elf_internal_supported_v1(void);
int esp_elf_internal_publish_v1(const esp_elf_t *);
#ifdef __cplusplus
}
#endif
