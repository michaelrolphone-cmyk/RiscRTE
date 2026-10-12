/* Loaded host ELF fixture. Runtime, GraphV2 and CpuPort are production code.
 * This fixture supplies only provider lifecycle and a typed, non-I/O volume. */
#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <RiscProviderSyncV1.h>
#include <RiscStoragePackageAsyncV1.h>
#include <assert.h>
#include <string.h>

extern void native_pin_event(const char *, const char *, const void *);
extern const char *native_pin_mode(void);
extern void native_pin_reenter(const char *);
#ifndef PIN_ID
#define PIN_ID "selected-storage"
#endif

#ifdef PIN_DEPENDENCY
static const unsigned dependency_api = 73;
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    assert(count == 1 && !strcmp(deps[0].capability_id, "hardware.device"));
    assert(((const risc_hardware_device_v1 *)deps[0].api)->instance_id == 7);
    native_pin_event(PIN_ID, "start", &dependency_api);
    return true;
}
static bool quiesce(void) {
    native_pin_event(PIN_ID, "quiesce", &dependency_api);
    return strcmp(native_pin_mode(), "dependency-retained") != 0;
}
static void stop(void) { native_pin_event(PIN_ID, "stop", &dependency_api); }
static const risc_driver_v2 driver = {
    2, sizeof(driver), PIN_ID, "test.pin-dependency", 1,
    &dependency_api, start, stop, quiesce
};
#else
static const risc_hardware_device_v1 *hardware;
static const risc_provider_sync_api_v1 *sync_api;
static const unsigned *dependency;
static risc_storage_volume_api_v1_download volume;
static risc_storage_volume_api_v1 *base(void) {
    return &volume.base.base.fs.state.prepared.base.sleep.terminal.power.volume.base;
}
/* None of these operations are authorized by the graph-pin test. Their
 * presence qualifies suffix discovery; invoking any one is a test failure. */
static int32_t observe(void *context) { (void)context; assert(0); return 0; }
static bool tell(void *context, risc_storage_dir_t handle, uint64_t *position) {
    (void)context; (void)handle; (void)position; assert(0); return false;
}
static bool seek(void *context, risc_storage_dir_t handle, uint64_t position) {
    (void)context; (void)handle; (void)position; assert(0); return false;
}
static int32_t metadata(void *context, const char *path, risc_storage_metadata_v1 *out) {
    (void)context; (void)path; (void)out; assert(0); return 0;
}
static bool truncate_file(void *context, risc_storage_file_t handle, uint64_t size) {
    (void)context; (void)handle; (void)size; assert(0); return false;
}
static bool replace(void *context, const char *from, const char *to) {
    (void)context; (void)from; (void)to; assert(0); return false;
}
static bool recover(void *context, const char *path) {
    (void)context; (void)path; assert(0); return false;
}
static int32_t begin(void *context, uint64_t *session) {
    (void)context; (void)session; assert(0); return 0;
}
static int32_t submit(void *context, uint64_t session,
                      const risc_storage_async_request_v1 *request, uint64_t *operation) {
    (void)context; (void)session; (void)request; (void)operation; assert(0); return 0;
}
static int32_t collect(void *context, uint64_t session, uint64_t operation,
                       risc_storage_async_result_v1 *result) {
    (void)context; (void)session; (void)operation; (void)result; assert(0); return 0;
}
static int32_t end(void *context, uint64_t session) {
    (void)context; (void)session; assert(0); return 0;
}
static int32_t package(void *context, uint64_t session,
                       const risc_storage_package_request_v1 *request, uint64_t *operation) {
    (void)context; (void)session; (void)request; (void)operation; assert(0); return 0;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    assert(count == 3);
    for (size_t i = 0; i < count; ++i) {
        if (!strcmp(deps[i].capability_id, "hardware.device")) hardware = deps[i].api;
        if (!strcmp(deps[i].capability_id, RISC_PROVIDER_SYNC_CAPABILITY)) sync_api = deps[i].api;
        if (!strcmp(deps[i].capability_id, "test.pin-dependency")) dependency = deps[i].api;
    }
    assert(hardware && sync_api && dependency && *dependency == 73);
    assert(sync_api->is_owner(sync_api->context));
    assert(hardware->instance_id == (!strcmp(PIN_ID, "selected-storage") ? 55u : 99u));
    memset(&volume, 0, sizeof(volume));
    base()->api_version = 1;
    base()->struct_size = sizeof(volume);
    base()->context = (void *)hardware;
    volume.base.base.fs.state.state_tag = RISC_STORAGE_STATE_TAG;
    volume.base.base.fs.state.state_version = 1;
    volume.base.base.fs.state.observe = observe;
    volume.base.base.fs.fs_tag = RISC_STORAGE_FS_TAG;
    volume.base.base.fs.fs_version = 1;
    volume.base.base.fs.dir_tell = tell;
    volume.base.base.fs.dir_seek = seek;
    volume.base.base.fs.metadata = metadata;
    volume.base.base.fs.file_truncate = truncate_file;
    volume.base.base.fs.replace_file = replace;
    volume.base.base.fs.recover_replace = recover;
    volume.base.base.async_tag = RISC_STORAGE_ASYNC_TAG;
    volume.base.base.async_version = RISC_STORAGE_ASYNC_VERSION;
    volume.base.base.begin = begin;
    volume.base.base.submit = submit;
    volume.base.base.collect = collect;
    volume.base.base.end = end;
    volume.base.package_tag = RISC_STORAGE_PACKAGE_TAG;
    volume.base.package_version = 1;
    volume.base.submit_package = package;
    volume.download_tag = RISC_STORAGE_DOWNLOAD_TAG;
    volume.download_version = 1;
    if (!strcmp(native_pin_mode(), "invalid-suffix") ||
        !strcmp(native_pin_mode(), "invalid-suffix-retained")) volume.download_tag = 0;
    native_pin_event(PIN_ID, "start", base());
    native_pin_reenter("start");
    return strcmp(native_pin_mode(), "start-failed") &&
           strcmp(native_pin_mode(), "start-retained");
}
static bool quiesce(void) {
    assert(hardware && dependency && *dependency == 73);
    native_pin_event(PIN_ID, "quiesce", base());
    native_pin_reenter("quiesce");
    return strcmp(native_pin_mode(), "release-retained") &&
           strcmp(native_pin_mode(), "start-retained") &&
           strcmp(native_pin_mode(), "invalid-suffix-retained") &&
           strcmp(native_pin_mode(), "destructor-retained");
}
static void stop(void) {
    assert(hardware && dependency && *dependency == 73);
    native_pin_event(PIN_ID, "stop", base());
    native_pin_reenter("stop");
    hardware = NULL;
    sync_api = NULL;
    dependency = NULL;
}
static const risc_driver_v2 driver = {
    2, sizeof(driver), PIN_ID, "storage.volume", 1,
    &volume, start, stop, quiesce
};
#endif
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) { return abi == 2 ? &driver : NULL; }
__attribute__((destructor)) static void unloaded(void) {
    native_pin_event(PIN_ID, "unload", &driver);
}
