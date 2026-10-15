set(root "${CMAKE_CURRENT_LIST_DIR}/../../..")
file(GLOB_RECURSE runtime_sources "${root}/src/*.cpp" "${root}/src/*.c")
idf_component_register(SRCS ${runtime_sources}
    INCLUDE_DIRS "${root}/src" "${root}/include" "${root}/sdk/app"
                 "${root}/sdk/driver" "${root}/sdk/hardware"
                 "${root}/lib/ArduinoJson/src" "${root}/lib/hal"
    REQUIRES riscrte_loader app_update esp_app_format esp_timer esp_system
             nvs_flash spiffs esp_psram esp_wifi bt lwip esp_netif esp_event
             esp_http_client esp-tls http_parser mbedtls fatfs driver esp_driver_gpio esp_driver_i2s
             esp_driver_ledc esp_driver_sdmmc esp_driver_sdspi esp_driver_spi
             esp_driver_uart esp_driver_usb_serial_jtag esp_hal_usb)
set_target_properties(${COMPONENT_LIB} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(${COMPONENT_LIB} PUBLIC RISC_NATIVE_ESP_IDF=1
    RISC_CONSOLE_USB_SERIAL_JTAG=1 RISC_RUNTIME_METADATA_PSRAM=1
    RISC_TARGET="esp32s3-native-idf61")
# Both the native startup and the legacy qualification route retain one-shot,
# no-erase NVS initialization. No automatic migration or format is introduced.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=nvs_flash_init")
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${root}"
    OUTPUT_VARIABLE source_sha OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/RiscBuildIdentity.h"
    "#define RISC_BUILD_SOURCE_SHA \"${source_sha}\"\n#define RISC_BUILD_IDENTITY \"RTE_SOURCE=${source_sha}\"\n#define RISC_BUILD_VERSION \"0.3.5-idf6-candidate\"\n")
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
# The transport audits the pinned SDK's actual socket custody before destroy;
# never copy its opaque struct or discard close failures.
idf_component_get_property(tls_root esp-tls COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${tls_root}/private_include")
