/* Real mapped app: local retention suppresses its fini, not Runtime cleanup. */
#include <RiscRuntimeV1.h>
#include <assert.h>
#include <string.h>
extern void provider_exit_event(const char*);
extern const char* provider_exit_mode(void);
extern unsigned provider_exit_invocation(void);
extern void provider_exit_save_app(const void*);
static bool retained;
#ifndef CHILD_APP
static unsigned witness;
#endif
struct probe_api { uint32_t api_version, struct_size; bool (*operation)(void); };
__attribute__((constructor)) static void loaded(void) { provider_exit_event("app:loaded"); }
__attribute__((destructor)) static void unloaded(void) { provider_exit_event("app:unloaded"); }
__attribute__((visibility("default"))) int app_module_init(void) { return 0; }
__attribute__((visibility("default"))) void app_module_fini(void) {
  provider_exit_event(retained ? "app:fini-skipped" : "app:fini");
}
__attribute__((visibility("default"))) void app_main(void) {
#ifdef CHILD_APP
  provider_exit_event("child:entry");
#else
  provider_exit_event("app:entry");
  if (provider_exit_invocation() > 1) return;
  provider_exit_save_app(&witness);
  const risc_runtime_api_v1* rt = risc_runtime_get_api(1);
  assert(rt && rt->request_launch("child.elf"));
  risc_runtime_capability_v1 grant = {.struct_size = sizeof(grant)};
  const char* mode = provider_exit_mode();
  if (!rt->acquire("test.leaf", 1, 0, &grant)) {
    assert(!strcmp(mode, "start-retained") || !strcmp(mode, "start-rolled-back"));
    assert(!grant.slot && !grant.api);
    retained = true;
    provider_exit_event("app:acquire-false");
    return;
  }
  const struct probe_api* api = grant.api;
  assert(api && api->api_version == 1 && api->struct_size == sizeof(*api));
  if (!strcmp(mode, "release-retained") || !strcmp(mode, "release-retry")) {
    assert(!rt->release(&grant));
    retained = true;
    provider_exit_event("app:release-false");
  } else {
    assert(!api->operation());
    retained = true;
    provider_exit_event("app:operation-false");
  }
#endif
}
