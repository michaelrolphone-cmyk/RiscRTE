#include <RiscRuntimeV1.h>
#include <RiscDeepSleepV1.h>
#include <RiscLightSleepV1.h>
#include "radio_test_api.h"
#include <assert.h>
#include <string.h>
extern void test_radio_trace(const char*);
extern const char* test_radio_mode(void);
extern void test_radio_recover(void);
extern unsigned test_radio_run(void);
extern void test_radio_save(const void*,const void*);
#ifdef CHILD_APP
#define ID "CHILD"
#else
#define ID "APP"
#endif
__attribute__((constructor)) static void loaded(void){test_radio_trace(ID " loaded");}
__attribute__((destructor)) static void unloaded(void){test_radio_trace(ID " unloaded");}
__attribute__((visibility("default"))) int app_module_init(void){test_radio_trace(ID " init");return 0;}
__attribute__((visibility("default"))) void app_module_fini(void){test_radio_trace(ID " fini");}
__attribute__((visibility("default"))) void app_main(void){
 test_radio_trace(ID " main");
#ifndef CHILD_APP
 if(test_radio_run()>1)return;
 const risc_runtime_api_v1* rt=risc_runtime_get_api(1);assert(rt);
 risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)},sleepGrant={.struct_size=sizeof(sleepGrant)},storageGrant={.struct_size=sizeof(storageGrant)};
 assert(rt->acquire("test.radio",1,12,&grant));assert(rt->acquire("test.sleep",1,7,&sleepGrant));
 assert(rt->acquire("test.alert",1,0,&storageGrant));
 const test_radio_storage_v1* storage=storageGrant.api;assert(storage->check(true));
 const test_radio_api_v1* radio=grant.api;const test_radio_sleep_v1* sleep=sleepGrant.api;
 test_radio_save((const void*)app_main,grant.api);
 const char* mode=test_radio_mode();
 // Queue before activity: retention must discard an already accepted launch.
 assert(rt->request_launch("child.elf"));
 if(!strcmp(mode,"idle")){assert(radio->leave());}
 else if(!strcmp(mode,"join-failed")){assert(!radio->join());}
 else if(!strcmp(mode,"scan-failed")){assert(!radio->scan());}
 else if(!strcmp(mode,"scan-clean") || !strcmp(mode,"scan-retained") || !strcmp(mode,"poll-failed")){
  assert(radio->scan());assert(storage->check(true));
  assert(sleep->sleep(false)==RISC_LIGHT_SLEEP_BUSY && sleep->sleep(true)==RISC_DEEP_SLEEP_BUSY);
  if(!strcmp(mode,"scan-clean"))assert(radio->cancel());
  if(!strcmp(mode,"poll-failed")){assert(!radio->poll());assert(storage->check(false));}
 }
 else {
  assert(radio->join());assert(storage->check(true));
  assert(sleep->sleep(false)==RISC_LIGHT_SLEEP_BUSY && sleep->sleep(true)==RISC_DEEP_SLEEP_BUSY);
  if(!strcmp(mode,"state-failed")){assert(!radio->state());assert(storage->check(false));}
  if(!strcmp(mode,"addresses-failed")){assert(!radio->addresses());assert(storage->check(false));}
  if(!strcmp(mode,"join-clean"))assert(radio->leave());
  if(!strcmp(mode,"cleanup-retained") || !strcmp(mode,"cleanup-retry")){
   assert(!radio->leave());assert(storage->check(false));
   assert(sleep->sleep(false)==RISC_LIGHT_SLEEP_RETAINED && sleep->sleep(true)==RISC_DEEP_SLEEP_RETAINED);
   if(!strcmp(mode,"cleanup-retry")){test_radio_recover();assert(radio->leave());assert(storage->check(false));}
  }
 }
 const bool retained=!strcmp(mode,"join-retained") || !strcmp(mode,"scan-retained") || !strcmp(mode,"cleanup-retained") || !strcmp(mode,"state-failed") || !strcmp(mode,"addresses-failed") || !strcmp(mode,"poll-failed");
 if(!retained){assert(sleep->sleep(false)==RISC_LIGHT_SLEEP_OK);assert(sleep->sleep(true)==RISC_DEEP_SLEEP_PLATFORM);}
 // App grants can be released without unloading a boot-owned provider. Actual
 // native state must determine finalization safety, not the grant count.
 if(strcmp(mode,"cleanup-retry") && !retained)assert(storage->check(true));
 assert(rt->release(&storageGrant));assert(rt->release(&sleepGrant));assert(rt->release(&grant));
#endif
}
