/* Private verified-provider relocation entry point. No hardware logic here. */
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include "private/esp_privileged_elf.h"
#include "private/esp_privileged_imports.h"
#include "private/esp_privileged_manifest_imports.h"
#include "private/esp_privileged_os_cpu.h"

extern bool esp_elf_validate_file(const uint8_t *image, size_t length);

int esp_elf_relocate_privileged_verified_v1(esp_elf_t *module,
                                            const uint8_t *verified_bytes,
                                            size_t verified_length,
                                            const char *const *signed_imports,
                                            size_t signed_import_count)
{
    /* Zero imports is a real exact declaration. The matcher verifies that no
     * undefined symbol occurs in either ELF symbol table; nullptr still
     * rejects missing metadata, regardless of the declared count. */
    if (!module || !verified_bytes || !signed_imports ||
        signed_import_count > 128 ||
        !esp_elf_validate_file(verified_bytes, verified_length) ||
        !esp_elf_privileged_imports_valid_v1(verified_bytes, verified_length) ||
        !esp_elf_privileged_manifest_imports_match_v1(
            verified_bytes, verified_length, signed_imports, signed_import_count))
        return -EINVAL;

    if (!esp_elf_privileged_os_cpu_begin_v1()) return -EBUSY;
    int result = esp_elf_init(module);
    if (result == 0) {
        if (!esp_elf_privileged_os_cpu_authorize_relocation_v1(module))
            result = -EPERM;
        else
            result = esp_elf_relocate(module, verified_bytes);
        if (result != 0) esp_elf_deinit(module);
    }
    if (!esp_elf_privileged_os_cpu_end_v1()) {
        if (result == 0) esp_elf_deinit(module);
        return -EIO;
    }
    return result;
}
