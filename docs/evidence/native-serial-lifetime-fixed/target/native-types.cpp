#include "ports/esp32s3/NativeSerialFiles.cpp"
#include "ports/esp32s3/NativeSerialStorage.cpp"
extern "C" {
#if RISC_NATIVE_SERIAL_FILES
char measure_NativeSerialState[sizeof(RiscCpu::NativeSerialFiles::state)];
char measure_NativeSerialWorkspace[sizeof(RiscCpu::NativeSerialFiles::workspaceStorage)];
#endif
#if RISC_NATIVE_SERIAL_STORAGE_SELECTED
char measure_NativeSerialSessionWorkspace[sizeof(RiscCpu::NativeSerialStorage::sessionStorage)];
#if RISC_NATIVE_SERIAL_APPDATA_FILES
char measure_NativeSerialPrivateProvider[sizeof(RiscCpu::NativeSerialStorage::privateProviderStorage)];
#endif
#endif
}
