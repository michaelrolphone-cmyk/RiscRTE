#include "RiscProviderV2.h"
#include <string.h>

// Deliberately RETAIN start()'s pointer until quiesce/stop. The runtime must
// keep the entire table, names, and dependency interfaces alive until then.
static const risc_provider_dependency_v1* retained;
static size_t retained_count;
static int capability = 73;
static bool started;

__attribute__((visibility("default")))
int retained_dependency_is_valid(void) {
    return started && retained && retained_count == 1 &&
        retained[0].capability_id &&
        strcmp(retained[0].capability_id, "cap.root") == 0 &&
        retained[0].api_version == 1 && retained[0].api &&
        *(const int*)retained[0].api == 42;
}

static bool start(const risc_provider_dependency_v1* dependencies, size_t count) {
    if (started || !dependencies || count != 1) return false;
    retained = dependencies;
    retained_count = count;
    started = true;
    return retained_dependency_is_valid() != 0;
}
static bool quiesce(void) {
    return retained_dependency_is_valid() != 0;
}
static void stop(void) {
    // The retained pointer must also be usable during stop, not just start.
    if (!retained_dependency_is_valid()) __builtin_trap();
    started = false;
    retained = 0;
    retained_count = 0;
}
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "fixture-retaining", "cap.retaining", 1, &capability,
    start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2* t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}
