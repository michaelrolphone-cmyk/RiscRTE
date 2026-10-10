#include "bootstrap/Runtime.h"
#include "ports/esp32s3/CpuPort.h"
#include "runtime/drivers/ProviderOwnedSpecV2.h"
// Inspect the actual host registry layout in this translation unit, without
// duplicating its private definition or invoking any provider/queue operation.
#include "runtime/streams/ProviderQueueHost.cpp"
#include <cstdio>
int main() {
  std::printf("{\"pointer_bytes\":%zu,\"apps\":%zu,\"providers\":%zu,\"grants\":%zu,"
    "\"runtime_bytes\":%zu,\"graph_bytes\":%zu,\"cpu_port_bytes\":%zu,"
    "\"stream_registry_bytes\":%zu,\"owned_node_bytes\":%zu,\"graph_matrix_bytes\":%zu}\n",
    sizeof(void*),RiscLimits::Apps,RiscLimits::Providers,RiscLimits::Grants,
    sizeof(RiscBoot::Runtime),sizeof(RuntimeProviders::GraphV2),sizeof(RiscCpu::Port),
    sizeof(RuntimeStreams::registry),sizeof(RuntimeProviders::OwnedNodeV2),
    sizeof(bool)*RiscLimits::Providers*RiscLimits::Providers);
}
