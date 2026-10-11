#include <RiscRuntimeV1.h>
#include <RiscKeyValueV1.h>
#include <RiscPlatformClockV1.h>
#include <assert.h>
#include <string.h>
extern unsigned multi_capacity(void);
extern unsigned multi_index_mode(void);
extern void multi_owner(int);
extern int multi_phase(void);
extern void multi_next(void);
extern int multi_retaining(void);
extern void multi_retain(void);
extern void multi_keep(risc_key_value_v1);
__attribute__((visibility("default"))) void app_main(void){
 const risc_runtime_api_v1* rt=risc_runtime_get_api(1);assert(rt);
 risc_runtime_capability_v1 one={.struct_size=sizeof(one)},five={.struct_size=sizeof(five)},bad={.struct_size=sizeof(bad)};
#ifdef CHILD
 assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,0,&one));
 assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,5,&five));
 const risc_key_value_v1* kv=one.api;char bytes[8]={0};uint32_t size=0;
 assert(kv->get(kv->context,"same",bytes,sizeof(bytes),&size)==0&&size==3&&!memcmp(bytes,"one",3));
 assert(rt->release(&one));multi_next();return;
#else
 unsigned index_mode=multi_index_mode();
 if(index_mode){
  if(index_mode==1){assert(rt->acquire("platform.clock",1,0,&one));const risc_platform_clock_api_v1* clock=one.api;assert(clock->monotonic_ms(clock->context)==12345);}
  else {assert(index_mode==2 && rt->acquire("test.slot15",1,0,&one));const unsigned* value=one.api;assert(value[0]==1 && value[2]==15);}
  assert(rt->release(&one));return;
 }
 unsigned capacity=multi_capacity();
 if(capacity){
  risc_runtime_capability_v1 grants[16]={0};assert(capacity<=17);
  unsigned live=capacity<16?capacity:16;
  for(unsigned i=0;i<live;++i){
   grants[i].struct_size=sizeof(grants[i]);assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,i+1,&grants[i]));
   const risc_key_value_v1* kv=grants[i].api;unsigned value=i+1,got=0;uint32_t n=0;
   assert(kv->put(kv->context,"slot",&value,sizeof(value))==0);
   assert(kv->get(kv->context,"slot",&got,sizeof(got),&n)==0&&n==sizeof(got)&&got==value);
  }
  if(live==16)assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,1,&bad));
  if(capacity==17){
   // Row 17 is authorized metadata, but no seventeenth live slot exists.
   assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,17,&bad));
   risc_runtime_capability_v1 old=grants[7];
   risc_key_value_v1 stale=*(const risc_key_value_v1*)old.api;
   assert(rt->release(&grants[7]));
   assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,17,&grants[7]));
   assert(grants[7].slot==old.slot && grants[7].generation!=old.generation);
   assert(!rt->release(&old));
   unsigned value=17,got=0;uint32_t n=99;
   assert(stale.get(stale.context,"slot",&got,sizeof(got),&n)==RISC_KEY_VALUE_CONTEXT && !n);
   assert(stale.put(stale.context,"slot",&value,sizeof(value))==RISC_KEY_VALUE_CONTEXT);
   const risc_key_value_v1* kv=grants[7].api;
   assert(kv->put(kv->context,"slot",&value,sizeof(value))==0);
   assert(kv->get(kv->context,"slot",&got,sizeof(got),&n)==0 && n==sizeof(got) && got==17);
   assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,8,&bad));
   multi_keep(*kv);
  }
  assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,0,&bad));
  assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,capacity+1,&bad));
  for(unsigned i=0;i<live;++i)assert(rt->release(&grants[i]));
  return;
 }
 bad.slot=77;bad.generation=88;bad.api=(void*)1;
 assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,0,&bad)&&!bad.slot&&!bad.generation&&!bad.api);
 assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,2,&bad));
 assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,2,1,&bad));
 assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,1,&one));
 assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,5,&five));
 const risc_key_value_v1* a=one.api;const risc_key_value_v1* b=five.api;
 assert(a->context!=b->context);
 char bytes[8]={0};uint32_t size=0;
 if(multi_phase()==0){
  assert(a->put(a->context,"same","one",3)==0);
  assert(b->put(b->context,"same","five",4)==0);
 }
 assert(a->get(a->context,"same",bytes,sizeof(bytes),&size)==0&&size==3&&!memcmp(bytes,"one",3));
 assert(b->get(b->context,"same",bytes,sizeof(bytes),&size)==0&&size==4&&!memcmp(bytes,"five",4));
 risc_key_value_v1 stale=*a;assert(rt->release(&one));
 assert(stale.put(stale.context,"same","bad",3)==RISC_KEY_VALUE_CONTEXT);
 assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,1,&one));a=one.api;assert(a->context!=stale.context);
 multi_owner(0);assert(a->get(a->context,"same",bytes,sizeof(bytes),&size)==RISC_KEY_VALUE_CONTEXT&&!size);
 assert(b->put(b->context,"same","bad",3)==RISC_KEY_VALUE_CONTEXT);assert(!rt->release(&one));multi_owner(1);
 risc_runtime_capability_v1 pool[15]={0};
 for(unsigned i=0;i<14;++i){pool[i].struct_size=sizeof(pool[i]);assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,(i&1)?1:5,&pool[i]));}
 pool[14].struct_size=sizeof(pool[14]);assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,5,&pool[14]));
 for(unsigned i=0;i<14;++i)assert(rt->release(&pool[i]));
 multi_keep(*b);
 if(multi_retaining()){multi_retain();return;}
 assert(rt->release(&one));assert(rt->release(&five));
 if(multi_phase()==0){multi_next();assert(rt->request_launch("child.elf"));}
#endif
}
