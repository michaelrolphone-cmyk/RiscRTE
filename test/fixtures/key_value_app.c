#include <RiscRuntimeV1.h>
#include <RiscKeyValueV1.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
_Static_assert(offsetof(risc_key_value_v1,api_version)==0,"version prefix");
_Static_assert(offsetof(risc_key_value_v1,struct_size)==4,"size prefix");
_Static_assert(offsetof(risc_key_value_v1,context)==8,"context prefix");
extern void test_kv_owner(int);
extern int test_kv_phase(void);
extern void test_kv_next(void);
extern void test_kv_keep(risc_key_value_v1);
extern void test_kv_revoked(void);
static unsigned runs;
__attribute__((visibility("default"))) void app_main(void){
 assert(++runs==1);
 const risc_runtime_api_v1* rt=risc_runtime_get_api(1);assert(rt);
 risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)};
#ifdef NO_POLICY
 assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,0,&grant));return;
#endif
 assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,2,0,&grant));
 assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,3,&grant));
 assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,UINT64_MAX,&grant));
 assert(!rt->acquire("undeclared",1,0,&grant));
#ifdef ISOLATED
 assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,2,&grant));
#else
 assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,1,&grant));
#endif
 const risc_key_value_v1* kv=grant.api;assert(kv && kv->api_version==1 && kv->struct_size==sizeof(*kv));
 uint8_t value[65];memset(value,0xa5,sizeof(value));uint32_t size=999;
 assert(kv->get(NULL,"mode",value,64,&size)==RISC_KEY_VALUE_CONTEXT && size==0);
 assert(kv->put(NULL,"mode",value,1)==RISC_KEY_VALUE_CONTEXT);
#ifdef ISOLATED
 assert(kv->get(kv->context,"mode",value,64,&size)==RISC_KEY_VALUE_NOT_FOUND && size==0);
 assert(kv->put(kv->context,"mode","other",5)==RISC_KEY_VALUE_OK);
 test_kv_next();
#elif defined(CHILD)
 test_kv_revoked();
 assert(kv->get(kv->context,"mode",value,64,&size)==0 && size==4 && !memcmp(value,"data",4));
 assert(rt->request_launch("isolated.elf"));
#else
 if(test_kv_phase()==0){
  assert(kv->get(kv->context,"mode",value,64,&size)==RISC_KEY_VALUE_NOT_FOUND && size==0);
  assert(kv->put(kv->context,"mode","data",4)==0);
  assert(kv->get(kv->context,"mode",NULL,0,&size)==RISC_KEY_VALUE_BUFFER_SMALL && size==4);
  size=999;assert(kv->get(kv->context,"mode",value,3,&size)==RISC_KEY_VALUE_BUFFER_SMALL && size==4 && value[0]==0xa5);
  size=999;assert(kv->get(kv->context,"mode",NULL,1,&size)==RISC_KEY_VALUE_INVALID && size==0);
  assert(kv->get(kv->context,"mode",value,64,NULL)==RISC_KEY_VALUE_INVALID);
  const char* bad[]={"","UPPER","bad/key","abcdefghijklmnop","bad key","caf\xc3\xa9",NULL};
  for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){
   size=999;assert(kv->get(kv->context,bad[i],value,64,&size)==RISC_KEY_VALUE_INVALID && size==0);
   assert(kv->put(kv->context,bad[i],value,1)==RISC_KEY_VALUE_INVALID);
  }
  assert(kv->put(kv->context,"a-._09",value,64)==0);
  assert(kv->put(kv->context,"abcdefghijklmno",value,64)==0);
  assert(kv->put(kv->context,"mode",value,65)==RISC_KEY_VALUE_INVALID);
  assert(kv->put(kv->context,"mode",value,0)==RISC_KEY_VALUE_INVALID);
  assert(kv->put(kv->context,"mode",NULL,1)==RISC_KEY_VALUE_INVALID);
  const char* badReads[]={"partial","oversize","empty","badstatus"};
  for(unsigned i=0;i<4;i++){size=999;assert(kv->get(kv->context,badReads[i],value,64,&size)==RISC_KEY_VALUE_IO && size==0 && value[0]==0xa5);}
  assert(kv->put(kv->context,"fail",value,1)==RISC_KEY_VALUE_IO);
  risc_key_value_v1 copied=*kv;
  test_kv_owner(0);size=999;
  assert(copied.get(copied.context,"mode",value,64,&size)==RISC_KEY_VALUE_CONTEXT && size==0);
  assert(copied.put(copied.context,"mode",value,1)==RISC_KEY_VALUE_CONTEXT);
  assert(!rt->release(&grant));test_kv_owner(1);
  assert(rt->release(&grant));size=999;
  assert(copied.get(copied.context,"mode",value,64,&size)==RISC_KEY_VALUE_CONTEXT && size==0);
  assert(copied.put(copied.context,"mode",value,1)==RISC_KEY_VALUE_CONTEXT);
  assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,0,&grant));kv=grant.api;
  assert(kv->context!=copied.context);
  assert(copied.put(copied.context,"mode",value,1)==RISC_KEY_VALUE_CONTEXT);
  risc_runtime_capability_v1 many[16];memset(many,0,sizeof(many));
  for(unsigned i=0;i<15;i++){many[i].struct_size=sizeof(many[i]);assert(rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,1,&many[i]));}
  many[15].struct_size=sizeof(many[15]);assert(!rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,1,&many[15]));
  for(unsigned i=0;i<15;i++)assert(rt->release(&many[i]));
  test_kv_keep(*kv);assert(rt->request_launch("child.elf"));
  test_kv_next();return; // deliberately leave live grant: runtime must revoke
 }
 assert(kv->get(kv->context,"mode",value,64,&size)==0 && size==4 && !memcmp(value,"data",4));
 assert(test_kv_phase()>=2);test_kv_revoked();
#endif
 assert(rt->release(&grant));
}
