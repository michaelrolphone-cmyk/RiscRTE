#include <RiscRuntimeV1.h>
#include <assert.h>
extern void image_pressure_stream_app(const risc_runtime_api_v1*,bool);
extern void image_pressure_stream_lifecycle(bool,bool);
static unsigned fresh;
#ifdef PRESSURE_CHILD
#define IS_CHILD true
#else
#define IS_CHILD false
#endif
__attribute__((visibility("default"))) int app_module_init(void) {
    assert(++fresh==1);image_pressure_stream_lifecycle(true,IS_CHILD);return 0;
}
__attribute__((visibility("default"))) void app_main(void) {
    const risc_runtime_api_v1* api=risc_runtime_get_api(1);assert(api);
    image_pressure_stream_app(api,IS_CHILD);
}
__attribute__((visibility("default"))) void app_module_fini(void) {
    image_pressure_stream_lifecycle(false,IS_CHILD);
}
