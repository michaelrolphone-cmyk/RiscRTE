#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Existing structural admission limit, including physical layout padding. */
#define ESP_ELF_MAX_IMAGE_BYTES (8u * 1024u * 1024u)

/* Keep virtual section ranges unchanged. Padding belongs to the allocation,
 * never to sec.size: enlarging virtual ranges aliases neighboring sections.
 * A mapped base retains its virtual residue modulo at least four bytes, so
 * an aligned virtual relocation site remains aligned after translation. */
static inline bool esp_elf_data_alignment(uint32_t alignment, uint32_t virtual_address,
                                          uint32_t *effective)
{
    if (!effective) return false;
    if (!alignment) alignment = 1;
    if ((alignment & (alignment - 1)) || (virtual_address & (alignment - 1))) return false;
    *effective = alignment < 4 ? 4 : alignment;
    return true;
}

static inline bool esp_elf_data_reserve(uint32_t length, uint32_t alignment,
                                        uint32_t virtual_address, uint32_t *capacity)
{
    uint32_t effective;
    if (!capacity || !esp_elf_data_alignment(alignment, virtual_address, &effective)) return false;
    const uint32_t padding = effective - 1;
    if (padding > UINT32_MAX - *capacity || length > UINT32_MAX - *capacity - padding) return false;
    *capacity += padding + length;
    return true;
}

static inline bool esp_elf_data_place(uintptr_t base, uint32_t cursor, uint32_t capacity,
                                      uint32_t length, uint32_t alignment, uint32_t virtual_address,
                                      uint32_t *offset)
{
    uint32_t effective;
    if (!offset || !esp_elf_data_alignment(alignment, virtual_address, &effective) ||
        cursor > capacity || base > UINTPTR_MAX - cursor) return false;
    const uint32_t padding = (uint32_t)((uintptr_t)virtual_address - (base + cursor)) & (effective - 1);
    if (padding > capacity - cursor || length > capacity - cursor - padding) return false;
    const uint32_t start = cursor + padding;
    if (base > UINTPTR_MAX - start || base + start > UINTPTR_MAX - length) return false;
    *offset = start;
    return true;
}
