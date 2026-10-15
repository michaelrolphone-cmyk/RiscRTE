# Exact shipped filesystem implementation, plus the bounded IDF6 ABI port.
# Product selection and mount activation belong to the consuming profile.
# IDF first evaluates component requirements in script mode, where the binary
# directory is the invoking source directory. Do not stage files in that pass.
if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(REQUIRES esp_partition vfs
        PRIV_REQUIRES spi_flash esp_system heap)
    return()
endif()
set(pin_root "${CMAKE_CURRENT_LIST_DIR}/../../../experimental/native_replica/pinned_littlefs")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/pinned")
idf_build_get_property(python PYTHON)
execute_process(COMMAND "${python}" "${pin_root}/prepare_idf6.py" "${generated}"
    COMMAND_ERROR_IS_FATAL ANY)
file(GLOB_RECURSE pin_inputs "${pin_root}/vendor/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${pin_inputs} "${pin_root}/prepare_idf6.py" "${pin_root}/mtime32.h"
    "${pin_root}/littlefs_api.h" "${pin_root}/littlefs/lfs.h" "${pin_root}/littlefs/lfs_util.h"
    "${pin_root}/vendor.sha256.json")
idf_component_register(SRCS "${generated}/esp_littlefs.c"
    "${generated}/littlefs_esp_part.c" "${generated}/littlefs/lfs.c"
    "${generated}/littlefs/lfs_util.c" "${generated}/lfs_config.c"
    INCLUDE_DIRS "${generated}"
    REQUIRES esp_partition vfs
    PRIV_REQUIRES spi_flash esp_system heap)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LFS_CONFIG=lfs_config.h)
target_compile_options(${COMPONENT_LIB} PRIVATE -include "${CMAKE_CURRENT_LIST_DIR}/profile_config.h")
# NativeAppDataMount verifies this qualification marker as well as private layout.
target_compile_definitions(${COMPONENT_LIB} PUBLIC RISC_LITTLEFS_IDF6_MTIME32=1)
