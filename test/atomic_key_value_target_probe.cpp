#include "bootstrap/Runtime.h"
#include "ports/esp32s3/CpuPort.h"
extern "C" {
unsigned char atomic_runtime_size[sizeof(RiscBoot::Runtime)];
unsigned char atomic_runtime_port_size[sizeof(RiscBoot::Port)];
unsigned char atomic_cpu_port_size[sizeof(RiscCpu::Port)];
}
#if RISC_RUNTIME_ATOMIC_KEY_VALUE
#include "experimental/native_replica/NativeAtomicKeyValue.h"
bool atomic_registration_probe(RiscBoot::Runtime&r,RiscExperimental::NativeAtomicKeyValue& endpoint){return r.registerAtomicKeyValue("atomic-main",6,endpoint);}
#endif
