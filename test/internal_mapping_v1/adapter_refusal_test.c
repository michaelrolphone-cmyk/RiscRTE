/* Link the actual adapter, not the modeled allocation leaves in loader_test.c.
 * Host SDK stubs may model existing ordinary allocation/cache dependencies,
 * but must not replace any esp_elf_internal_* implementation. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "private/esp_elf_internal_v1.h"
int main(void) {
    esp_elf_t image;
    memset(&image, 0, sizeof(image));
    assert(!esp_elf_internal_supported_v1());
    void *owner = NULL;
    uintptr_t writable = 0, executable = 0;
    assert(esp_elf_internal_alloc_text_v1(16, &owner, &writable, &executable) == -ENOTSUP);
    assert(!owner && !writable && !executable);
    assert(esp_elf_internal_alloc_data_v1(16, &owner) == -ENOTSUP && !owner);
    assert(esp_elf_internal_alloc_text_v1(3, &owner, &writable, &executable) == -EINVAL);
    assert(esp_elf_internal_alloc_data_v1(0, &owner) == -EINVAL);
    assert(esp_elf_internal_publish_v1(NULL) == -EINVAL);
    assert(!esp_elf_internal_publish_v1(&image));
    image.internal_v1.tag = ESP_ELF_INTERNAL_TAG_V1;
    assert(esp_elf_internal_publish_v1(&image) == -ENOTSUP);
    image.internal_v1.tag ^= 1;
    assert(esp_elf_internal_publish_v1(&image) == -EINVAL);
    owner = (void *)(uintptr_t)1;
    assert(esp_elf_internal_alloc_data_v1(16, &owner) == -EINVAL);
    assert(owner == (void *)(uintptr_t)1);
    puts("PASS actual-adapter-refusal");
    return 0;
}
