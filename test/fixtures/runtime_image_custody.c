#include <assert.h>
#include <RiscProviderV2.h>

extern void runtime_image_custody_event(unsigned, const void*);

#ifdef CUSTODY_APP
__attribute__((visibility("default"))) void app_main(void) {
    runtime_image_custody_event(7, 0);
}
#else
static const unsigned api[2] = {1, 0x51a7c0de};
static bool start(const risc_provider_dependency_v1* deps, size_t count) {
#ifdef CUSTODY_CHILD
    assert(count == 1 && deps && deps[0].api);
    assert(((const unsigned*)deps[0].api)[1] == 0x51a7c0de);
    runtime_image_custody_event(4, deps[0].api);
#else
    assert(!count && !deps);
    runtime_image_custody_event(1, api);
#endif
    return true;
}
static bool quiesce(void) {
    runtime_image_custody_event(
#ifdef CUSTODY_CHILD
        5,
#else
        2,
#endif
        api);
    return true;
}
static void stop(void) {
    runtime_image_custody_event(
#ifdef CUSTODY_CHILD
        6,
#else
        3,
#endif
        api);
}
static const risc_driver_v2 driver = {
    2, sizeof(driver),
#ifdef CUSTODY_CHILD
    "custody-child", "test.child",
#else
    "custody-root", "test.root",
#endif
    1, api, start, stop, quiesce
};
__attribute__((visibility("default"))) const risc_driver_v2* t5_driver_get(uint32_t abi) {
    return abi == 2 ? &driver : 0;
}
#endif
