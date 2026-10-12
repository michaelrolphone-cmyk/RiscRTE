#include "ports/esp32s3/CpuPort.h"
extern "C" {
unsigned char native_storage_port_size[sizeof(RiscCpu::Port)];
}
