#include <RiscRuntimeV1.h>
#include <RiscDeepSleepV1.h>
#include <RiscLightSleepV1.h>
#include "i2s_test_api.h"
#include <assert.h>
#include <string.h>
extern void test_i2s_trace(const char*);
extern const char* test_i2s_mode(void);
extern void test_i2s_recover(void);
extern unsigned test_i2s_run(void);
extern void test_i2s_save(const void*,const void*);
#ifdef CHILD_APP
#define ID "CHILD"
#else
#define ID "APP"
#endif
__attribute__((constructor)) static void loaded(void){test_i2s_trace(ID " loaded");}
__attribute__((destructor)) static void unloaded(void){test_i2s_trace(ID " unloaded");}
__attribute__((visibility("default"))) int app_module_init(void){return 0;}
__attribute__((visibility("default"))) void app_module_fini(void){test_i2s_trace(ID " fini");}
__attribute__((visibility("default"))) void app_main(void){
#ifndef CHILD_APP
 if(test_i2s_run()>1)return;
 const risc_runtime_api_v1* rt=risc_runtime_get_api(1);assert(rt);
 const char* mode=test_i2s_mode();bool rx=!strncmp(mode,"rx-",3);
 risc_runtime_capability_v1 audioGrant={.struct_size=sizeof(audioGrant)},sleepGrant={.struct_size=sizeof(sleepGrant)},storageGrant={.struct_size=sizeof(storageGrant)};
 assert(rt->acquire("test.audio",1,rx?13:12,&audioGrant));assert(rt->acquire("test.sleep",1,7,&sleepGrant));assert(rt->acquire("test.alert",1,0,&storageGrant));
 const test_i2s_v1* audio=audioGrant.api;const test_i2s_storage_v1* storage=storageGrant.api;const test_i2s_sleep_v1* sleep=sleepGrant.api;
 test_i2s_save((const void*)app_main,audioGrant.api);assert(storage->check(true));
 assert(rt->request_launch("child.elf"));
 bool broken=false;const bool retained=strstr(mode,"retained")!=NULL;
 if(strstr(mode,"open-")){
  assert(!audio->open());broken=retained;
  assert(storage->check(!broken));
 }else{
  assert(audio->open());assert(storage->check(true));
  assert(sleep->sleep(false)==RISC_LIGHT_SLEEP_BUSY && sleep->sleep(true)==RISC_DEEP_SLEEP_BUSY);
  if(strstr(mode,"error") || strstr(mode,"partial") || strstr(mode,"oversize")){
   assert(!audio->transfer());broken=true;assert(storage->check(false));assert(!audio->transfer());
   assert(sleep->sleep(false)==RISC_LIGHT_SLEEP_RETAINED && sleep->sleep(true)==RISC_DEEP_SLEEP_RETAINED);
   if(!retained)assert(audio->close());
  }else if(strstr(mode,"close-")){
   assert(audio->transfer());assert(!audio->close());broken=true;assert(storage->check(false));assert(!audio->transfer());
   if(!retained){test_i2s_recover();assert(audio->close());}
  }else{
   assert(audio->transfer());assert(storage->check(true));if(!retained)assert(audio->close());
  }
 }
 // Revocation is permanent even after a clean recovery. A healthy, open
 // stream must never revoke this boot-held provider's storage authority.
 assert(storage->check(!broken));
 if(!retained){assert(sleep->sleep(false)==RISC_LIGHT_SLEEP_OK);assert(sleep->sleep(true)==RISC_DEEP_SLEEP_PLATFORM);}
 assert(rt->release(&storageGrant));assert(rt->release(&sleepGrant));assert(rt->release(&audioGrant));
#endif
}
