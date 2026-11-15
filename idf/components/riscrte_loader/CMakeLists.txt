set(root "${CMAKE_CURRENT_LIST_DIR}/../../../lib/elf_loader")
option(RISC_CPU_SCOPE_V5_CANDIDATE "Build explicit default-off native5 task-scope consumer" OFF)
set(risc_scope_v5_sources)
set(risc_scope_v5_requires)
set(risc_scope_v5_includes)
if(RISC_CPU_SCOPE_V5_CANDIDATE)
    list(APPEND risc_scope_v5_sources
        "${root}/src/esp_cpu_task_retirement_v1.c"
        "${root}/src/esp_cpu_task_scope_v1.c"
        "${root}/src/esp_cpu_task_stack_v1.c"
        "${root}/src/esp_cpu_task_stack_port_v1.c"
        "${root}/src/esp_cpu_worker_v5.c"
        "${root}/src/esp_cpu_monotonic_v1.c")
    list(APPEND risc_scope_v5_requires pthread)
    list(APPEND risc_scope_v5_includes "${root}/../../sdk/driver")
endif()
idf_component_register(SRCS ${risc_scope_v5_sources}
    "${root}/src/esp_elf.c" "${root}/src/esp_elf_adapter.c"
    "${root}/src/esp_elf_symbol.c" "${root}/src/esp_elf_validate.c"
    "${root}/src/esp_elf_idf6_compat.c"
    "${root}/src/esp_privileged_os_cpu.c" "${root}/src/esp_privileged_elf.c"
    "${root}/src/esp_privileged_imports.c" "${root}/src/esp_privileged_manifest_imports.c"
    "${root}/src/arch/esp_elf_xtensa.c"
    "${root}/src/dlso/dlfcn.c" "${root}/src/dlso/dlmod.c"
    INCLUDE_DIRS "${root}/include"
    PRIV_INCLUDE_DIRS ${risc_scope_v5_includes}
    PRIV_REQUIRES spi_flash lwip esp_timer ${risc_scope_v5_requires})
if(RISC_CPU_SCOPE_V5_CANDIDATE)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC RISC_CPU_SCOPE_V5_CANDIDATE=1)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    RISC_NATIVE_ESP_IDF=1 ELF_LOADER_VER_MAJOR=1 ELF_LOADER_VER_MINOR=3 ELF_LOADER_VER_PATCH=3)
