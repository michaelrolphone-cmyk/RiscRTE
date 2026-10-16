/* Trusted native image custody. These declarations are not ELF imports. */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct elf_file {
    uint8_t *payload;
    size_t size;
} elf_file_t;

/* Return zero with canonical structurally validated bytes allocated using
 * esp_elf_malloc and all media handles closed. Success transfers ownership to
 * the loader. Return -1 with errno on failure (ENOMEM permits one retry after
 * optional cache reclamation); the reader owns all failure cleanup. Every call
 * receives an empty output. The reader is scoped to this load, never global. */
typedef int (*esp_dl_image_reader)(void *context, const char *path, elf_file_t *file);
