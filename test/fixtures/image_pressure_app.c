#include <RiscRuntimeV1.h>
#include <assert.h>
extern void image_pressure_app(const risc_runtime_api_v1*);
static unsigned fresh;
__attribute__((visibility("default"))) void app_main(void) {
    assert(++fresh==1);
    const risc_runtime_api_v1* api=risc_runtime_get_api(1);assert(api);
    image_pressure_app(api);
}
