cmake_minimum_required(VERSION 3.22)
set(IDF_TARGET esp32s3)
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
include("${CMAKE_CURRENT_LIST_DIR}/product-profile.cmake")
set(COMPONENTS runtime)
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../platformio.ini" version_lines REGEX "^version[ \t]*=")
list(LENGTH version_lines version_count)
if(NOT version_count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one shared Runtime version")
endif()
string(REGEX REPLACE "^version[ \t]*=[ \t]*" "" PROJECT_VER "${version_lines}")
if(NOT PROJECT_VER MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "Runtime version must be a numeric release triplet")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../platformio.ini")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(riscrte_native)
# Existing paired-bank firmware admission compares this fixed descriptor field.
# Retain the historical image identity as metadata, without linking Arduino.
idf_component_get_property(app_format_lib esp_app_format COMPONENT_LIB)
idf_component_get_property(app_format_dir esp_app_format COMPONENT_DIR)
set(app_desc_source "${app_format_dir}/esp_app_desc.c")
get_source_file_property(app_desc_definitions "${app_desc_source}"
    TARGET_DIRECTORY ${app_format_lib} COMPILE_DEFINITIONS)
list(FILTER app_desc_definitions EXCLUDE REGEX "^[ \t]*PROJECT_NAME=")
list(APPEND app_desc_definitions "PROJECT_NAME=\"arduino-lib-builder\"")
set_source_files_properties("${app_desc_source}" TARGET_DIRECTORY ${app_format_lib}
    PROPERTIES COMPILE_DEFINITIONS "${app_desc_definitions}")
if(NOT IDF_VERSION_MAJOR EQUAL 6 OR NOT IDF_VERSION_MINOR EQUAL 1)
    message(FATAL_ERROR "This candidate is pinned to ESP-IDF 6.1")
endif()
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "$ENV{IDF_PATH}"
    OUTPUT_VARIABLE sdk_sha OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT sdk_sha STREQUAL "fff9895c82d744c7237be8847347bdd1b07c6643")
    message(FATAL_ERROR "ESP-IDF source must match the pinned v6.1 commit")
endif()
if(RISC_NATIVE_IQ)
    set(iq_aliases "${CMAKE_BINARY_DIR}/native-iq-rom.ld")
    execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/native_idf_iq_link.py"
        prepare --rom-elf "$ENV{ESP_ROM_ELF_DIR}/esp32s3_rev0_rom.elf" --output "${iq_aliases}"
        COMMAND_ERROR_IS_FATAL ANY)
    idf_build_get_property(native_elf EXECUTABLE)
    target_link_options(${native_elf} PRIVATE "-T${iq_aliases}"
        "-Wl,-u,rom_i2c_readReg" "-Wl,-u,rom_i2c_writeReg"
        "-Wl,-u,rom_pbus_rd" "-Wl,-u,ets_delay_us")
    add_custom_command(TARGET ${native_elf} POST_BUILD
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/native_idf_iq_link.py"
        verify --elf "$<TARGET_FILE:${native_elf}>" --target "${RISC_NATIVE_TARGET}"
        --output "${CMAKE_BINARY_DIR}/radio-iq-proof.json")
endif()
