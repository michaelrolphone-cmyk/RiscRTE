#include <RiscRuntimeV1.h>
#include <RiscAppCatalogV1.h>
extern void app_catalog_test_entry(const risc_runtime_api_v1*,int);
__attribute__((visibility("default"))) void app_main(void) {
    app_catalog_test_entry(risc_runtime_get_api(1),
#ifdef CATALOG_CHILD
                          1
#else
                          0
#endif
    );
}
