/* Real mapped app. Explicit terminal retention is distinct from a bool error. */
#include <RiscRuntimeV1.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
extern void provider_exit_event(const char*);
extern const char* provider_exit_mode(void);
extern unsigned provider_exit_invocation(void);
extern void provider_exit_save_app(const void*);
extern void provider_exit_save_allocation(const char*);
extern void provider_exit_save_api(const risc_runtime_api_v1*);
extern void provider_exit_owner(bool);
static bool retained;
static unsigned witness;
static char* allocation;
static const risc_runtime_api_v1* rt;
static risc_runtime_capability_v1 grant;
struct probe_api { uint32_t api_version, struct_size; bool (*operation)(void); };
static void signal_retained(void) {
  assert(rt->api_version == 1 && rt->struct_size >= RISC_RUNTIME_RETAIN_INVOCATION_V1_SIZE && rt->retain_invocation);
  provider_exit_save_app(&witness);
  assert(rt->retain_invocation());
  retained = true;
  provider_exit_event("app:signaled");
  assert(!risc_runtime_get_api(1) && rt->retain_invocation());
  provider_exit_owner(false); assert(!rt->retain_invocation()); provider_exit_owner(true);
  assert(!rt->diagnostic("forbidden") && !rt->request_launch("child.elf"));
  risc_runtime_health_v1 health = {.struct_size = sizeof(health)};
  risc_runtime_capability_v1 fresh = {.struct_size = sizeof(fresh)};
  assert(!rt->health(&health) && !rt->acquire("test.leaf", 1, 0, &fresh));
  assert(!rt->release(&grant) && !rt->confirm_boot());
  rt->yield_ms(1); /* Raw scheduler only; no provider or ordinary delay work. */
}
__attribute__((constructor)) static void loaded(void) { provider_exit_event("app:loaded"); }
__attribute__((destructor)) static void unloaded(void) { provider_exit_event("app:unloaded"); }
__attribute__((visibility("default"))) int app_module_init(void) {
  rt = risc_runtime_get_api(1); assert(rt);
  provider_exit_save_api(rt);
  const char* mode = provider_exit_mode();
  if (strstr(mode, "retained") || strstr(mode, "signal") || !strcmp(mode, "release-retry")) {
    allocation = malloc(16); assert(allocation); strcpy(allocation, "still retained");
    provider_exit_save_allocation(allocation);
  }
#ifndef CHILD_APP
  if (!strcmp(provider_exit_mode(), "signal-init")) {
    grant.struct_size = sizeof(grant);
    assert(rt->acquire("test.leaf", 1, 0, &grant));
    assert(rt->request_launch("child.elf"));
    signal_retained();
  }
#endif
  return 0;
}
__attribute__((visibility("default"))) void app_module_fini(void) {
#ifndef CHILD_APP
  if (!strcmp(provider_exit_mode(), "fini-release-retained")) {
    provider_exit_event("app:fini-release"); assert(!rt->release(&grant)); return;
  }
  if (!strcmp(provider_exit_mode(), "signal-fini")) {
    provider_exit_event("app:fini-signal"); signal_retained(); return;
  }
#endif
  provider_exit_event(retained ? "app:fini-skipped" : "app:fini");
  if (!retained) { free(allocation); allocation = NULL; }
}
__attribute__((visibility("default"))) void app_main(void) {
  const char* mode = provider_exit_mode();
#ifdef CHILD_APP
  provider_exit_event("child:entry");
  if (!strcmp(mode, "signal-child")) {
    assert(rt->request_launch("default.elf")); signal_retained();
  }
#else
  provider_exit_event("app:entry");
  if (provider_exit_invocation() > 1) return;
  provider_exit_save_app(&witness);
  assert(rt->request_launch("child.elf"));
  if (!strcmp(mode, "signal-no-grants")) { signal_retained(); return; }
  grant.struct_size = sizeof(grant);
  if (!rt->acquire("test.leaf", 1, 0, &grant)) {
    assert(!strcmp(mode, "start-retained") || !strcmp(mode, "start-rolled-back"));
    assert(!grant.slot && !grant.api);
    retained = true;
    provider_exit_event("app:acquire-false");
    return;
  }
  const struct probe_api* api = grant.api;
  assert(api && api->api_version == 1 && api->struct_size == sizeof(*api));
  if (!strcmp(mode, "signal-main")) { signal_retained(); return; }
  if (!strcmp(mode, "signal-fini") || !strcmp(mode, "signal-child") || !strcmp(mode, "fini-release-retained")) return;
  if (!strcmp(mode, "foreign-denied")) {
    provider_exit_owner(false);
    assert(!rt->retain_invocation());
    provider_exit_owner(true);
    assert(risc_runtime_get_api(1) && rt->diagnostic("owner-restored"));
    assert(rt->release(&grant)); return;
  }
  if (!strcmp(mode, "release-retained") || !strcmp(mode, "release-retry") || !strcmp(mode, "release-recovered")) {
    assert(!rt->release(&grant));
    provider_exit_event("app:release-false");
    if (!strcmp(mode, "release-recovered")) {
      assert(rt->release(&grant));
      provider_exit_event("app:release-recovered"); return;
    }
    retained = true;
  } else {
    assert(!api->operation());
    retained = true;
    provider_exit_event("app:operation-false");
    if (!strcmp(mode, "operation-retained-eager") || !strcmp(mode, "operation-retained-demand") ||
        !strcmp(mode, "cpu-gpio-retained-eager")) signal_retained();
  }
#endif
}
