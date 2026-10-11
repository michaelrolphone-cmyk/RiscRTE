#include <RiscRuntimeV1.h>
#include <assert.h>
extern void storage_work_test_begin(void);
extern void storage_work_test_result(int);
__attribute__((visibility("default"))) void app_main(void){
 const risc_runtime_api_v1*rt=risc_runtime_get_api(1);assert(rt);
 storage_work_test_begin();
 risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)};
 int acquired=rt->acquire("platform.clock",1,0,&grant);
 storage_work_test_result(acquired);
 if(acquired)assert(rt->release(&grant));
}
