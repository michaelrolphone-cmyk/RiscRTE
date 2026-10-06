// Fake VFS redirects the adapter's fixed mount paths to fixture directories.
// It never formats a filesystem. Raw partition verification remains production
// code; SPIFFS's own on-media parser is covered by target/store image tests.
#include <filesystem>
#include <dirent.h>
static std::string fsRoot;
static unsigned receiptMode=0;
static bool cohortLiveClose=false;
static uint8_t consumedProfile[32]{};
extern "C" FILE* __real_fopen(const char*,const char*);
extern "C" int __real_fclose(FILE*);
static std::map<FILE*,std::pair<std::string,bool>> metadataStreams;
static std::string metadataClosePath;
static unsigned metadataFailedCloses=0;
static bool metadataRequireCandidate=true;
extern "C" DIR* __real_opendir(const char*);
extern "C" int __real_stat(const char*,struct stat*);
extern "C" int __real_lstat(const char*,struct stat*);
static std::string redirected(const char* path){
 if(!fsRoot.empty() && (!strncmp(path,"/bootfs",7) || !strncmp(path,"/updatefs",9)))return fsRoot+path;
 return path;
}
extern "C" FILE* __wrap_fopen(const char* path,const char* mode){auto mapped=redirected(path);FILE* file=__real_fopen(mapped.c_str(),mode);if(file)metadataStreams[file]={mapped,mode[0]=='r'};return file;}
extern "C" int __wrap_fclose(FILE* file){
 auto entry=metadataStreams.find(file);const bool fail=entry!=metadataStreams.end() && entry->second.second &&
   entry->second.first==metadataClosePath && (!metadataRequireCandidate || RiscBankStore::candidateCpu);
 if(entry!=metadataStreams.end())metadataStreams.erase(entry);
 const int result=__real_fclose(file);
 if(fail){metadataClosePath.clear();++metadataFailedCloses;return EOF;}
 return result;
}
extern "C" DIR* __wrap_opendir(const char* path){auto mapped=redirected(path);return __real_opendir(mapped.c_str());}
extern "C" int __wrap_stat(const char* path,struct stat* info){auto mapped=redirected(path);return __real_stat(mapped.c_str(),info);}
extern "C" int __wrap_lstat(const char* path,struct stat* info){auto mapped=redirected(path);return __real_lstat(mapped.c_str(),info);}
static void saveNative(const std::string& path,const std::string& text){std::ofstream out(fsRoot+path);out<<text;assert(out.good());}
static void saveElf(const std::string& path,bool provider,const char* imported="memcpy"){
 auto data=elf(imported);
 if(provider){
  auto* sym=reinterpret_cast<elf32_sym_t*>(data.data()+416);sym[2].name=40;
  strcpy(reinterpret_cast<char*>(data.data()+552),imported);
  strcpy(reinterpret_cast<char*>(data.data()+513),"t5_driver_get");
 }
 std::ofstream out(fsRoot+path,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),data.size());assert(out.good());
}
static std::string hexNative(const uint8_t* bytes){std::string result;for(unsigned i=0;i<32;++i){const char* digits="0123456789abcdef";result+=digits[bytes[i]>>4];result+=digits[bytes[i]&15];}return result;}
static std::vector<uint8_t> cohortBytes;
static risc_bank_cohort_v1 cohortImage;
static void identityNative(const char* root,const char* version,const char* runtimeVersion,const char* revision){
 JsonDocument doc;doc["schema"]="riscrte.cohort";doc["schema_version"]=1;doc["product"]="twatch-s3";doc["version"]=version;
 doc["runtime_version"]=runtimeVersion;doc["source_repo"]="owner/watch";doc["source_revision"]=revision;
 doc["layout"]=RiscUpdate::Layout;doc["store_abi"]=RiscUpdate::StoreAbi;doc["firmware_size"]=cohortImage.firmware_size;
 doc["firmware_sha256"]=hexNative(cohortImage.firmware_sha256);std::string text;serializeJson(doc,text);saveNative(std::string(root)+"/cohort.json",text);
}
extern "C" void test_cohort_update(){
 using namespace RiscBankStore;
 assert(confirmBoot() && confirms==1);
 risc_bank_cohort_status_v1 installed{};installed.struct_size=sizeof(installed);assert(api.cohort_status(nullptr,&installed)==0 && !strcmp(installed.version,"1.0.1"));
 auto send=[](uint64_t token){
  for(size_t i=0;i<cohortBytes.size();){uint32_t n=std::min(size_t(4081),cohortBytes.size()-i);int result=api.write(nullptr,token,cohortBytes.data()+i,n);if(result)return result;i+=n;}
  return api.finish(nullptr,token);
 };
 auto advance=[](uint64_t token){
  risc_bank_status_v1 state{};state.struct_size=sizeof(state);
  for(unsigned i=0;i<5000;++i){int result=api.step(nullptr,token,&state);if(result)return result;if(state.state==RISC_BANK_READY)return 0;}
  assert(false);return -1;
 };
 for(unsigned bad=0;bad<7;++bad){auto request=cohortImage;
  if(bad==0)strcpy(request.version,"1.0.1");
  if(bad==1)strcpy(request.product,"other");
  if(bad==2)strcpy(request.source_repo,"other/watch");
  if(bad==3)strcpy(request.runtime_version,"0.1.10"); // Stored anchor is older; actual native wins.
  if(bad==4)request.source_revision[0]='Z';
  if(bad==5)request.active_store_sha256[0]^=1;
  if(bad==6)memset(request.product,'x',sizeof(request.product));
  uint64_t token=99;unsigned before=writes;assert(api.begin_cohort(nullptr,&request,&token)==RISC_BANK_INVALID && !token && writes==before);
 }
 for(unsigned bad=0;bad<7;++bad){
  uint64_t token=0;assert(api.begin_cohort(nullptr,&cohortImage,&token)==0 && token);
  if(bad==0)identityNative("/updatefs","1.0.2",RISC_BUILD_VERSION,std::string(40,'c').c_str());
  if(bad==1)saveElf("/updatefs/new.elf",false,"esp_partition_write");
  if(bad==2)saveElf("/updatefs/new-driver.elf",true,"fopen");
  if(bad==3)saveNative("/updatefs/extra.json","{}");
  if(bad==4)mountFailure=true;
  if(bad==5)unmountFailure=true;
  if(bad==6)writeFailure=true;
  int result=send(token);if(!result)result=advance(token);
  assert(result!=0 && !transaction->activated(token));
  if(bad==5){assert(result==RISC_BANK_RETAINED);assert(api.abort(nullptr,token)==RISC_BANK_RETAINED);}
  mountFailure=unmountFailure=writeFailure=false;
  assert(api.abort(nullptr,token)==0 && !mounted);
  identityNative("/updatefs","1.0.2",RISC_BUILD_VERSION,cohortImage.source_revision);
  saveElf("/updatefs/new.elf",false);saveElf("/updatefs/new-driver.elf",true);
  std::filesystem::remove(fsRoot+"/updatefs/extra.json");
 }
 if(receiptMode){
  // A carried receipt is part of readiness: uncertain writes never select.
  for(unsigned fault=1;fault<=3;++fault){
   uint64_t token=0;assert(api.begin_cohort(nullptr,&cohortImage,&token)==0 && send(token)==0);
   attemptWriteFault=fault;assert(advance(token)==RISC_BANK_IO);attemptWriteFault=0;
   assert(!selectorCalls && !restarts && api.activate(nullptr,token)==RISC_BANK_STATE);
   assert(api.abort(nullptr,token)==0);
  }
 }
 uint64_t token=0;assert(api.begin_cohort(nullptr,&cohortImage,&token)==0);assert(api.write(nullptr,token,cohortBytes.data(),123)==0);assert(api.abort(nullptr,token)==0);
 assert(api.begin_cohort(nullptr,&cohortImage,&token)==0 && send(token)==0 && advance(token)==0);
 if(receiptMode){
  RiscUpdate::Record selected{};RiscUpdate::ProvisionAttempt receipt{};
  memcpy(&selected,flash.data()+RiscUpdate::JournalOffset+4096,sizeof(selected));
  memcpy(&receipt,flash.data()+RiscUpdate::JournalOffset+4096+RiscUpdate::AttemptOffset,sizeof(receipt));
  assert(RiscUpdate::validAttempt(receipt,selected,1) && !memcmp(receipt.profileSha,consumedProfile,32));
  assert(!std::filesystem::exists(fsRoot+"/updatefs/.provision-sha256"));
 }
 assert(!mounted && api.activate(nullptr,token)==0);
 if(cohortLiveClose){
  metadataClosePath=fsRoot+"/bootfs/boot.json";metadataRequireCandidate=false;
  uint8_t bytes[4096];uint32_t actual=99;
  assert(api.get_app(nullptr,0,bytes,sizeof(bytes),&actual)!=RISC_BANK_OK && !actual);
  assert(runtime->metadataCloseRetained() && metadataFailedCloses==1 && !exitSafe() && !cleanup(nullptr));
  assert(!api.restart(nullptr,token) && !restarts);return;
 }
 assert(!api.restart(nullptr,token) && restarts==1);
 assert(api.abort(nullptr,token)==RISC_BANK_STATE);
}
static void cohortNative(const char* loader,const char* root){
 using namespace RiscBankStore;namespace fs=std::filesystem;
 fsRoot=root;fs::create_directories(fsRoot+"/bootfs");fs::create_directories(fsRoot+"/updatefs");
 std::ifstream input(loader,std::ios::binary);std::vector<uint8_t> boot((std::istreambuf_iterator<char>(input)),{});assert(boot.size()==15104);
 memcpy(flash.data(),boot.data(),boot.size());firmware(0,RISC_BUILD_VERSION);firmware(1,RISC_BUILD_VERSION);
 cohortBytes.assign(flash.begin()+0x800000,flash.begin()+0x800000+imageSize);cohortBytes.resize(imageSize+RiscUpdate::StoreBytes,0x79);
 cohortImage={};cohortImage.struct_size=sizeof(cohortImage);cohortImage.store_abi=RiscUpdate::StoreAbi;cohortImage.firmware_size=imageSize;cohortImage.store_size=RiscUpdate::StoreBytes;
 strcpy(cohortImage.product,"twatch-s3");strcpy(cohortImage.version,"1.0.2");strcpy(cohortImage.runtime_version,RISC_BUILD_VERSION);strcpy(cohortImage.source_repo,"owner/watch");memset(cohortImage.source_revision,'b',40);
 SHA256(cohortBytes.data(),cohortBytes.size(),cohortImage.sha256);SHA256(cohortBytes.data(),imageSize,cohortImage.firmware_sha256);SHA256(cohortBytes.data()+imageSize,RiscUpdate::StoreBytes,cohortImage.store_sha256);
 uint8_t fw[32],store[32];SHA256(flash.data()+0x10000,imageSize,fw);SHA256(flash.data()+RiscUpdate::StoreOffset[0],RiscUpdate::StoreBytes,store);
 memcpy(cohortImage.active_store_sha256,store,32);auto record=RiscUpdate::makeRecord(0,imageSize,fw,store);memcpy(flash.data()+RiscUpdate::JournalOffset,&record,sizeof(record));
 for(const char* base:{"/bootfs","/updatefs"}){
  saveNative(std::string(base)+"/board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"watch","revision":"rev1","buses":[],"devices":[]})");
  saveNative(std::string(base)+"/default.json",R"({"type":"application","id":"clock","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[]})");
 }
 identityNative("/bootfs","1.0.1","0.1.10",std::string(40,'a').c_str());identityNative("/updatefs","1.0.2",RISC_BUILD_VERSION,cohortImage.source_revision);
 saveNative("/bootfs/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"default.json","grants":[]}]})");
 fs::copy_file(fsRoot+"/cohort-native.elf",fsRoot+"/bootfs/default.elf",fs::copy_options::overwrite_existing);
 saveNative("/updatefs/boot.json",R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"new-driver.json"}],"app_capabilities":[{"manifest":"default.json","grants":[]},{"manifest":"new.json","grants":[{"capability":"test.new","api":1,"instance_id":0}]}]})");
 saveNative("/updatefs/new.json",R"({"type":"application","id":"new","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"new.elf","entry":"app_main","requires":[{"capability":"test.new","api":1}]})");
 saveNative("/updatefs/new-driver.json",R"({"type":"driver","id":"new-provider","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"new-driver.elf","requires":[],"provides":[{"capability":"test.new","api":1}]})");
 saveElf("/updatefs/default.elf",false);saveElf("/updatefs/new.elf",false);saveElf("/updatefs/new-driver.elf",true);
 if(receiptMode){
  bootNet.profile=R"({"schema":"riscrte.provisioning","schema_version":2,"wifi":{"ssid":"test-network","password":"test-only-password"},"base_url":"https://example.test/","files":[)";
  bool first=true;for(const char* name:{"boot.json","board.json","default.elf"}){
   if(!first)bootNet.profile+=",";
   first=false;
   bootNet.profile+="{\"path\":\""+std::string(name)+"\",\"bytes\":1,\"sha256\":\""+std::string(64,'0')+"\"}";
  }bootNet.profile+="]}";
  bootNet.descriptor=R"({"schema":"riscrte.bootstrap","schema_version":1,"profile_key":"profile"})";
  SHA256(reinterpret_cast<const uint8_t*>(bootNet.profile.data()),bootNet.profile.size(),consumedProfile);
  if(receiptMode==1){
   auto prior=record;prior.bank=1;prior.crc=RiscUpdate::crc32(&prior,offsetof(RiscUpdate::Record,crc));
   const auto receipt=RiscUpdate::makeAttempt(0,consumedProfile,record,prior);
   memcpy(flash.data()+RiscUpdate::JournalOffset+RiscUpdate::AttemptOffset,&receipt,sizeof(receipt));
  }else saveNative("/bootfs/.provision-sha256",std::string(reinterpret_cast<const char*>(consumedProfile),32));
 }
 assert(prepareBoot(own,restartSafe,safe));
 auto instance=std::make_unique<RiscBoot::Runtime>(RiscBoot::Port{own,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char*){return true;},bind,nullptr,exitSafe,safe,confirmBoot});
 assert(instance->prepare("/bootfs"));
 assert(instance->run()!=cohortLiveClose);
 if(cohortLiveClose){assert(instance->retained() && instance->metadataCloseRetained());instance.release();
  std::cout<<"Post-activation live metadata close: invocation retained, cleanup/restart refused, zero resets PASS\n";}
 for(size_t i=0x9000;i<0xf000;++i)assert(flash[i]==0xff);
#ifdef RISC_PAIRED_APP_DATA
 for(size_t i=0x270000;i<0x2f0000;++i)assert(flash[i]==0x5a);
#endif
 uint8_t check[32];SHA256(flash.data()+0x10000,imageSize,check);assert(!memcmp(check,fw,32));
 SHA256(flash.data()+RiscUpdate::StoreOffset[0],RiscUpdate::StoreBytes,check);assert(!memcmp(check,store,32));
 if(receiptMode){
  instance.reset();runtime=nullptr;
  auto reboot=[&](unsigned bank,esp_ota_img_states_t state){
   assert(!appFile&&!mounted&&!provisionFiles&&!provisionReadRetained);
   transaction->~Transaction();free(transaction);transaction=nullptr;
   scratch->~Scratch();free(scratch);scratch=nullptr;
   prepared=pending=confirmed=rollbackTrusted=false;replacingFirmware=replacingCohort=false;
   active=bank;otaState=state;writes=selectorCalls=restarts=0;
   assert(prepareBoot(own,restartSafe,safe));
  };
  auto hardware=admissionHardware();hardware.sleep=[](uint32_t n){vTaskDelay(n);};
  hardware.radioJoin=[](const char*,const char*){++bootNet.joins;return false;};
  hardware.radioState=[](uint8_t*,int8_t*){return false;};hardware.radioAddresses=[](uint8_t*,uint8_t*){return false;};
  hardware.radioLeave=[](){return true;};hardware.radioIdle=[](){return true;};
  hardware.httpClient=&bootstrapHttp;hardware.httpIdle=hardware.httpSafe=[](){return true;};
  RiscBootstrap::Port port{hardware,nullptr,safe,{nullptr,bootstrapInput},{}};
  reboot(1,ESP_OTA_IMG_PENDING_VERIFY);
  auto result=RiscBootstrap::run(port,"/bootfs");
  assert(result.outcome==RiscBootstrap::Outcome::Installed && result.reason==RiscBootstrap::Reason::PairUnavailable && !bootNet.inputReads);
  // Model the subsequent fresh boot after normal application health confirms
  // the selected pair. Pending-image gating is exercised above, never bypassed.
  reboot(1,ESP_OTA_IMG_VALID);
  result=RiscBootstrap::run(port,"/bootfs");
  assert(result.outcome==RiscBootstrap::Outcome::Installed && result.reason==RiscBootstrap::Reason::Unchanged);
  assert(!bootNet.joins&&!bootNet.opens&&!writes&&!selectorCalls&&!restarts);
  // A rollback to the still-intact confirmed old pair preserves consumption.
  reboot(0,ESP_OTA_IMG_VALID);result=RiscBootstrap::run(port,"/bootfs");
  assert(result.reason==RiscBootstrap::Reason::Unchanged && !writes);
  reboot(1,ESP_OTA_IMG_VALID);
  memset(flash.data()+RiscUpdate::JournalOffset,0xff,4096); // later failed update overwrote former source
  result=RiscBootstrap::run(port,"/bootfs");
  assert(result.reason==RiscBootstrap::Reason::Unchanged && !writes&&!bootNet.joins&&!selectorCalls);
  std::cout<<"Full-cohort receipt carry: legacy/current source, uncertain write/readback, pending/confirmed reboot, rollback and overwritten former source PASS\n";
 }
 std::cout<<"Native cohort API/metadata/new-app-provider/real-ELF/mount/uncertain-write/cleanup/activation, NVS/appdata and old-pair preservation PASS\n";
}
