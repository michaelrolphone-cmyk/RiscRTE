/* Real host-mapped ABI-2 fixture for host-only module lease ordering. The
 * observation callbacks are test executable exports, never Runtime imports. */
#include "RiscProviderV2.h"
#include <string.h>

#ifndef FIXTURE_SLOT
#define FIXTURE_SLOT 0
#endif
#ifndef FIXTURE_ID
#define FIXTURE_ID "fixture-lease-root"
#endif
#ifndef FIXTURE_CAPABILITY
#define FIXTURE_CAPABILITY "cap.lease-root"
#endif
#ifndef FIXTURE_DESCRIPTOR_ABI
#define FIXTURE_DESCRIPTOR_ABI RISC_PROVIDER_DRIVER_ABI_V2
#endif

extern void provider_lease_event(unsigned slot, const char *event);
extern int provider_lease_control(unsigned slot, unsigned control);
extern uint64_t provider_lease_token(unsigned slot);
extern int provider_lease_authorized(unsigned slot, uint64_t token);

static int value = 42;
static const risc_provider_dependency_v1 *retained;
static size_t retained_count;
static uint64_t saved_token;
static bool started;
#ifdef FIXTURE_STREAMS
static const risc_stream_provider_v1 *retained_streams;
#endif

static bool dependency_valid(void) {
#ifdef FIXTURE_REQUIRE
    return retained && retained_count == 1 && retained[0].capability_id &&
        strcmp(retained[0].capability_id, FIXTURE_REQUIRE) == 0 &&
        retained[0].api_version == 1 && retained[0].api &&
        *(const int *)retained[0].api == 42;
#else
    return !retained && !retained_count;
#endif
}

static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    provider_lease_event(FIXTURE_SLOT, "start");
    if (started) __builtin_trap();
    retained = dependencies;
    retained_count = count;
    saved_token = provider_lease_token(FIXTURE_SLOT);
    if (!dependency_valid()) __builtin_trap();
#ifdef FIXTURE_STREAMS
    if (!retained_streams || !retained_streams->context) __builtin_trap();
#endif
    started = true; /* Model a resource acquired even when start rejects. */
    return !provider_lease_control(FIXTURE_SLOT, 0);
}

static bool quiesce(void) {
    provider_lease_event(FIXTURE_SLOT, "quiesce");
    if (saved_token && provider_lease_authorized(FIXTURE_SLOT, saved_token))
        __builtin_trap();
    if (started && !dependency_valid()) __builtin_trap();
#ifdef FIXTURE_STREAMS
    /* The borrowed stream table, like dependencies, stays allocated to stop. */
    if (retained_streams && !retained_streams->context) __builtin_trap();
#endif
    return provider_lease_control(FIXTURE_SLOT, 2) != 0;
}

static void stop(void) {
    provider_lease_event(FIXTURE_SLOT, "stop");
    if (saved_token && provider_lease_authorized(FIXTURE_SLOT, saved_token))
        __builtin_trap();
    if (started && !dependency_valid()) __builtin_trap();
    started = false;
    retained = 0;
    retained_count = 0;
    saved_token = 0;
#ifdef FIXTURE_STREAMS
    retained_streams = 0;
#endif
}

static bool last_error(char *out, size_t capacity) {
    const char detail[] = "fixture start rejected";
    provider_lease_event(FIXTURE_SLOT, "diagnostics");
    if (saved_token && provider_lease_authorized(FIXTURE_SLOT, saved_token))
        __builtin_trap();
    if (!out || capacity < sizeof(detail)) return false;
    memcpy(out, detail, sizeof(detail));
    return true;
}

#ifdef FIXTURE_STREAMS
static bool bind_streams(const risc_stream_provider_v1 *host) {
    provider_lease_event(FIXTURE_SLOT, "bind");
    if (!host || host->api_version != RISC_STREAM_PROVIDER_API_V1 ||
        host->struct_size < sizeof(*host) || !host->context) __builtin_trap();
    retained_streams = host;
    return !provider_lease_control(FIXTURE_SLOT, 1);
}
#endif

static const risc_driver_streams_v2 driver = {{
    FIXTURE_DESCRIPTOR_ABI, sizeof(risc_driver_streams_v2),
    FIXTURE_ID, FIXTURE_CAPABILITY, 1, &value, start, stop, quiesce
}, last_error,
#ifdef FIXTURE_STREAMS
    bind_streams
#else
    0
#endif
};

__attribute__((visibility("default")))
#ifdef FIXTURE_NO_ENTRY
const risc_driver_v2 *fixture_without_entry(uint32_t abi) {
#else
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
#endif
    provider_lease_event(FIXTURE_SLOT, "entry");
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver.driver : 0;
}
