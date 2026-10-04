#include <RiscRuntimeV1.h>
#include <RiscKeyValueV1.h>
#include <RiscBoundKeyValueV1.h>
#include "bound_key_value_test_api.h"
#include <assert.h>
#include <string.h>
extern unsigned test_bound_app_enter(void);
extern void test_bound_probe(void);
extern void test_bound_live(void);
extern int test_bound_retention(void);
static unsigned runs;
__attribute__((visibility("default"))) void app_main(void) {
  assert(++runs == 1); /* Every handoff uses a fresh dynamic app mapping. */
  const risc_runtime_api_v1* rt = risc_runtime_get_api(1); assert(rt);
  const unsigned phase = test_bound_app_enter();
  risc_runtime_capability_v1 providers[2] = {{.struct_size = sizeof(providers[0])}, {.struct_size = sizeof(providers[1])}};
  assert(!rt->acquire(RISC_BOUND_KEY_VALUE_CAPABILITY, 1, 0, &providers[0]));
  assert(rt->acquire("test.bound.first", 1, 0, &providers[0]));
  assert(rt->acquire("test.bound.second", 1, 0, &providers[1]));
  for (unsigned i = 0; i < 2; ++i) {
    const test_bound_provider_api* provider = providers[i].api;
    assert(provider && provider->api_version == 1 && provider->struct_size == sizeof(*provider));
    provider->check();
  }
  if (!phase) {
    test_bound_probe();
    risc_runtime_capability_v1 grant = {.struct_size = sizeof(grant)};
    assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY, 1, 4, &grant));
    assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY, 1, 3, &grant));
    const risc_key_value_v1* kv = grant.api;
    assert(kv->put(kv->context, "alarm_cfg", "config", 6) == RISC_KEY_VALUE_OK);
    assert(kv->put(kv->context, "alarm_occ", "forged", 6) == RISC_KEY_VALUE_OK);
    assert(rt->release(&grant));
    /* Namespace3's whole-namespace authority cannot touch namespace4. */
    ((const test_bound_provider_api*)providers[0].api)->check();
  }
  for (unsigned i = 0; i < 2; ++i) assert(rt->release(&providers[i]));
  test_bound_live(); /* Boot references retain both provider leases. */
  if (test_bound_retention()) return;
  if (phase == 0) assert(rt->request_launch("child.elf"));
  if (phase == 1) assert(rt->request_launch("third.elf"));
}
