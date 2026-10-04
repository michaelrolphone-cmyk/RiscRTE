#include <RiscRuntimeV1.h>
#include <assert.h>
#include <string.h>
extern const char* test_update_mode(void);
extern void test_update_inspect(void);
extern unsigned test_update_phase(void);
extern void test_update_next(void);
extern void test_update_retain(void);
static const risc_runtime_api_v1* rt;
__attribute__((visibility("default"))) int app_module_init(void){
 rt=risc_runtime_get_api(1);assert(rt && rt->struct_size>=RISC_RUNTIME_BOOT_CONFIRM_V1_SIZE);
 assert(!rt->confirm_boot());return 0;
}
__attribute__((visibility("default"))) void app_module_fini(void){assert(!rt->confirm_boot());}
__attribute__((visibility("default"))) void app_main(void){
 const char* mode=test_update_mode();
 if(!strcmp(mode,"exit"))return;
#ifdef CHILD_APP
 assert(!rt->confirm_boot());return;
#else
 test_update_inspect();
 if(!strcmp(mode,"queued") && !test_update_phase()){
   test_update_next();assert(rt->request_launch("child.elf"));assert(!rt->confirm_boot());return;
 }
 if(!strcmp(mode,"retained")){test_update_retain();assert(!rt->confirm_boot());return;}
 assert(rt->confirm_boot()==(strcmp(mode,"refuse")!=0));
#endif
}
