set(root "${CMAKE_CURRENT_LIST_DIR}/../../..")
file(GLOB_RECURSE runtime_sources "${root}/src/*.cpp" "${root}/src/*.c")
idf_component_register(SRCS ${runtime_sources} ${RISC_NATIVE_EXTRA_SOURCES}
    INCLUDE_DIRS "${root}/src" "${root}/include" "${root}/sdk/app"
                 "${root}/sdk/driver" "${root}/sdk/hardware"
                 "${root}/lib/ArduinoJson/src" "${root}/lib/hal"
    REQUIRES riscrte_loader risc_littlefs app_update esp_app_format esp_timer esp_system
             nvs_flash spiffs spi_flash esp_psram esp_wifi bt lwip esp_netif esp_event
             esp_http_client esp-tls http_parser mbedtls fatfs driver esp_driver_gpio esp_driver_i2s
             esp_driver_ledc esp_driver_sdmmc esp_driver_sdspi esp_driver_spi
             esp_driver_uart esp_driver_usb_serial_jtag esp_hal_usb)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(${COMPONENT_LIB} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>"
    ${RISC_NATIVE_COMPILE_OPTIONS})
target_include_directories(${COMPONENT_LIB} PRIVATE ${RISC_NATIVE_INCLUDE_DIRECTORIES})
target_compile_definitions(${COMPONENT_LIB} PUBLIC RISC_NATIVE_ESP_IDF=1
    RISC_CONSOLE_USB_SERIAL_JTAG=1 RISC_RUNTIME_METADATA_PSRAM=1
    RISC_TARGET="${RISC_NATIVE_TARGET}" ${RISC_NATIVE_DEFINITIONS})
if(RISC_NATIVE_SETTINGS_HEADER)
    target_compile_options(${COMPONENT_LIB} PRIVATE "-include" "${RISC_NATIVE_SETTINGS_HEADER}")
endif()
if(RISC_NATIVE_APPDATA)
    foreach(symbol esp_vfs_register esp_vfs_unregister esp_vfs_register_fs esp_vfs_unregister_fs
                   esp_partition_read esp_partition_write esp_partition_erase_range
                   esp_littlefs_format esp_littlefs_format_partition)
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${symbol}")
    endforeach()
endif()
target_link_libraries(${COMPONENT_LIB} INTERFACE ${RISC_NATIVE_LINK_OPTIONS})
# Keep the allocated app-capacity marker in the final ELF, rather than relying
# on a host admission harness's independently compiled capacity.
foreach(definition IN LISTS RISC_NATIVE_DEFINITIONS)
    if(definition MATCHES "^RISC_INSTALLED_APP_CAPACITY=[0-9]+$")
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,-u,risc_installed_app_capacity")
    endif()
endforeach()
# Both the native startup and the legacy qualification route retain one-shot,
# no-erase NVS initialization. No automatic migration or format is introduced.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=nvs_flash_init")
if(DEFINED RISC_SOURCE_SHA)
    # GitHub's container checkout can have a different owner from IDF's shell.
    # Its immutable checkout SHA is supplied by the workflow, without changing
    # Git's ownership protections or relying on a user-wide safe.directory.
    set(source_sha "${RISC_SOURCE_SHA}")
else()
    execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${root}"
        OUTPUT_VARIABLE source_sha OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
endif()
string(LENGTH "${source_sha}" source_sha_length)
if(NOT source_sha_length EQUAL 40 OR NOT source_sha MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "Runtime source identity must be an exact Git commit SHA")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/RiscBuildIdentity.h"
    "#define RISC_BUILD_SOURCE_SHA \"${source_sha}\"\n#define RISC_BUILD_IDENTITY \"RTE_SOURCE=${source_sha}\"\n#define RISC_BUILD_VERSION \"${PROJECT_VER}\"\n")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
# The transport audits the pinned SDK's actual socket custody before destroy;
# never copy its opaque struct or discard close failures.
idf_component_get_property(tls_root esp-tls COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${tls_root}/private_include")
