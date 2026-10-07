#include <RiscRuntimeV1.h>
#include "provider_sync_api.h"
#include <assert.h>
#include <string.h>
extern void test_sync_trace(const char*);
extern const char* test_sync_mode(void);
extern void test_sync_save(const void*,const void*);
__attribute__((destructor)) static void unloaded(void){test_sync_trace("app-unloaded");}
__attribute__((visibility("default"))) void app_main(void){
 const risc_runtime_api_v1* runtime=risc_runtime_get_api(1);assert(runtime);
 risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)};
 assert(runtime->acquire("test.sync",1,1,&grant));
 const test_sync_api* api=grant.api;test_sync_save((const void*)app_main,api);
 assert(api->hold());
 if(strcmp(test_sync_mode(),"retained"))assert(api->release());
 assert(runtime->release(&grant));
}
