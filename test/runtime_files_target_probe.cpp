#include "bootstrap/RuntimeFiles.h"
#include "runtime/storage/PosixFiles.h"
#include "ports/esp32s3/NativeSpiffsFiles.h"
static_assert(sizeof(RiscBoot::RuntimeFiles)<=256,"internal source client grew");
static_assert(sizeof(RiscStorage::PosixFiles)<=512,"optional provider fixed state grew");
#ifdef ESP_PLATFORM
static_assert(RISC_POSIX_FILES_NOREPLACE==0,"native provider must not claim unavailable no-replace primitive");
#endif
bool runtime_files_target_probe(){static RiscStorage::PosixFiles p("/");return RiscBoot::runtimeFiles().bind(p.api(),[]{return true;},[]{return true;});}

bool native_spiffs_files_target_probe(){static RiscCpu::NativeSpiffsFiles p("/");return p.api()&&p.exitSafe();}
