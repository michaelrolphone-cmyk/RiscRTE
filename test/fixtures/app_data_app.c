#include <RiscRuntimeV1.h>
#include <RiscAppDataV1.h>
#include <assert.h>
#include <string.h>
extern void app_data_test_owner(int);
extern void app_data_test_keep(risc_app_data_v1);
extern void app_data_test_revoked(void);
extern int app_data_test_phase(void);
extern int app_data_test_fault_mode(void);
extern void app_data_test_fault(void);
extern void app_data_test_next(void);
__attribute__((visibility("default"))) void app_main(void){
 const risc_runtime_api_v1*rt=risc_runtime_get_api(1);assert(rt);app_data_test_revoked();
 risc_runtime_capability_v1 g={.struct_size=sizeof(g)};uint32_t size=0;uint64_t revision=0;
#ifdef CHILD
 assert(!rt->acquire(RISC_APP_DATA_CAPABILITY,1,1,&g));app_data_test_next();return;
#else
 risc_runtime_capability_v1 clock={.struct_size=sizeof(clock)};assert(rt->acquire("platform.clock",1,0,&clock));
 assert(!rt->acquire(RISC_APP_DATA_CAPABILITY,2,1,&g));assert(!rt->acquire(RISC_APP_DATA_CAPABILITY,1,2,&g));
 assert(rt->acquire(RISC_APP_DATA_CAPABILITY,1,1,&g));const risc_app_data_v1*v=g.api;
 assert(v && v->api_version==1 && v->struct_size==sizeof(*v));
 risc_runtime_capability_v1 second={.struct_size=sizeof(second)};assert(!rt->acquire(RISC_APP_DATA_CAPABILITY,1,1,&second));
 app_data_test_owner(0);assert(v->stat(v->context,"state.json",&size,&revision)==RISC_APP_DATA_CONTEXT);app_data_test_owner(1);
 if(app_data_test_fault_mode()){
  app_data_test_keep(*v);app_data_test_fault();assert(v->stat(v->context,"state.json",&size,&revision)==RISC_APP_DATA_RETAINED);
  assert(!rt->release(&g) && !rt->release(&clock) && !rt->request_launch("child.elf"));assert(!rt->acquire("platform.clock",1,0,&second));assert(!rt->acquire(RISC_APP_DATA_CAPABILITY,1,1,&second));rt->yield_ms(1);return;
 }
 int32_t status=v->stat(v->context,"state.json",&size,&revision);assert(status==0 || status==RISC_APP_DATA_NOT_FOUND);
 assert(v->replace(v->context,"state.json",revision,"complete JSON",13)==0);
 assert(v->stat(v->context,"state.json",&size,&revision)==0 && size==13 && revision);
 char bytes[14]={0};assert(v->read(v->context,"state.json",revision,bytes,13,&size,&revision)==0 && size==13 && !strcmp(bytes,"complete JSON"));
 risc_app_data_v1 old=*v;assert(rt->release(&g));assert(old.stat(old.context,"state.json",&size,&revision)==RISC_APP_DATA_CONTEXT);
 assert(rt->acquire(RISC_APP_DATA_CAPABILITY,1,1,&g));v=g.api;assert(v->context!=old.context);
 assert(old.stat(old.context,"state.json",&size,&revision)==RISC_APP_DATA_CONTEXT);app_data_test_keep(*v);
 if(!app_data_test_phase()){app_data_test_next();assert(rt->request_launch("child.elf"));}
#endif
}
__attribute__((visibility("default"))) int app_module_init(void){return 0;}
__attribute__((visibility("default"))) void app_module_fini(void){assert(!app_data_test_fault_mode());}
