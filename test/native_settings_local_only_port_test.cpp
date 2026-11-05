// Reuse only host NVS/RTOS/heap/port fixtures. This binary never creates an SD
// pin in ordinary cases, loads a volume driver, imports a card image or runs
// the external main. The isolated probe must intercept a forbidden acquire.
#include NATIVE_LOCAL_PORT_FIXTURE

extern "C" void attemptedExternalAcquire() asm("_ZN16RiscExperimental19NativeSdPortAttempt7acquireEv");
extern "C" void queuedNativeInitialization() asm("_ZN7RiscCpu23NativeSettingsBootstrap11queueNativeEv");
extern "C" void enteredNativeInitialization() asm("_ZN7RiscCpu23NativeSettingsBootstrap16initializeNativeEv");
static unsigned externalAttempts=0,queuedInitializations=0,enteredInitializations=0;
static bool probeControl=false;
extern "C" __attribute__((no_instrument_function)) void __cyg_profile_func_enter(void*fn,void*){
 if(fn==reinterpret_cast<void*>(&queuedNativeInitialization))++queuedInitializations;
 if(fn==reinterpret_cast<void*>(&enteredNativeInitialization))++enteredInitializations;
 if(fn==reinterpret_cast<void*>(&attemptedExternalAcquire)){++externalAttempts;if(probeControl)std::_Exit(86);std::abort();}
}
extern "C" __attribute__((no_instrument_function)) void __cyg_profile_func_exit(void*,void*){}
static bool requestCancel=false;
static void noExternal(){assert(!externalAttempts&&!loads&&!unloads&&!hardwareCalls&&!f&&!loadMedia&&!saveMedia&&!entropyFills&&!entropyWindow&&mailbox.idle());}
static Bytes flashBytes(){Bytes b(native_lfs_fixture_flash_size());assert(native_lfs_fixture_snapshot(b.data(),b.size())==b.size());return b;}
static void localProof(){
 assert(core(6)->ready()&&core(6)->durabilityPolicy()==C::DurabilityPolicy::LocalOnlyV1);
 assert(!core(6)->identity().known&&!core(6)->pendingCount()&&!core(6)->exportPrepared()&&!core(1)->ready());
 noExternal();
}
int main(int argc,char**argv){
 assert(argc==3);mode=argv[2];
 if(mode=="probe-external-acquire"){
  Port cpu(hardware());port=&cpu;RiscBoot::Port boot{owner,[](risc_runtime_health_v1*){return true;},schedule,[](const char*){return true;},bind};
  RiscBoot::Runtime runtime(boot);P deliberatelyForbidden(runtime,cpu,6,0);
  probeControl=true;(void)deliberatelyForbidden.acquire();return 1; // must be intercepted before acquire body
 }
 assert(native_lfs_fixture_reset()==0);
 nvs["rte00000001"]["quick_radio"]={1,0,0,0};
 const auto peer=nvs.at("rte00000001");
 const bool imported=mode=="legacy"||mode=="cancel-inspect"||mode=="cancel-init"||mode=="cancel-committed";
 if(imported)nvs["rte00000006"]["opaque:nonwifi"]={'k','e','e','p'};
 const auto original=nvs;
 native_lfs_fixture_shutdown();native_lfs_fixture_select_appdata();
 assert(M::prepare(static_cast<const esp_partition_t*>(native_lfs_fixture_partition())));
 assert(!native_lfs_fixture_reboot()&&M::finish());
 {M::Guard g(M::Access::AppData);assert(g.held);assert(!native_lfs_fixture_mkdir("/n00000001"));assert(!native_lfs_fixture_seed("/n00000001/peer","preserve",8));}
 assert(B::prepare(owner));admission=B::admission();assert(admission&&F::maintenance());
 assert(core(1)->durabilityPolicy()==C::DurabilityPolicy::LocalOnlyV1&&core(6)->durabilityPolicy()==C::DurabilityPolicy::LocalOnlyV1);
 assert(L::prepare(admission,{owner,schedule,[](){return uint32_t(nowMs);},[](){return requestCancel;}}));
 assert(W::StackBytes==8192&&Shim::tasks.size()==2);
 Port cpu(hardware());port=&cpu;
 RiscBoot::Port boot{owner,[](risc_runtime_health_v1*){return true;},schedule,[](const char*){return true;},bind};
 static const RiscBoot::KeyValueBackend legacy{nullptr,
  [](void*,uint32_t ns,const char*k,void*out,uint32_t cap,uint32_t*n)->int32_t{
   if(n)*n=0;if(ns!=1||strcmp(k,"quick_radio"))return RISC_KEY_VALUE_NOT_FOUND;
   if(cap<4)return RISC_KEY_VALUE_IO;memcpy(out,nvs.at("rte00000001").at("quick_radio").data(),4);if(n)*n=4;return RISC_KEY_VALUE_OK;},
  [](void*,uint32_t,const char*,const void*,uint32_t)->int32_t{return RISC_KEY_VALUE_IO;},2048};
 boot.keyValue=&legacy;
 RiscTest::FileSources sources({argv[1]});RiscBoot::Runtime runtime(boot);assert(sources.bind(runtime)&&L::bind(runtime));
 if(!runtime.prepare(argv[1])){fprintf(stderr,"prepare: %s\n",runtime.error());assert(false);}
 const auto before=flashBytes();
 assert(L::hydrate(runtime,cpu)&&L::ready()&&L::attachOwner(runtime,cpu));
 assert(!core(6)->ready()&&availability(6)==RISC_KEY_VALUE_UNAVAILABLE);
 const auto&writer=W::coordinator(6)->endpoint();risc_key_value_atomic_state_v1 state{sizeof(state)};
 assert(writer.state(writer.context,&state)!=RISC_ATOMIC_OK);noExternal();
 assert(L::discoveryCheckpoint()&&L::discovering()&&!L::exitSafe());
 if(mode=="cancel-inspect")requestCancel=true;
 bool cancelledAtInit=false,cancelledCommitted=false;
 for(unsigned i=0;i<300000&&L::discovering();++i){
  assert(L::discoveryCheckpoint());
  if(mode=="cancel-init"&&queuedInitializations&&!enteredInitializations&&!requestCancel){
   assert(!core(6)->ready()&&availability(6)!=RISC_KEY_VALUE_AVAILABLE);
   requestCancel=true;cancelledAtInit=true;
   assert(L::discoveryCheckpoint()); // forward cancellation before the worker ticket
  }
  schedule(1);
  if(mode=="cancel-committed"&&enteredInitializations&&core(6)->ready()&&!requestCancel){
   assert(L::discovering()&&!L::exitSafe()&&availability(6)!=RISC_KEY_VALUE_AVAILABLE);
   requestCancel=true;cancelledCommitted=true;
  }
 }
 assert(!L::discovering()&&L::exitSafe()&&!L::retained());
 if(mode=="cancel-inspect"||mode=="cancel-init"){
  assert(mode!="cancel-init"||cancelledAtInit);
  assert(!enteredInitializations&&!core(6)->ready()&&nvs==original&&flashBytes()==before);
 }else if(mode=="fresh"&&!RISC_NATIVE_SETTINGS_LOCAL_FRESH){
  assert(!core(6)->ready()&&availability(6)!=RISC_KEY_VALUE_AVAILABLE&&nvs==original&&flashBytes()==before);
 }else{
  localProof();assert(availability(6)==RISC_KEY_VALUE_AVAILABLE);
  if(mode=="cancel-committed")assert(cancelledCommitted&&enteredInitializations==1&&queuedInitializations==1);
  if(imported){assert(core(6)->baseline().archiveObligation());value(*core(6),"opaque:nonwifi","keep");assert(nvs.at("rte00000006")==original.at("rte00000006"));}
  assert(!writer.state(writer.context,&state));
  risc_key_value_atomic_mutation_v1 mutation{"opaque:new",RISC_ATOMIC_PUT,"value",5};uint64_t token=0;
  assert(!writer.submit(writer.context,state.revision,&mutation,1,"native-local-edit",17,&token)&&token);
  schedule(1); // accepted receipt is deliberately not yet collected
  risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt)};int rc;
  do{schedule(1);rc=writer.collect(writer.context,token,&receipt);}while(rc==RISC_ATOMIC_PENDING);
  assert(!rc&&!receipt.status);value(*core(6),"opaque:new","value");localProof();
  assert(L::checkpoint(true)&&L::exitSafe());noExternal();
 }
 assert(nvs.at("rte00000001")==peer);
 {M::Guard g(M::Access::AppData);assert(g.held);char b[8];int fd=native_lfs_fixture_open("/n00000001/peer",O_RDONLY);assert(fd>=0&&native_lfs_fixture_read(fd,b,8)==8&&!memcmp(b,"preserve",8)&&!native_lfs_fixture_close(fd));}
 assert(L::shutdown()&&W::exitSafe()&&!M::retained()&&handles.empty()&&iterators.empty());
 Shim::stop();noExternal();
 printf("PASS local-only port %s fresh_permission=%d workers=2 stack_each=%u internal_peak=%zu payload_peak=%zu; host-only, not target capacity proof\n",mode.c_str(),RISC_NATIVE_SETTINGS_LOCAL_FRESH,unsigned(W::StackBytes),Shim::peakInternal,Shim::peakPayload);
 return 0;
}
