# Compile-only scratch assembly with the concrete mount dependency enabled.
include("${CMAKE_CURRENT_LIST_DIR}/native_metadata_profile.cmake")
set(RISC_NATIVE_TARGET "scratch-provider-compile")
set(RISC_NATIVE_APPDATA ON)
list(APPEND RISC_NATIVE_DEFINITIONS RISC_PAIRED_APP_DATA=1
    RISC_NATIVE_APP_DATA_MOUNT=1 RISC_NATIVE_FILE_SOURCE=1)
