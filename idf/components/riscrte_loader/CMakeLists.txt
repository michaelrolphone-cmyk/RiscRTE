set(root "${CMAKE_CURRENT_LIST_DIR}/../../../lib/elf_loader")
idf_component_register(SRCS
    "${root}/src/esp_elf.c" "${root}/src/esp_elf_adapter.c"
    "${root}/src/esp_elf_symbol.c" "${root}/src/esp_elf_validate.c"
    "${root}/src/esp_privileged_os_cpu.c" "${root}/src/esp_privileged_elf.c"
    "${root}/src/esp_privileged_imports.c" "${root}/src/esp_privileged_manifest_imports.c"
    "${root}/src/arch/esp_elf_xtensa.c"
    "${root}/src/dlso/dlfcn.c" "${root}/src/dlso/dlmod.c"
    INCLUDE_DIRS "${root}/include"
    PRIV_REQUIRES spi_flash lwip esp_timer)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ELF_LOADER_VER_MAJOR=1 ELF_LOADER_VER_MINOR=3 ELF_LOADER_VER_PATCH=3)
