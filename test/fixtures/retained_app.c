/* Host-only lifecycle witness. Each image is a real independently mapped app. */
#include <RiscRuntimeV1.h>
#include <RiscKeyValueV1.h>
#include <RiscDeepSleepV1.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

extern void test_retained_trace(const char*);
extern const char* test_retained_mode(void);
extern unsigned test_retained_default_run(void);
extern void test_retained_save(const risc_runtime_api_v1*,
                              const risc_runtime_capability_v1*,
                              const risc_key_value_v1*, const void*, const char*);

#if defined(DEFAULT_APP)
#define APP "DEFAULT"
#elif defined(QUEUED_APP)
#define APP "QUEUED"
#else
#define APP "CLOCK"
static const struct probe_api {
  uint32_t api_version, struct_size;
  int32_t (*enter)(const char*);
} *probe;
#endif

static const risc_runtime_api_v1* rt;
static unsigned runs;
static char* allocation;

#if !defined(DEFAULT_APP) && !defined(QUEUED_APP)
static void prepare_clock(void) {
  risc_runtime_capability_v1 grant = {.struct_size = sizeof(grant)};
  risc_runtime_capability_v1 kv_grant = {.struct_size = sizeof(kv_grant)};
  assert(rt->acquire("test.retained", 1, 7, &grant));
  probe = grant.api;
  assert(probe && probe->api_version == 1 && probe->struct_size == sizeof(*probe));
  assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY, 1, 1, &kv_grant));
  const risc_key_value_v1* kv = kv_grant.api;
  assert(kv && kv->api_version == 1 && kv->struct_size == sizeof(*kv));
  assert(kv->put(kv->context, "probe", "x", 1) == RISC_KEY_VALUE_OK);
  test_retained_save(rt, &grant, kv, &runs, allocation);
  /* Queue while safe: the barrier must discard this existing request. */
  assert(rt->request_launch("queued.elf"));
  assert(rt->diagnostic("CLOCK queued-child"));
}
#endif

__attribute__((constructor)) static void loaded(void) {
  test_retained_trace(APP " loaded");
}
__attribute__((destructor)) static void unloaded(void) {
  test_retained_trace(APP " unloaded");
}
__attribute__((visibility("default"))) int app_module_init(void) {
  rt = risc_runtime_get_api(1);
  assert(rt && rt->diagnostic(APP " init"));
  allocation = malloc(16);
  assert(allocation);
  strcpy(allocation, "still retained");
#if !defined(DEFAULT_APP) && !defined(QUEUED_APP)
  if (!strcmp(test_retained_mode(), "init-retained")) {
    prepare_clock();
    assert(probe->enter("unexpected-return") == RISC_DEEP_SLEEP_RETAINED);
    assert(rt->diagnostic("CLOCK init-retained"));
  }
#endif
  return 0;
}
__attribute__((visibility("default"))) void app_module_fini(void) {
  assert(rt->diagnostic(APP " fini"));
#if !defined(DEFAULT_APP) && !defined(QUEUED_APP)
  if (!strcmp(test_retained_mode(), "fini-retained")) {
    assert(probe->enter("unexpected-return") == RISC_DEEP_SLEEP_RETAINED);
    test_retained_trace("CLOCK fini-retained");
    return;
  }
#endif
  free(allocation);
  allocation = NULL;
}
__attribute__((visibility("default"))) void app_main(void) {
  assert(++runs == 1); /* A default reload must have fresh data/BSS. */
  assert(rt->diagnostic(APP " main"));
#if defined(DEFAULT_APP)
  if (test_retained_default_run() == 1) {
    assert(rt->request_launch("clock.elf"));
    assert(rt->diagnostic("DEFAULT queued-clock"));
  }
#elif !defined(QUEUED_APP)
  prepare_clock();
  if (!strcmp(test_retained_mode(), "fini-retained")) {
    assert(rt->diagnostic("CLOCK defer-sleep-to-fini"));
    return;
  }
  const int32_t result = probe->enter(test_retained_mode());
  if (!strcmp(test_retained_mode(), "ordinary-refusal") ||
      !strcmp(test_retained_mode(), "held-output")) {
    assert(result == RISC_DEEP_SLEEP_PLATFORM);
    assert(rt->diagnostic("CLOCK sleep-refused"));
  } else {
    assert(result == RISC_DEEP_SLEEP_RETAINED);
    assert(rt->diagnostic("CLOCK sleep-retained"));
  }
  /* Deliberately leave both grants live. Boot references must not hide poison. */
#endif
}
