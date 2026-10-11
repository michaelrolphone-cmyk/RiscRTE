#include "ports/esp32s3/NativeEntropy.h"
static_assert(sizeof(risc_entropy_v1)==16,"ESP32-S3 entropy ABI size");
extern "C" const RiscBoot::EntropyBackend* entropy_target_compile(){
 return RiscCpu::NativeEntropy::backend();
}
extern "C" bool entropy_target_configure(bool (*owner)(),bool (*ready)(),bool (*take)(),void (*give)()){
 return RiscCpu::NativeEntropy::configure(owner,ready,take,give);
}
