#include "ports/esp32s3/NativeHttp.h"
// Force production callback bodies into the real ESP32-S3 SDK object.
extern "C" const risc_http_client_v1* risc_native_http_target_probe(bool(*owner)(),bool(*ready)()){
 RiscCpu::NativeHttp::configure(owner,ready);return RiscCpu::NativeHttp::api();
}
