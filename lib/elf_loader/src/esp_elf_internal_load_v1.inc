/* Included in the actual section loader. No independent authority, allocator
 * registry, firmware hardware service or executable public entry point. */
static void elf_internal_clear_v1(esp_elf_t *elf) {
    esp_elf_free(elf->internal_v1.data_owner);
    esp_elf_free(elf->internal_v1.text_owner);
    memset(&elf->internal_v1, 0, sizeof(elf->internal_v1));
}
#if RISC_ELF_INTERNAL_PLACEMENT_V1 == 1
static int elf_internal_load_v1(esp_elf_t *elf, const uint8_t *pbuf, uint32_t external_bytes) {
    const elf32_hdr_t *h = (const elf32_hdr_t *)pbuf;
    const elf32_shdr_t *sections = (const elf32_shdr_t *)(pbuf + h->shoff);
    const char *names = (const char *)pbuf + sections[h->shstrndx].offset;
    uint32_t alignment[ESP_ELF_INTERNAL_SECTIONS_V1] = {0};
    uint32_t offsets[ESP_ELF_INTERNAL_SECTIONS_V1] = {0};
    bool seen[ESP_ELF_INTERNAL_SECTIONS_V1] = {false};
    bool any = false;
    uint32_t text_capacity = 0, data_capacity = 0;
    if (elf->internal_v1.tag || elf->internal_v1.text_owner || elf->internal_v1.data_owner)
        return -EINVAL;
    for (unsigned i = 0; i < h->shnum; ++i) {
        const char *name = names + sections[i].name;
        const int slot = esp_elf_internal_class_v1(name);
        if (slot < 0) {
            if (!strncmp(name, ESP_ELF_INTERNAL_PREFIX_V1, sizeof(ESP_ELF_INTERNAL_PREFIX_V1) - 1u))
                return -EINVAL;
            continue;
        }
        if (seen[slot] || !esp_elf_internal_shape_v1((unsigned)slot, &sections[i])) return -EINVAL;
        seen[slot] = any = true;
        const elf32_shdr_t *source = &sections[i];
        if (source->size > UINT32_MAX - source->addr ||
            (slot == ESP_ELF_INTERNAL_TEXT_V1 && ((source->addr & 3u) || source->addralign < 4u)))
            return -EINVAL;
        esp_elf_sec_t *dest = &elf->internal_v1.sec[slot];
        dest->v_addr = source->addr; dest->size = source->size; dest->offset = source->offset;
        alignment[slot] = source->addralign;
        uint32_t *capacity = slot == ESP_ELF_INTERNAL_TEXT_V1 ? &text_capacity : &data_capacity;
        if (!esp_elf_data_reserve(source->size, source->addralign, source->addr, capacity)) return -EINVAL;
    }
    if (!any) return 0;
    /* A section name cannot authorize an ordinary image to consume this lane. */
    if (!esp_elf_privileged_os_cpu_scope_owned_v1()) return -EPERM;
    if (!esp_elf_internal_supported_v1()) return -ENOTSUP;
    for (unsigned i = 0; i < ESP_ELF_INTERNAL_SECTIONS_V1; ++i) {
        const esp_elf_sec_t *s = &elf->internal_v1.sec[i];
        if (!s->size) continue;
        for (unsigned j = 0; j < ELF_SECS + i; ++j) {
            const esp_elf_sec_t *other = j < ELF_SECS ? &elf->sec[j] : &elf->internal_v1.sec[j - ELF_SECS];
            if (other->size && (other->size > UINTPTR_MAX - other->v_addr ||
                (s->v_addr < other->v_addr + other->size && other->v_addr < s->v_addr + s->size)))
                return -EINVAL;
        }
    }
    if (text_capacity > UINT32_MAX - 3u) return -EINVAL;
    text_capacity = (text_capacity + 3u) & ~UINT32_C(3);
    if (external_bytes > ESP_ELF_MAX_IMAGE_BYTES ||
        text_capacity > ESP_ELF_MAX_IMAGE_BYTES - external_bytes ||
        data_capacity > ESP_ELF_MAX_IMAGE_BYTES - external_bytes - text_capacity) return -EINVAL;
    int rc;
    if (text_capacity) {
        uintptr_t data = 0, code = 0;
        rc = esp_elf_internal_alloc_text_v1(text_capacity, &elf->internal_v1.text_owner, &data, &code);
        if (rc) return rc;
        esp_elf_sec_t *text = &elf->internal_v1.sec[ESP_ELF_INTERNAL_TEXT_V1];
        if (!esp_elf_data_place(data, 0, text_capacity, text->size, alignment[0], text->v_addr, &offsets[0]) ||
            offsets[0] > UINTPTR_MAX - code || text->size > UINTPTR_MAX - code - offsets[0]) return -EINVAL;
        text->addr = data + offsets[0];
        elf->internal_v1.text_code = code + offsets[0];
        memcpy((void *)text->addr, pbuf + text->offset, text->size);
    }
    if (data_capacity) {
        rc = esp_elf_internal_alloc_data_v1(data_capacity, &elf->internal_v1.data_owner);
        if (rc) return rc;
        const uintptr_t base = (uintptr_t)elf->internal_v1.data_owner;
        uint32_t cursor = 0;
        for (unsigned i = 1; i < ESP_ELF_INTERNAL_SECTIONS_V1; ++i) {
            esp_elf_sec_t *s = &elf->internal_v1.sec[i];
            if (!s->size) continue;
            if (!esp_elf_data_place(base, cursor, data_capacity, s->size, alignment[i], s->v_addr, &offsets[i]))
                return -EINVAL;
            s->addr = base + offsets[i]; cursor = offsets[i] + s->size;
            if (i == ESP_ELF_INTERNAL_BSS_V1) memset((void *)s->addr, 0, s->size);
            else memcpy((void *)s->addr, pbuf + s->offset, s->size);
        }
    }
    elf->internal_v1.tag = ESP_ELF_INTERNAL_TAG_V1;
    return 0;
}

#else
static int elf_internal_load_v1(esp_elf_t *elf, const uint8_t *pbuf, uint32_t external_bytes) {
    (void)elf; (void)external_bytes;
    const elf32_hdr_t *h = (const elf32_hdr_t *)pbuf;
    const elf32_shdr_t *sections = (const elf32_shdr_t *)(pbuf + h->shoff);
    const char *names = (const char *)pbuf + sections[h->shstrndx].offset;
    /* Structural validation precedes this loader. No allocation, support or
     * publication callback is referenced when the explicit feature is off. */
    for (unsigned i = 0; i < h->shnum; ++i)
        if (!strncmp(names + sections[i].name, ESP_ELF_INTERNAL_PREFIX_V1,
                     sizeof(ESP_ELF_INTERNAL_PREFIX_V1) - 1u)) return -ENOTSUP;
    return 0;
}
#endif
