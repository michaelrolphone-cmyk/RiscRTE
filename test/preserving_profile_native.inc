// Real private whole-store admission, with an installed policy and candidate
// fixture. Only flash/VFS boundaries are modeled; providers never execute.
static void preservingNative(const char* loader,const char* directory,bool closeFailure){
 using namespace RiscBankStore;namespace fs=std::filesystem;
 const auto boot=validatedBootloader(loader);
 memcpy(flash.data(),boot.data(),boot.size());otaState=ESP_OTA_IMG_VALID;firmware(0,RISC_BUILD_VERSION);
 uint8_t fw[32],store[32];SHA256(flash.data()+RiscUpdate::FirmwareOffset[0],imageSize,fw);SHA256(flash.data()+RiscUpdate::StoreOffset[0],RiscUpdate::StoreBytes,store);
 auto record=RiscUpdate::makeRecord(0,imageSize,fw,store);memcpy(flash.data()+RiscUpdate::JournalOffset,&record,sizeof(record));assert(prepareBoot(own,restartSafe,safe));
 fsRoot=std::string(directory)+"-installed";fs::create_directories(fsRoot+"/bootfs");
 modelStageRoot=directory;fs::create_directories(modelStageRoot);stagingRoot=modelStageRoot.c_str();modelProvisionFiles=true;
 RiscTest::FileSources sources({RiscTest::FileSources::Mapping{"/bootfs",fsRoot+"/bootfs"},RiscTest::FileSources::Mapping{modelStageRoot,modelStageRoot}},own,safe);fileSources=sources.sources();
 auto text=[](const char*s){return std::vector<uint8_t>(s,s+strlen(s));};
 std::map<std::string,std::vector<uint8_t>> baseline{
  {"boot.json",text(R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"default.json","grants":[]}]})")},
  {"board.json",text(R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})")},
  {"default.json",text(R"({"type":"application","id":"preserved-home","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[]})")},
  {"default.elf",elf("memcpy")}};
 JsonDocument cohort;cohort["schema"]="riscrte.cohort";cohort["schema_version"]=1;cohort["product"]="test";cohort["version"]="1.0.0";cohort["runtime_version"]=RISC_BUILD_VERSION;cohort["source_repo"]="owner/product";cohort["source_revision"]=std::string(40,'a');cohort["layout"]=RiscUpdate::Layout;cohort["store_abi"]=RiscUpdate::StoreAbi;cohort["firmware_size"]=imageSize;cohort["firmware_sha256"]=hexNative(fw);
 std::string encoded;serializeJson(cohort,encoded);baseline["cohort.json"]={encoded.begin(),encoded.end()};
 for(const auto&row:baseline){std::ofstream out(fsRoot+"/bootfs/"+row.first,std::ios::binary);out.write(reinterpret_cast<const char*>(row.second.data()),row.second.size());assert(out);}
 auto hardware=admissionHardware();
 for(unsigned scenario=0;scenario<(closeFailure?1u:4u);++scenario){
  auto files=baseline;
  // Immutable profile digest authorizes bytes within identical policy even
  // when manifest version is unchanged. Native ELF admission still runs.
  files["default.elf"]=elf("memset");
  if(scenario==1)files["board.json"]=text(R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"changed","revision":"unspecified","buses":[],"devices":[]})");
  if(scenario==2)files["boot.json"]=text(R"({"board":"board.json","default_app":"default.elf","drivers":[]})");
  if(scenario==3)files["default.elf"]=elf("esp_restart");
  auto profile=std::make_unique<RiscProvision::Profile>();profile->count=files.size();size_t at=0;
  for(const auto&row:files){auto&f=profile->files[at++];strcpy(f.path,row.first.c_str());f.bytes=row.second.size();SHA256(row.second.data(),row.second.size(),f.sha256);}
  uint8_t digest[32]{};digest[0]=uint8_t(70+scenario);uint64_t token=0;
  assert(provisionBegin(*profile,digest,hardware,nullptr,&token,nullptr,true)==RISC_BANK_OK);
  risc_bank_status_v1 status{};status.struct_size=sizeof(status);
  for(unsigned i=0;i<5000&&!transaction->stagingStore(token);++i)assert(provisionStep(token,&status)==RISC_BANK_OK);
  assert(transaction->stagingStore(token));at=0;
  for(const auto&row:files){assert(provisionWrite(token,at++,row.second.data(),row.second.size())==RISC_BANK_OK);}
  if(closeFailure){metadataClosePath=fsRoot+"/bootfs/boot.json";metadataRequireCandidate=false;}
  const auto result=provisionFinish(token);
  if(closeFailure){assert(result==RISC_BANK_INTEGRITY&&provisionReadRetained&&metadataFailedCloses==1);assert(provisionAbort(token)==RISC_BANK_RETAINED&&!exitSafe()&&!provisionRestart(token)&&!restarts);}
  else {
   assert((result==RISC_BANK_OK)==(scenario==0));
   if(scenario==0){for(unsigned i=0;i<5000;++i){assert(transaction->status(&status));if(status.state==RISC_BANK_READY)break;assert(provisionStep(token,&status)==0);}assert(status.state==RISC_BANK_READY);}
   else assert(provisionActivate(token)==RISC_BANK_STATE);
   assert(provisionAbort(token)==0);
  }
  uint8_t actual[32];SHA256(flash.data()+RiscUpdate::StoreOffset[0],RiscUpdate::StoreBytes,actual);assert(!memcmp(actual,record.storeSha,32));SHA256(flash.data()+RiscUpdate::FirmwareOffset[0],imageSize,actual);assert(!memcmp(actual,record.firmwareSha,32));assert(!hardwareCalls&&!selectorCalls&&!restarts);
 }
 puts(closeFailure?"Preserving native profile: source metadata close retained, no abort/fallback/reset PASS":"Preserving native profile: same-version new image admitted, board/feature changes and forbidden ELF refused, firmware/source preserved PASS");
}
