#include <RiscRuntimeV1.h>
#include <RiscRealtimeV1.h>
#include <RiscDeepSleepV1.h>
#include <assert.h>
#include <string.h>
#include <stddef.h>
_Static_assert(sizeof(risc_realtime_snapshot_v1)==40,"snapshot ABI size");
_Static_assert(offsetof(risc_realtime_snapshot_v1,monotonic_before_us)==24,"snapshot ABI offset");
extern int test_time_mode(void);
extern void test_time_owner(int);
extern void test_time_safe(int);
extern void test_time_backend(int);
extern void test_time_save(const risc_realtime_api_v1*,const risc_realtime_control_api_v1*);
extern void test_time_old(void);
__attribute__((visibility("default"))) void app_main(void){
 const risc_runtime_api_v1* rt=risc_runtime_get_api(1);assert(rt);test_time_old();
 const int mode=test_time_mode();
 risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)},other={.struct_size=sizeof(other)};
 risc_realtime_snapshot_v1 s={.struct_size=sizeof(s)};
 risc_realtime_api_v1 read={0};risc_realtime_control_api_v1 control={0};
 if(mode==0){
  assert(!rt->acquire(RISC_REALTIME_CONTROL_CAPABILITY,1,0,&grant));
  assert(rt->acquire(RISC_REALTIME_CAPABILITY,1,0,&grant));
  read=*(const risc_realtime_api_v1*)grant.api;
  assert(read.struct_size==sizeof(read));
 }else{
  if(mode!=7)assert(!rt->acquire(RISC_REALTIME_CAPABILITY,1,0,&grant)); // Control is self-contained.
  assert(!rt->acquire(RISC_REALTIME_CONTROL_CAPABILITY,2,0,&grant));
  assert(!rt->acquire(RISC_REALTIME_CONTROL_CAPABILITY,1,7,&grant));
  assert(rt->acquire(RISC_REALTIME_CONTROL_CAPABILITY,1,0,&grant));
  control=*(const risc_realtime_control_api_v1*)grant.api;
  read=(risc_realtime_api_v1){1,sizeof(read),control.context,control.read};
  assert(!rt->acquire(RISC_REALTIME_CONTROL_CAPABILITY,1,0,&other));
 }
 if(mode==7){
  assert(rt->acquire(RISC_REALTIME_CAPABILITY,1,0,&other));
  const risc_realtime_api_v1 *reader=other.api;
  assert(control.seed(reader->context,1,0)==RISC_REALTIME_CONTEXT);
  assert(reader->read(reader->context,&s)==0 && !s.validity);
  assert(rt->release(&other));
 }
 risc_realtime_snapshot_v1 before=s;
 assert(read.read(0,&s)==RISC_REALTIME_CONTEXT);
 assert(read.read((void*)UINTPTR_MAX,&s)==RISC_REALTIME_CONTEXT);
 assert(read.read(read.context,0)==RISC_REALTIME_INVALID);
 --s.struct_size;before=s;assert(read.read(read.context,&s)==RISC_REALTIME_INVALID && !memcmp(&s,&before,sizeof(s)));++s.struct_size;
 test_time_owner(0);assert(read.read(read.context,&s)==RISC_REALTIME_CONTEXT);test_time_owner(1);
 test_time_safe(0);assert(read.read(read.context,&s)==RISC_REALTIME_CONTEXT);test_time_safe(1);
 assert(read.read(read.context,&s)==0 && s.validity==RISC_REALTIME_UNSET && !s.epoch_seconds);
 if(mode){
  assert(control.seed(0,1,0)==RISC_REALTIME_CONTEXT);
  assert(control.seed(control.context,-1,0)==RISC_REALTIME_INVALID);
  assert(control.seed(control.context,INT64_MAX,0)==RISC_REALTIME_INVALID);
  assert(control.seed(control.context,1,1000000000)==RISC_REALTIME_INVALID);
  assert(control.seed(control.context,1,1)==RISC_REALTIME_INVALID);
  test_time_owner(0);assert(control.seed(control.context,1,0)==RISC_REALTIME_CONTEXT);test_time_owner(1);
  test_time_safe(0);assert(control.seed(control.context,1,0)==RISC_REALTIME_CONTEXT);test_time_safe(1);
  assert(control.seed(control.context,1800000000,123456000)==0);
  assert(read.read(read.context,&s)==0 && s.validity==1 && s.epoch_seconds==1800000000 && s.nanoseconds==123456000);
 }
 before=s;
 for(int failure=1;failure<=7;++failure){
  test_time_backend(failure);assert(read.read(read.context,&s)==RISC_REALTIME_IO && !memcmp(&s,&before,sizeof(s)));
 }
 test_time_backend(0);test_time_save(&read,&control);
 assert(rt->release(&grant));assert(read.read(read.context,&s)==RISC_REALTIME_CONTEXT);
 if(mode)assert(control.seed(control.context,1,0)==RISC_REALTIME_CONTEXT);
 assert(rt->acquire(mode?RISC_REALTIME_CONTROL_CAPABILITY:RISC_REALTIME_CAPABILITY,1,0,&grant));
 assert(read.read(read.context,&s)==RISC_REALTIME_CONTEXT);
 if(mode){control=*(const risc_realtime_control_api_v1*)grant.api;read.context=control.context;read.read=control.read;}
 else read=*(const risc_realtime_api_v1*)grant.api;
 if(mode==2 || mode==3){
  test_time_save(&read,&control);
  assert(rt->acquire("test.deep",1,7,&other));
  const struct {uint32_t version,size;int32_t(*enter)(void);} *deep=other.api;
  const int result=deep->enter();
  assert(result==(mode==2?RISC_DEEP_SLEEP_ACTIVE_WAKE:RISC_DEEP_SLEEP_RETAINED));
  if(mode==3){assert(read.read(read.context,&s)==RISC_REALTIME_CONTEXT);assert(control.seed(control.context,1,0)==RISC_REALTIME_CONTEXT);return;}
  assert(read.read(read.context,&s)==0 && s.validity==1);assert(rt->release(&other));
 }
 test_time_save(&read,&control); // Must become stale at app return, too.
}
