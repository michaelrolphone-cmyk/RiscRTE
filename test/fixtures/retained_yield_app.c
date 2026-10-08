/* Actual mapped invocation; retained loops use only the table cached at init. */
#include <RiscRuntimeV1.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
extern const char* retained_yield_mode(void);
extern void retained_yield_event(const char*);
extern void retained_yield_save(const risc_runtime_api_v1*, const void*, const char*);
extern void retained_yield_owner(bool);
extern void retained_yield_check(bool, bool);
extern void retained_yield_native_busy(bool);
#ifndef CHILD_APP
static const risc_runtime_api_v1* rt;
static risc_runtime_capability_v1 grant;
static unsigned witness;
static char* allocation;
static void yields(bool retained) {
  retained_yield_check(retained, false);
  rt->yield_ms(0);rt->yield_ms(1);rt->yield_ms(20);
  rt->yield_ms(50);rt->yield_ms(51);rt->yield_ms(UINT32_MAX);
  /* Bounded sample of the legacy helper's cached yield_ms(50) loop. */
  for(unsigned i=0;i<3;++i)rt->yield_ms(50);
  retained_yield_owner(false);rt->yield_ms(50);retained_yield_owner(true);
  retained_yield_check(retained, true);
}
static void retain(void) {
  assert(rt->request_launch("child.elf"));
  assert(rt->retain_invocation());
  assert(!risc_runtime_get_api(1) && rt->retain_invocation());
  retained_yield_event("app:retained");
  yields(true);
  assert(!rt->request_launch("child.elf") && !rt->diagnostic("forbidden"));
  risc_runtime_capability_v1 denied={.struct_size=sizeof(denied)};
  risc_runtime_health_v1 health={.struct_size=sizeof(health)};
  assert(!rt->acquire("test.yield",1,0,&denied) && !rt->release(&grant));
  assert(!rt->health(&health) && !rt->confirm_boot());
}
#endif
__attribute__((constructor)) static void loaded(void) { retained_yield_event("app:loaded"); }
__attribute__((destructor)) static void unloaded(void) { retained_yield_event("app:unloaded"); }
__attribute__((visibility("default"))) int app_module_init(void) {
#ifdef CHILD_APP
  retained_yield_event("child:init");
#else
  rt=risc_runtime_get_api(1);assert(rt);
  allocation=malloc(16);assert(allocation);strcpy(allocation,"still retained");
  retained_yield_save(rt,&witness,allocation);
  grant.struct_size=sizeof(grant);assert(rt->acquire("test.yield",1,0,&grant));
  if(!strcmp(retained_yield_mode(),"signal-init"))retain();
#endif
  return 0;
}
__attribute__((visibility("default"))) void app_main(void) {
#ifdef CHILD_APP
  retained_yield_event("child:entry");
#else
  retained_yield_event("app:entry");
  const char* mode=retained_yield_mode();
  yields(false);
  if(!strcmp(mode,"normal") || !strcmp(mode,"signal-fini"))return;
  if(!strcmp(mode,"native-busy")) {
    /* Exit-unsafe need not mean terminal: ordinary yields must still work. */
    retained_yield_native_busy(true);yields(false);retained_yield_native_busy(false);
    return;
  }
  if(!strcmp(mode,"graph-retained")) {
    assert(rt->request_launch("child.elf"));
    assert(!rt->release(&grant));
    retained_yield_event("app:graph-retained");
    yields(true);
    assert(!risc_runtime_get_api(1) && !rt->request_launch("child.elf"));
    return;
  }
  retain();
#endif
}
__attribute__((visibility("default"))) void app_module_fini(void) {
#ifndef CHILD_APP
  if(!strcmp(retained_yield_mode(),"signal-fini")){retain();return;}
  retained_yield_event("app:fini");
  free(allocation);allocation=0;
#endif
}
