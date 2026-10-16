# Compile-only generic paired-store assembly. No device, application or GPIO
# selection is required to compile the actual optional metadata consumers.
set(RISC_NATIVE_TARGET "metadata-provider-compile")
set(RISC_NATIVE_PARTITIONS "${RISC_RUNTIME_ROOT}/partitions-paired.csv")
set(RISC_NATIVE_APPDATA OFF)
set(RISC_NATIVE_IQ OFF)
set(RISC_NATIVE_DEFINITIONS RISC_PAIRED_BANKS=1 RISC_RUNTIME_METADATA_PSRAM=1
    RISC_RUNTIME_ATOMIC_KEY_VALUE=1 RISC_RUNTIME_PREFERENCE_STORE=1 RISC_RUNTIME_STORAGE_FILES=1)
