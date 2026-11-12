// Real checked backend and pinned LittleFS. This is not target heap admission.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define heap_caps_malloc original_fixture_heap_caps_malloc
#define main original_backend_main
#include "native_settings_backend_test.cpp"
#undef main
#undef heap_caps_malloc
#pragma GCC diagnostic pop
#include <cstdlib>
static bool refusePayload=false;
static unsigned payloadCalls=0;
void*heap_caps_malloc(size_t n,unsigned flags){
 ++payloadCalls;
 assert(flags==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(refusePayload)return nullptr;
 return original_fixture_heap_caps_malloc(n,flags);
}
static Bytes flashImage(){
 Bytes out(native_lfs_fixture_flash_size());
 assert(native_lfs_fixture_snapshot(out.data(),out.size())==out.size());return out;
}
static void peer(){
 char out[8]{};const int fd=native_lfs_fixture_open("/n00000001/timecard.json",O_RDONLY);
 assert(fd>=0&&native_lfs_fixture_read(fd,out,sizeof out)==8&&!memcmp(out,"preserve",8)&&native_lfs_fixture_close(fd)==0);
}
static void mutation(unsigned index,bool reject,bool memory=false){
 const auto&b=(index?inputs.wifi:inputs.preferences).backend;const uint32_t ns=index?6:1;
 active(ns);M::Guard guard(M::Access::Settings);assert(guard.held);
 F::Store store(ns,*b.files,b.metadata,b.guard,RiscCpu::NativeSettingsPayloadAllocator::allocator(),b.artifacts);
 C core(ns,store.backend(),F::Store::limits(),RiscCpu::NativeSettingsPayloadAllocator::allocator());
 assert(core.load()==C::Ok);store.finishOperation();
 const auto revision=core.revision();const auto beforeNvs=nvs;
 const Bytes beforeFlash=flashImage();const unsigned calls=payloadCalls;
 const C::Mutation change{"budget-check",C::Put,"new-value",9};
 refusePayload=memory;
 const int rc=core.apply(core.revision(),&change,1,"budget-fixture",14);
 refusePayload=false;
 if(reject){
  assert(rc==(memory?C::NoMemory:C::NoSpace));
  assert(core.revision()==revision&&nvs==beforeNvs&&flashImage()==beforeFlash);
  if(memory)assert(payloadCalls==calls+1); // First payload refusal, no fallback.
 }else assert(rc==C::Ok);
 store.finishOperation();
 assert(core.load()==C::Ok);store.finishOperation();
 verifyValue(core,"original","kept");
 if(!reject)verifyValue(core,"budget-check","new-value");
 assert(store.exitSafe()&&!M::retained()&&handles.empty()&&iterators.empty());
 printf("backend budget ns=%u expected=%s status=%d mutation preserved=%u\n",ns,reject?(memory?"NoMemory":"NoSpace"):"Ok",rc,unsigned(reject));
}
int main(int argc,char**argv){
 assert(argc==2);const std::string mode=argv[1];
 assert(mode=="full-space"||mode=="payload-refusal");
 assert(native_lfs_fixture_reset()==0);ready();
 {M::Guard g(M::Access::AppData);assert(g.held);
  assert(native_lfs_fixture_mkdir("/n00000001")==0);
  assert(native_lfs_fixture_seed("/n00000001/timecard.json","preserve",8)==0);}
 exercise(0,"legacy");exercise(1,"legacy");
 const auto legacyOne=nvs.at("rte00000001"),legacySix=nvs.at("rte00000006");
 std::vector<std::string> filler;
 if(mode=="full-space"){
  liveWorker=false;onOwner=true;M::Guard g(M::Access::AppData);assert(g.held);
  assert(native_lfs_fixture_mkdir("/budget-fixture")==0);
  Bytes chunk(4096,0x6d);size_t total=0,used=0;
  for(unsigned i=0;;++i){
   assert(M::usage(&total,&used)&&total==F::Store::PartitionBytes&&used<=total);
   if(total-used<F::Store::RecoveryReserve)break;
   assert(i<128);
   const auto path="/budget-fixture/filler-"+std::to_string(i);
   assert(native_lfs_fixture_seed(path.c_str(),chunk.data(),chunk.size())==0);filler.push_back(path);
  }
  peer();printf("real LittleFS used=%zu total=%zu free=%zu filler_files=%zu\n",used,total,total-used,filler.size());
 }
 mutation(0,true,mode=="payload-refusal");mutation(1,true,mode=="payload-refusal");
 assert(nvs.at("rte00000001")==legacyOne&&nvs.at("rte00000006")==legacySix);
 {liveWorker=false;onOwner=true;M::Guard g(M::Access::AppData);assert(g.held);peer();
  // Only names created by this fixture are removed. Existing peer bytes stay.
  for(const auto&path:filler)assert(native_lfs_fixture_unlink(path.c_str())==0);}
 mutation(0,false);mutation(1,false);
 {liveWorker=false;onOwner=true;M::Guard g(M::Access::AppData);assert(g.held);peer();}
 assert(nvs.at("rte00000001")==legacyOne&&nvs.at("rte00000006")==legacySix);
 assert(blocks.empty()&&!M::retained());
 puts("Real backend refusal/reload/retry and peer preservation PASS; no durable reservation, physical reboot or product heap admission claimed");
 return 0;
}
