#include <RiscRuntimeV1.h>
#include <RiscDiagnosticSourceV1.h>
#include <assert.h>
extern void test_diagnostic_source_app_ran(void);
__attribute__((visibility("default"))) void app_main(void){
 const risc_runtime_api_v1* api=risc_runtime_get_api(1);assert(api);
 risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)};
 assert(!api->acquire(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,0,&grant));
 assert(!api->acquire(RISC_DIAGNOSTIC_SOURCE_CAPABILITY,1,8,&grant));
 assert(api->acquire("test.diagnostic-consumer",1,0,&grant));
 assert(api->release(&grant));test_diagnostic_source_app_ran();
}
