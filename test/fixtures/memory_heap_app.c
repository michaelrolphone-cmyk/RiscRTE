#include "RiscRuntimeV1.h"
#include "RiscMemoryHeapV1.h"
#include "RiscRandomV1.h"
#include <assert.h>
extern void random_test_keep(const risc_random_api_v1*);
extern void memory_test_owner(int);extern void memory_test_keep(const risc_memory_heap_api_v1*);
__attribute__((visibility("default"))) void app_main(void){
 const risc_runtime_api_v1* rt=risc_runtime_get_api(1);assert(rt);
 risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)};
 assert(!rt->acquire("memory.heap",2,0,&grant));assert(!rt->acquire("memory.heap",1,1,&grant));
 assert(rt->acquire("memory.heap",1,0,&grant));
 const risc_memory_heap_api_v1* api=grant.api;assert(api->api_version==1&&api->struct_size==sizeof(*api));
 risc_memory_heap_snapshot_v1 s={.struct_size=sizeof(s)};
 assert(api->snapshot(api->context,&s)==0&&s.total_bytes==10000&&s.free_bytes==7000&&s.largest_block==4096);
 s.memory_class=RISC_MEMORY_EXTERNAL;assert(api->snapshot(api->context,&s)==RISC_MEMORY_UNSUPPORTED);
 s.memory_class=99;assert(api->snapshot(api->context,&s)==RISC_MEMORY_INVALID);
 s.memory_class=0;memory_test_owner(0);assert(api->snapshot(api->context,&s)==RISC_MEMORY_CONTEXT);memory_test_owner(1);
 risc_memory_heap_api_v1 old=*api;assert(rt->release(&grant));assert(old.snapshot(old.context,&s)==RISC_MEMORY_CONTEXT);
 assert(rt->acquire("memory.heap",1,0,&grant));assert(old.snapshot(old.context,&s)==RISC_MEMORY_CONTEXT);memory_test_keep(grant.api);
 risc_runtime_capability_v1 rng={.struct_size=sizeof(rng)};
 assert(!rt->acquire("random.bytes",2,0,&rng));assert(!rt->acquire("random.bytes",1,1,&rng));
 assert(rt->acquire("random.bytes",1,0,&rng));const risc_random_api_v1* random=rng.api;
 unsigned char bytes[256]={0};assert(random->fill(random->context,bytes,sizeof(bytes))==RISC_RANDOM_OK);
 for(unsigned i=0;i<sizeof(bytes);++i)assert(bytes[i]==(unsigned char)(i^0xA5));
 assert(random->fill(random->context,bytes,257)==RISC_RANDOM_INVALID);
 assert(random->fill(random->context,0,1)==RISC_RANDOM_INVALID);
 memory_test_owner(0);assert(random->fill(random->context,bytes,1)==RISC_RANDOM_CONTEXT);memory_test_owner(1);
 risc_random_api_v1 oldRandom=*random;assert(rt->release(&rng));assert(oldRandom.fill(oldRandom.context,bytes,1)==RISC_RANDOM_CONTEXT);
 assert(rt->acquire("random.bytes",1,0,&rng));assert(oldRandom.fill(oldRandom.context,bytes,1)==RISC_RANDOM_CONTEXT);random_test_keep(rng.api);
}
