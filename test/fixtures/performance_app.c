#include <RiscRuntimeV1.h>
#include <assert.h>
extern unsigned performance_generation(void);
extern void performance_owner(int);
extern void performance_observe(unsigned, uint32_t);
static const risc_runtime_api_v1* api;
__attribute__((visibility("default"))) int app_module_init(void) {
  api=risc_runtime_get_api(RISC_RUNTIME_API_V1);
  assert(api && api->struct_size>=RISC_RUNTIME_TRACE_V1_SIZE && api->trace);
  performance_observe(10,api->trace(0,4,10));
  return 0;
}
__attribute__((visibility("default"))) void app_module_fini(void) {
  performance_observe(11,api->trace(0,4,11));
}
__attribute__((visibility("default"))) void app_main(void) {
#ifdef PERFORMANCE_CHILD
  performance_observe(2,api->trace(0,4,2));
#else
  const unsigned generation=performance_generation();
  uint32_t id=api->trace(0,1,1);
  performance_observe(1,id);
  performance_owner(0);
  assert(api->trace(id,2,999)==0);
  performance_owner(1);
  assert(api->trace(id,2,2)==id);
  assert(api->trace(id,3,3)==id);
  api->yield_ms(3);
  if(generation<3) assert(api->request_launch(generation==1?"child.elf":"missing.elf"));
#endif
}
