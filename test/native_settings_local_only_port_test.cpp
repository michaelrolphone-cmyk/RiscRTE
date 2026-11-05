// Reuse only host NVS/RTOS/heap/port fixtures. This binary never creates an SD
// pin in ordinary cases, loads a volume driver, imports a card image or runs
// the external main. The isolated probe must intercept a forbidden acquire.
extern "C" void local_port_after_nvs_commit();
#include NATIVE_LOCAL_PORT_FIXTURE

extern "C" void attemptedExternalAcquire() asm("_ZN16RiscExperimental19NativeSdPortAttempt7acquireEv");
extern "C" void queuedNativeInitialization() asm("_ZN7RiscCpu23NativeSettingsBootstrap11queueNativeEv");
extern "C" void enteredNativeInitialization() asm("_ZN7RiscCpu23NativeSettingsBootstrap16initializeNativeEv");
static std::atomic<unsigned> externalAttempts{0},queuedInitializations{0},enteredInitializations{0},completedInitializations{0};
static bool probeControl=false;
extern "C" __attribute__((no_instrument_function)) void __cyg_profile_func_enter(void*fn,void*){
 if(fn==reinterpret_cast<void*>(&queuedNativeInitialization))++queuedInitializations;
 if(fn==reinterpret_cast<void*>(&enteredNativeInitialization))++enteredInitializations;
 if(fn==reinterpret_cast<void*>(&attemptedExternalAcquire)){++externalAttempts;if(probeControl)std::_Exit(86);std::abort();}
}
extern "C" __attribute__((no_instrument_function)) void __cyg_profile_func_exit(void*fn,void*){if(fn==reinterpret_cast<void*>(&enteredNativeInitialization))++completedInitializations;}
static bool requestCancel=false;
static void noExternal(){assert(!externalAttempts&&!loads&&!unloads&&!hardwareCalls&&!f&&!loadMedia&&!saveMedia&&!entropyFills&&!entropyWindow&&mailbox.idle());}
static Bytes flashBytes(){Bytes b(native_lfs_fixture_flash_size());assert(native_lfs_fixture_snapshot(b.data(),b.size())==b.size());return b;}

static void saveLocalState(const std::string&path){
 auto flash=flashBytes();std::ofstream out(path+"/flash.bin",std::ios::binary);out.write(reinterpret_cast<const char*>(flash.data()),flash.size());out.close();assert(out);
 std::ofstream rows(path+"/nvs.bin",std::ios::binary);
 auto word=[&](uint32_t n){rows.write(reinterpret_cast<const char*>(&n),4);};
 auto bytes=[&](const auto&v){word(v.size());rows.write(reinterpret_cast<const char*>(v.data()),v.size());};
 word(nvs.size());for(const auto&space:nvs){bytes(space.first);word(space.second.size());for(const auto&row:space.second){bytes(row.first);bytes(row.second);}}rows.close();assert(rows);
}
static void loadLocalState(const std::string&path){
 std::ifstream in(path+"/flash.bin",std::ios::binary);Bytes b{std::istreambuf_iterator<char>(in),{}};assert(b.size()==native_lfs_fixture_flash_size()&&!native_lfs_fixture_restore(b.data(),b.size()));
 std::ifstream rows(path+"/nvs.bin",std::ios::binary);
 auto word=[&](){uint32_t n=0;rows.read(reinterpret_cast<char*>(&n),4);assert(rows&&n<1024*1024);return n;};
 auto str=[&](){auto n=word();assert(n<64);std::string v(n,' ');rows.read(v.data(),n);assert(rows);return v;};
 nvs.clear();auto count=word();assert(count<16);for(unsigned i=0;i<count;++i){auto space=str();auto entries=word();assert(entries<64);for(unsigned j=0;j<entries;++j){auto key=str();auto n=word();auto&v=nvs[space][key];v.resize(n);rows.read(reinterpret_cast<char*>(v.data()),n);assert(rows);}}
}

extern "C" void local_port_after_nvs_commit(){
 if(mode!="fresh-cut"&&mode!="legacy-cut")return;
 auto it=nvs.find("rnr00000006");if(it==nvs.end())return;
 auto row=it->second.find("root");if(row==it->second.end()||row->second.size()!=48)return;
 const char*expected=mode=="fresh-cut"?"RTENFL1":"RTENLL1";
 if(memcmp(row->second.data(),expected,8))return;
 assert(task==Task::NativeSettings&&M::held()&&!core(6)->ready());
 saveLocalState(output);noExternal();
 puts("PROCESS CUT after exact durable local initialization receipt, before bank publication");fflush(stdout);std::_Exit(77);
}

static void seedRefusedMetadata(const std::string&kind){
 Bytes root(48,0);const char*magic=kind=="external-fresh"?"RTENFI1":kind=="external-import"?"RTENLI1":kind=="external-unclassified"?"RTENINIT":"BADROOT!";
 memcpy(root.data(),magic,8);root[8]=6;
 if(kind=="external-fresh"||kind=="external-import")memset(root.data()+12,0x31,32);
 uint32_t crc=~uint32_t(0);for(unsigned n=0;n<44;++n){crc^=root[n];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&uint32_t(-int(crc&1)));}crc=~crc;
 for(unsigned n=0;n<4;++n)root[44+n]=uint8_t(crc>>(n*8));
 nvs["rnr00000006"]["root"]=root;
}
static void localProof(){
 assert(core(6)->ready()&&core(6)->formatVersion()==5&&core(6)->durabilityPolicy()==C::DurabilityPolicy::LocalOnlyV1);
 assert(!core(6)->identity().known&&!core(6)->pendingCount()&&!core(6)->exportPrepared()&&!core(1)->ready());
 noExternal();
}

extern "C" void test_local_port_app(){
 assert(mode=="held-grants"&&availability(6)==RISC_KEY_VALUE_UNAVAILABLE);
 const auto*api=risc_runtime_get_api(1);assert(api);
 risc_runtime_capability_v1 data{sizeof(data)},atomic{sizeof(atomic)},status{sizeof(status)};
 assert(api->acquire(RISC_KEY_VALUE_CAPABILITY,1,6,&data));
 assert(api->acquire(RISC_KEY_VALUE_ATOMIC_CAPABILITY,1,6,&atomic));
 assert(api->acquire(RISC_KEY_VALUE_AVAILABILITY_CAPABILITY,1,6,&status));
 const auto&kv=*static_cast<const risc_key_value_v1*>(data.api);const auto&writer=*static_cast<const risc_key_value_atomic_v1*>(atomic.api);
 const auto before=nvs;char bytes[16]="draft";uint32_t size=0;
 assert(kv.get(kv.context,"opaque:nonwifi",bytes,sizeof(bytes),&size)==RISC_KEY_VALUE_NOT_FOUND&&!strcmp(bytes,"draft"));
 assert(kv.put(kv.context,"opaque:nonwifi","default",7)!=RISC_KEY_VALUE_OK&&nvs==before);
 discovery();localProof();assert(availability(6)==RISC_KEY_VALUE_RECONCILIATION_PENDING);
 risc_key_value_atomic_state_v1 captured{sizeof(captured)};assert(writer.state(writer.context,&captured)==RISC_ATOMIC_BUSY);
 assert(api->release(&atomic));assert(L::checkpoint(false)&&availability(6)==RISC_KEY_VALUE_RECONCILIATION_PENDING);
 assert(api->release(&data));assert(L::checkpoint(false)&&availability(6)==RISC_KEY_VALUE_AVAILABLE);
 assert(api->release(&status));noExternal();
}

int main(int argc,char**argv){
 assert(argc==4);mode=argv[2];output=argv[3];
 if(mode=="probe-external-acquire"){
  Port cpu(hardware());port=&cpu;RiscBoot::Port boot{owner,[](risc_runtime_health_v1*){return true;},schedule,[](const char*){return true;},bind};
  RiscBoot::Runtime runtime(boot);P deliberatelyForbidden(runtime,cpu,6,0);
  probeControl=true;(void)deliberatelyForbidden.acquire();return 1; // must be intercepted before acquire body
 }
 assert(native_lfs_fixture_reset()==0);
 nvs["rte00000001"]["quick_radio"]={1,0,0,0};
 const auto peer=nvs.at("rte00000001");
 const bool refused=mode=="external-fresh"||mode=="external-import"||mode=="external-unclassified"||mode=="malformed-root";
 if(refused)seedRefusedMetadata(mode);
 const bool resume=mode=="fresh-resume"||mode=="legacy-resume";
 const bool reboot=mode=="legacy-reboot"||mode=="fresh-reboot";
 const bool imported=mode=="legacy"||mode=="legacy-reboot"||mode=="legacy-cut"||mode=="legacy-resume"||mode=="held-grants"||mode=="retained-init"||mode=="cancel-inspect"||mode=="cancel-init"||mode=="cancel-committed";
 if(imported)nvs["rte00000006"]["opaque:nonwifi"]={'k','e','e','p'};
 if(reboot||resume)loadLocalState(output+"/../"+(imported?(resume?"legacy-cut":"legacy"):(resume?"fresh-cut":"fresh")));
 const auto original=nvs;
 native_lfs_fixture_shutdown();native_lfs_fixture_select_appdata();
 assert(M::prepare(native_lfs_fixture_partition()));
 assert(!native_lfs_fixture_reboot()&&M::finish());
 if(!reboot&&!resume){M::Guard g(M::Access::AppData);assert(g.held);assert(!native_lfs_fixture_mkdir("/n00000001"));assert(!native_lfs_fixture_seed("/n00000001/peer","preserve",8));}
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
 boot.appExitSafe=[](){return L::continuing()?port->nativeDiscoveryAppExitSafe():port->appExitSafe()&&L::exitSafe();};
 boot.providerStorageSafe=[](){return !L::retained()&&(L::continuing()?port->nativeDiscoveryProviderSafe():port->providerStorageSafe());};
 boot.retainedDelay=schedule;boot.nativeOwnerCheckpoint=[](bool admit){return L::checkpoint(admit);};
 boot.nativeDiscoveryCheckpoint=[](){return L::discoveryCheckpoint();};boot.nativeSettingsShutdown=[](){return L::shutdown();};
 RiscTest::FileSources sources({argv[1]});RiscBoot::Runtime runtime(boot);assert(sources.bind(runtime)&&L::bind(runtime));
 if(!runtime.prepare(argv[1])){fprintf(stderr,"prepare: %s\n",runtime.error());assert(false);}
 const auto before=flashBytes();
 if(refused){
  assert(!L::hydrate(runtime,cpu)&&!core(6)->ready()&&nvs==original&&flashBytes()==before);
  const auto e=W::coordinator(6)->endpoint().api();risc_key_value_atomic_state_v1 state{sizeof(state)};assert(e.state(e.context,&state)!=RISC_ATOMIC_OK);
  assert(!queuedInitializations&&!enteredInitializations&&L::exitSafe()&&L::shutdown());Shim::stop();noExternal();
  printf("PASS local-only policy refusal %s: no availability or mutation\n",mode.c_str());return 0;
 }
 assert(L::hydrate(runtime,cpu)&&L::ready()&&L::attachOwner(runtime,cpu));
 assert(core(6)->ready()==reboot&&availability(6)==(reboot?RISC_KEY_VALUE_AVAILABLE:RISC_KEY_VALUE_UNAVAILABLE));
 const auto writer=W::coordinator(6)->endpoint().api();risc_key_value_atomic_state_v1 state{sizeof(state)};
 if(!reboot)assert(writer.state(writer.context,&state)!=RISC_ATOMIC_OK);noExternal();
 if(mode=="held-grants"){assert(runtime.run()&&!runtime.retained());}
 else if(!reboot)assert(L::discoveryCheckpoint()&&L::discovering()&&!L::exitSafe());
 if(mode=="cancel-inspect")requestCancel=true;
 bool cancelledAtInit=false,cancelledCommitted=false,retainedFaultArmed=false;
 for(unsigned i=0;i<300000&&L::discovering();++i){
  const bool checkpoint=L::discoveryCheckpoint();
  if(!checkpoint){assert(mode=="retained-init"&&L::retained());break;}
  if(mode=="retained-init"&&queuedInitializations&&!enteredInitializations&&!retainedFaultArmed){
   native_lfs_fixture_fault(NATIVE_LFS_FLASH_PROG,2,NATIVE_LFS_FAIL_BEFORE);retainedFaultArmed=true;
  }
  if(mode=="cancel-init"&&queuedInitializations&&!enteredInitializations&&!requestCancel){
   assert(!core(6)->ready()&&availability(6)!=RISC_KEY_VALUE_AVAILABLE);
   requestCancel=true;cancelledAtInit=true;
   assert(L::discoveryCheckpoint()); // forward cancellation before the worker ticket
  }
  schedule(1);
  if(mode=="cancel-committed"&&completedInitializations&&core(6)->ready()&&!requestCancel){
   assert(L::discovering()&&!L::exitSafe()&&availability(6)!=RISC_KEY_VALUE_AVAILABLE);
   requestCancel=true;cancelledCommitted=true;
  }
 }
 if(mode=="retained-init"){
  assert(retainedFaultArmed&&L::retained()&&!L::exitSafe()&&!W::exitSafe()&&M::retained()&&availability(6)!=RISC_KEY_VALUE_AVAILABLE);
  assert(nvs.at("rte00000001")==peer&&nvs.at("rte00000006")==original.at("rte00000006"));
  native_lfs_fixture_counters beforeCalls{},afterCalls{};native_lfs_fixture_get_counters(&beforeCalls);const auto beforeNvsCalls=nvsCalls;
  assert(!L::checkpoint(true)&&!L::shutdown());native_lfs_fixture_get_counters(&afterCalls);
  assert(!memcmp(&beforeCalls,&afterCalls,sizeof(beforeCalls))&&nvsCalls==beforeNvsCalls);noExternal();
  // Permanent retained controls are not destroyed. Only this isolated process
  // ends; all ordinary cases keep normal teardown and LeakSanitizer enabled.
  puts("PASS local-only retained initialization: unavailable, custody held, later I/O blocked");fflush(stdout);std::_Exit(87);
 }
 assert(!L::discovering()&&L::exitSafe()&&!L::retained());
 if(mode=="cancel-inspect"||mode=="cancel-init"){
  assert(mode!="cancel-init"||cancelledAtInit);
  assert(!enteredInitializations&&!core(6)->ready()&&nvs==original&&flashBytes()==before);
 }else if(mode=="fresh"&&!RISC_NATIVE_SETTINGS_LOCAL_FRESH){
  assert(!core(6)->ready()&&availability(6)!=RISC_KEY_VALUE_AVAILABLE&&nvs==original&&flashBytes()==before);
 }else{
  localProof();assert(availability(6)==RISC_KEY_VALUE_AVAILABLE);
  if(reboot){assert(!enteredInitializations&&!queuedInitializations&&core(6)->revision()==2&&core(6)->sequence()==1);value(*core(6),"opaque:new","value");}
  if(mode=="cancel-committed")assert(cancelledCommitted&&enteredInitializations==1&&queuedInitializations==1);
  if(imported){assert(core(6)->baseline().archiveObligation());value(*core(6),"opaque:nonwifi","keep");assert(nvs.at("rte00000006")==original.at("rte00000006"));}
  assert(!writer.state(writer.context,&state));
  risc_key_value_atomic_mutation_v1 mutation{"opaque:new",RISC_ATOMIC_PUT,"value",5};uint64_t token=0;
  assert(!writer.submit(writer.context,state.revision,&mutation,1,"native-local-edit",17,&token)&&token);
  schedule(1);assert(!W::exitSafe()); // accepted receipt deliberately remains uncollected
  risc_key_value_atomic_receipt_v1 receipt{sizeof(receipt)};int rc;
  do{schedule(1);rc=writer.collect(writer.context,token,&receipt);}while(rc==RISC_ATOMIC_PENDING);
  assert(!rc&&!receipt.status);value(*core(6),"opaque:new","value");localProof();
  assert(L::checkpoint(true)&&L::exitSafe());noExternal();
 }
 assert(nvs.at("rte00000001")==peer);
 {M::Guard g(M::Access::AppData);assert(g.held);char b[8];int fd=native_lfs_fixture_open("/n00000001/peer",O_RDONLY);assert(fd>=0&&native_lfs_fixture_read(fd,b,8)==8&&!memcmp(b,"preserve",8)&&!native_lfs_fixture_close(fd));}
 assert(L::shutdown()&&W::exitSafe()&&!M::retained()&&handles.empty()&&iterators.empty());
 Shim::stop();noExternal();if(core(6)->ready())saveLocalState(output);
 printf("PASS local-only port %s fresh_permission=%d workers=2 stack_each=%u internal_peak=%zu payload_peak=%zu; host-only, not target capacity proof\n",mode.c_str(),RISC_NATIVE_SETTINGS_LOCAL_FRESH,unsigned(W::StackBytes),Shim::peakInternal,Shim::peakPayload);
 return 0;
}
