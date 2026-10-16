#define NATIVE_ATOMIC_ENDPOINT_FIXTURE_ONLY
#include "native_atomic_key_value_test.cpp"
#include "bootstrap/Runtime.h"
#include <fstream>
#include "RuntimeFileFixture.h"
using R=RiscBoot::Runtime;
static R* runtime;static A* endpoint;
static uint32_t availability=RISC_KEY_VALUE_UNAVAILABLE;static uint64_t generation=1;
static bool discovered=false,provider=false;static unsigned snapshots=0,discoveryPolls=0,shutdowns=0;
static std::thread::id ownerThread=std::this_thread::get_id();
static bool owner(){return ownerThread==std::this_thread::get_id();}
static int32_t snapshot(void*,uint32_t ns,risc_key_value_availability_state_v1*out){
 assert(owner()&&ns==6);++snapshots;if(!out||out->struct_size<sizeof(*out))return RISC_KEY_VALUE_AVAILABILITY_INVALID;
 *out={sizeof(*out),ns,availability,0,generation};return 0;
}
static bool checkpoint(bool){
 assert(!runtime->active());
 if(discovered&&availability!=RISC_KEY_VALUE_AVAILABLE){const uint32_t next=runtime->keyValueReconciliationSafe(6)?RISC_KEY_VALUE_AVAILABLE:RISC_KEY_VALUE_RECONCILIATION_PENDING;
  if(availability!=next){availability=next;++generation;}}
 return true;
}
static bool discovery(){++discoveryPolls;return checkpoint(false);}
static bool bind(R&r){
 assert(r.registerAtomicKeyValue("availability-test",6,*endpoint));
 assert(!r.registerKeyValueAvailability(6,{nullptr,snapshot}));
 assert(r.registerAtomicKeyValueReadThrough(6,*endpoint));
 assert(r.registerKeyValueAvailability(6,{nullptr,snapshot}));
 assert(!r.registerKeyValueAvailability(6,{nullptr,snapshot}));
 assert(!r.registerKeyValueAvailability(7,{nullptr,snapshot}));return true;
}
extern "C" void test_atomic_bound_started(unsigned,risc_bound_key_value_v1 table){
 char byte='x';uint32_t n=42;assert(table.get(table.context,"local",&byte,1,&n)==RISC_KEY_VALUE_NOT_FOUND&&!n&&byte=='x');
}
extern "C" void test_atomic_bound_stopped(risc_bound_key_value_v1){}
static risc_runtime_capability_v1 acquire(const risc_runtime_api_v1*rt,const char*cap,uint32_t version=1){
 risc_runtime_capability_v1 grant{sizeof(grant)};assert(rt->acquire(cap,version,6,&grant));return grant;
}
extern "C" void test_availability_app(){
 const auto*rt=risc_runtime_get_api(1);assert(rt);
 risc_runtime_capability_v1 refused{sizeof(refused)};assert(!rt->acquire(RISC_KEY_VALUE_AVAILABILITY_CAPABILITY,1,7,&refused));
 assert(!rt->acquire(RISC_KEY_VALUE_AVAILABILITY_CAPABILITY,2,6,&refused));
 auto statusGrant=acquire(rt,RISC_KEY_VALUE_AVAILABILITY_CAPABILITY);
 const auto status=*static_cast<const risc_key_value_availability_v1*>(statusGrant.api);
 risc_key_value_availability_state_v1 state{sizeof(state)};assert(!status.snapshot(status.context,&state)&&state.availability==RISC_KEY_VALUE_UNAVAILABLE);
 assert(status.snapshot(status.context,nullptr)==RISC_KEY_VALUE_AVAILABILITY_INVALID);
 auto dataGrant=acquire(rt,RISC_KEY_VALUE_CAPABILITY);const auto data=*static_cast<const risc_key_value_v1*>(dataGrant.api);
 auto atomicGrant=acquire(rt,RISC_KEY_VALUE_ATOMIC_CAPABILITY);const auto atomic=*static_cast<const risc_key_value_atomic_v1*>(atomicGrant.api);
 char value[16]="untouched";uint32_t n=42;uint64_t token=42;risc_key_value_atomic_state_v1 atomicState{sizeof(atomicState)};
 assert(data.get(data.context,"local",value,sizeof(value),&n)==RISC_KEY_VALUE_NOT_FOUND&&!n&&!strcmp(value,"untouched"));
 assert(data.put(data.context,"local","default",7)==RISC_KEY_VALUE_CONTEXT);
 assert(atomic.state(atomic.context,&atomicState)==RISC_ATOMIC_BUSY);
 assert(atomic.get(atomic.context,1,"local",value,sizeof(value),&n)==RISC_ATOMIC_BUSY&&!n);
 risc_key_value_atomic_mutation_v1 mutation{"local",RISC_ATOMIC_PUT,"default",7};
 assert(atomic.submit(atomic.context,1,&mutation,1,"save",4,&token)==RISC_ATOMIC_BUSY&&!token);
 discovered=true;const auto old=discoveryPolls;rt->yield_ms(1);assert(discoveryPolls==old+1&&!runtime->retained());
 assert(!status.snapshot(status.context,&state)&&state.availability==RISC_KEY_VALUE_RECONCILIATION_PENDING);
 const auto pendingGeneration=state.generation;
 for(unsigned i=0;i<100;++i)rt->yield_ms(1);
 assert(!status.snapshot(status.context,&state)&&state.generation==pendingGeneration);
 assert(rt->release(&atomicGrant));assert(!status.snapshot(status.context,&state)&&state.availability==RISC_KEY_VALUE_RECONCILIATION_PENDING);
 assert(rt->release(&dataGrant));assert(!status.snapshot(status.context,&state));
 if(provider){assert(state.availability==RISC_KEY_VALUE_RECONCILIATION_PENDING);}
 else {
  assert(state.availability==RISC_KEY_VALUE_AVAILABLE&&state.generation>pendingGeneration);
  dataGrant=acquire(rt,RISC_KEY_VALUE_CAPABILITY);const auto fresh=*static_cast<const risc_key_value_v1*>(dataGrant.api);
  assert(!fresh.get(fresh.context,"local",value,sizeof(value),&n)&&n==9&&!memcmp(value,"recovered",9));
  assert(data.get(data.context,"local",value,sizeof(value),&n)==RISC_KEY_VALUE_CONTEXT);
  assert(rt->release(&dataGrant));
 }
 std::thread other([&]{risc_key_value_availability_state_v1 out{sizeof(out)};assert(status.snapshot(status.context,&out)==RISC_KEY_VALUE_AVAILABILITY_CONTEXT);});other.join();
 assert(rt->release(&statusGrant));assert(status.snapshot(status.context,&state)==RISC_KEY_VALUE_AVAILABILITY_CONTEXT);
}
int main(int argc,char**argv){
 assert(argc==3);const std::string root=argv[1];provider=!strcmp(argv[2],"provider");
 auto file=[&](const char*name,const std::string&body){std::ofstream(root+"/"+name)<<body;};
 file("board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 file("app.json",R"({"type":"application","id":"availability-test","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"storage.key-value","api":1},{"capability":"storage.key-value.atomic","api":1},{"capability":"storage.key-value.availability","api":1}]})");
 file("reader.json",R"({"type":"driver","id":"atomic-reader-1","version":"1.0.0","architecture":"xtensa-esp32s3","driver_abi":2,"file_name":"reader.elf","requires":[{"capability":"storage.key-value.bound","api":1}],"provides":[{"capability":"test.atomic.reader1","api":1}]})");
 const std::string drivers=provider?R"({"manifest":"reader.json","key_value":[{"key":"local","namespace":6,"access":"read"}]})":"";
 file("boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","drivers":[)")+drivers+R"(],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"storage.key-value","api":1,"instance_id":6},{"capability":"storage.key-value.atomic","api":1,"instance_id":6},{"capability":"storage.key-value.availability","api":1,"instance_id":6}]}]})");
 Disk disk;Alloc allocation;C core(6,disk.backend(),limits(),allocation.api());C::Value recovered{"local","recovered",9};assert(!core.initialize(&recovered,1,{}));Gate gate;A atomic(core,gate.api());endpoint=&atomic;
 const auto before=disk.root;disk.calls=0;
 RiscBoot::KeyValueBackend legacy{nullptr,[](void*,uint32_t,const char*,void*,uint32_t,uint32_t*){assert(false);return RISC_KEY_VALUE_IO;},[](void*,uint32_t,const char*,const void*,uint32_t){assert(false);return RISC_KEY_VALUE_IO;}};
 RiscBoot::Port p{owner,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind,&legacy};
 p.nativeOwnerCheckpoint=checkpoint;p.nativeDiscoveryCheckpoint=discovery;p.nativeSettingsShutdown=[](){++shutdowns;return true;};
 RiscTest::FileSources rFiles0({root.c_str()});R r(p);assert(rFiles0.bind(r));runtime=&r;assert(r.prepare(root.c_str()));assert(!r.registerKeyValueAvailability(6,{nullptr,snapshot}));
 assert(r.run()&&!r.retained()&&shutdowns==1&&snapshots&&disk.root==before&&!disk.calls);
 assert(r.keyValueReconciliationSafe(6));
 puts("Availability: exact version/namespace grants, RAM defaults, no writes, pending authority, legacy/atomic/provider fences, generation and stale contexts PASS");
}
