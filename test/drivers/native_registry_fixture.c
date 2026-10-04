#include "RiscProviderV2.h"
#include "RiscHardwareConfigV1.h"
#include "native_registry_fixture.h"
#include <assert.h>
#include <string.h>
#ifndef FIXTURE_SLOT
#define FIXTURE_SLOT 0
#endif
#ifndef FIXTURE_ID
#define FIXTURE_ID "registry-hardware"
#endif
#ifndef FIXTURE_CAP
#define FIXTURE_CAP "test.hardware"
#endif
extern void registry_fixture_event(unsigned, const char*);
extern int registry_fixture_control(unsigned, unsigned);
extern uint64_t registry_fixture_token(unsigned);
extern bool registry_fixture_authorized(unsigned, uint64_t);
static unsigned serial;
static bool started;
static uint64_t saved;
static const registry_fixture_api* dependency;
static int next(void) { return ++serial; }
static const registry_fixture_api api = {0x51ab, next};
static bool start(const risc_provider_dependency_v1* deps, size_t count) {
    assert(!started && serial == 0);
    assert(count == 1 && deps);
#if FIXTURE_SLOT == 0
    assert(!strcmp(deps[0].capability_id, "hardware.device"));
    const risc_hardware_device_v1* device = deps[0].api;
    assert(device && device->instance_id == 11);
#else
    assert(!strcmp(deps[0].capability_id, "test.hardware"));
    dependency = deps[0].api;
    assert(dependency && dependency->marker == api.marker);
#endif
    started = true;
    saved = registry_fixture_token(FIXTURE_SLOT);
    assert(registry_fixture_authorized(FIXTURE_SLOT, saved));
    registry_fixture_event(FIXTURE_SLOT, "start");
    return !registry_fixture_control(FIXTURE_SLOT, 0);
}
static bool quiesce(void) {
    assert(!registry_fixture_authorized(FIXTURE_SLOT, saved));
    if (dependency) assert(dependency->marker == api.marker);
    registry_fixture_event(FIXTURE_SLOT, "quiesce");
    return !registry_fixture_control(FIXTURE_SLOT, 1);
}
static void stop(void) {
    assert(!registry_fixture_authorized(FIXTURE_SLOT, saved));
    if (dependency) assert(dependency->marker == api.marker);
    registry_fixture_event(FIXTURE_SLOT, "stop");
    started = false; dependency = NULL; saved = 0;
}
__attribute__((destructor)) static void fini(void) {
    assert(!started && !registry_fixture_authorized(FIXTURE_SLOT, saved));
    registry_fixture_event(FIXTURE_SLOT, "fini");
}
static const risc_driver_v2 driver = {
    2, sizeof(driver), FIXTURE_ID, FIXTURE_CAP, 1, &api, start, stop, quiesce
};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi) {
    return abi == 2 ? &driver : NULL;
}
