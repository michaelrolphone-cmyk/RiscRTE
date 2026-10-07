#include <RiscRuntimeV1.h>
#include <RiscRetainedWakeV1.h>
#include <RiscDeepSleepV1.h>
#include <assert.h>
#include <string.h>
extern int test_wake_mode(void);
extern void test_wake_refused(void);
extern unsigned test_wake_cause(void);
extern void test_wake_owner(int);
extern void test_wake_safe(int);
extern void test_wake_snapshot(const risc_retained_wake_api_v1*);
__attribute__((visibility("default"))) void app_main(void){
 const risc_runtime_api_v1* rt=risc_runtime_get_api(1);assert(rt);
 risc_runtime_capability_v1 g={.struct_size=sizeof(g)}, other={.struct_size=sizeof(other)};
 assert(!rt->acquire(RISC_RETAINED_WAKE_CAPABILITY,1,1,&g));
 assert(rt->acquire(RISC_RETAINED_WAKE_CAPABILITY,1,0,&g));
 assert(!rt->acquire(RISC_RETAINED_WAKE_CAPABILITY,1,0,&other));
 risc_retained_wake_api_v1 a=*(const risc_retained_wake_api_v1*)g.api;
 risc_retained_wake_record_v1 r={.struct_size=sizeof(r),.type=7,.schema_version=1,.size=80};
 for(unsigned i=0;i<80;++i)r.payload[i]=(uint8_t)i;
 risc_retained_wake_record_v1 out={.struct_size=sizeof(out)}, before=out;unsigned cause=99;
 int mode=test_wake_mode();
 test_wake_owner(0);assert(a.stage(a.context,&r)==RISC_RETAINED_WAKE_CONTEXT);test_wake_owner(1);
 test_wake_safe(0);assert(a.read(a.context,7,1,&out,&cause)==RISC_RETAINED_WAKE_CONTEXT);test_wake_safe(1);
 if(mode==1){
  assert(a.read(a.context,8,1,&out,&cause)==RISC_RETAINED_WAKE_MISMATCH && !memcmp(&out,&before,sizeof(out)));
  assert(a.read(a.context,7,2,&out,&cause)==RISC_RETAINED_WAKE_MISMATCH);
  assert(a.read(a.context,7,1,&out,&cause)==0 && !memcmp(&out,&r,sizeof(out)));
 }
 assert(a.read(a.context,7,1,&out,&cause)==RISC_RETAINED_WAKE_ABSENT);
 assert(cause==test_wake_cause());
 r.size=129;assert(a.stage(a.context,&r)==RISC_RETAINED_WAKE_INVALID);r.size=80;
 r.type=0;assert(a.stage(a.context,&r)==RISC_RETAINED_WAKE_INVALID);r.type=7;
 assert(a.stage(a.context,&r)==0);
 test_wake_snapshot(&a);
 assert(rt->release(&g));assert(a.stage(a.context,&r)==RISC_RETAINED_WAKE_CONTEXT);
 assert(rt->acquire(RISC_RETAINED_WAKE_CAPABILITY,1,0,&g));
 assert(a.clear(a.context)==RISC_RETAINED_WAKE_CONTEXT);
 a=*(const risc_retained_wake_api_v1*)g.api;
 assert(a.stage(a.context,&r)==0);r.payload[0]=255; /* Synchronous value copy. */
 if(mode==2 || mode==3 || mode==4){
  assert(rt->acquire("test.deep",1,7,&other));
  const struct {uint32_t version,size;int32_t(*enter)(void);} *deep=other.api;
  int rc=deep->enter();
  assert(mode!=2);
  assert(rc==(mode==3?RISC_DEEP_SLEEP_ACTIVE_WAKE:RISC_DEEP_SLEEP_RETAINED));
  if(mode==4)return;
  test_wake_refused();
  assert(rt->release(&other));
 }
 if(mode==0)return; /* Automatic teardown must cancel pending data. */
 assert(a.clear(a.context)==0);assert(rt->release(&g));
}
