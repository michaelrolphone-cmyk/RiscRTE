#include <RiscRuntimeV1.h>
#include <RiscKeyValueV1.h>
#include <assert.h>
#include <string.h>
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
