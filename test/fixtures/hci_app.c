#include <RiscRuntimeV1.h>
#include "hci_test_api.h"
#include <assert.h>
#include <string.h>
extern void test_hci_trace(const char*);
extern const char* test_hci_mode(void);
extern void test_hci_recover(void);
extern unsigned test_hci_run(void);
extern void test_hci_save(const void*,const void*);
#ifdef CHILD_APP
#define ID "CHILD"
#else
#define ID "APP"
#endif
__attribute__((constructor)) static void loaded(void){test_hci_trace(ID " loaded");}
__attribute__((destructor)) static void unloaded(void){test_hci_trace(ID " unloaded");}
__attribute__((visibility("default"))) int app_module_init(void){test_hci_trace(ID " init");return 0;}
__attribute__((visibility("default"))) void app_module_fini(void){test_hci_trace(ID " fini");}
__attribute__((visibility("default"))) void app_main(void){
 test_hci_trace(ID " main");const risc_runtime_api_v1* rt=risc_runtime_get_api(1);assert(rt);
 risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)};assert(rt->acquire("test.hci",1,12,&grant));
 const test_hci_api_v1* api=grant.api;const char* mode=test_hci_mode();
#ifdef CHILD_APP
 if(!strcmp(mode,"handoff"))assert(api->poll());
#else
 if(test_hci_run()>1){assert(api->disable());assert(rt->release(&grant));return;}
 test_hci_save((const void*)app_main,grant.api);
 // An accepted launch must be discarded if the subsequent native operation
 // fails and leaves ownership uncertain; callbacks never point into this ELF.
 assert(rt->request_launch("child.elf"));
 if(!strcmp(mode,"open-clean") || !strcmp(mode,"open-retained"))assert(!api->enable());
 else {
  assert(api->enable() && api->poll());
  if(!strcmp(mode,"cleanup-retained") || !strcmp(mode,"cleanup-retry")){
   assert(!api->disable());if(!strcmp(mode,"cleanup-retry")){test_hci_recover();assert(api->disable());}
  }
  if(!strcmp(mode,"poll-retained")){test_hci_recover();assert(!api->poll());}
 }
#endif
 assert(rt->release(&grant));
}
