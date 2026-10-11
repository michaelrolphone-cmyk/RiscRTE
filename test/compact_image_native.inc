// Production NativeBootstrap -> NativeBankStore -> paired journal/selector.
// Flash is the existing NOR/IDF test model; the mount exposes known fixture
// files. Separate pinned-SPIFFS tests verify actual image decoding/mounting.
static std::vector<uint8_t> compactImage(const std::map<std::string,std::vector<uint8_t>>& files){
 std::vector<uint8_t> image(RiscUpdate::StoreBytes,0xff);const uint32_t blocks=image.size()/4096;
 auto put16=[&](size_t at,uint16_t n){image[at]=n;image[at+1]=n>>8;};
 for(uint32_t b=0;b<blocks;++b)put16(b*4096+252,uint16_t(0x20140529u^256u^(blocks-b)));
 uint32_t next=1;uint16_t object=1;
 auto allocate=[&](uint16_t id){if(next%16==0)++next;uint32_t page=next++;
  put16((page/16)*4096+((page%16)-1)*2,id);return page;};
 for(const auto& item:files){
  assert(item.second.size()<251*103 && item.first.size()+2<=32);
  const uint32_t header=allocate(object|0x8000),at=header*256;
  put16(at,object|0x8000);put16(at+2,0);image[at+4]=0xf8;
  uint32_t size=item.second.size();memcpy(image.data()+at+8,&size,4);image[at+12]=1;
  std::string name="/"+item.first;memcpy(image.data()+at+13,name.c_str(),name.size()+1);
  for(size_t offset=0,span=0;offset<item.second.size();offset+=251,++span){
   const uint32_t page=allocate(object),data=page*256;put16(data,object);put16(data+2,span);image[data+4]=0xfc;
   memcpy(image.data()+data+5,item.second.data()+offset,std::min<size_t>(251,item.second.size()-offset));put16(at+49+span*2,page);
  }++object;
 }
 return image;
}
static std::string compactHex(const std::vector<uint8_t>& bytes){
 uint8_t digest[32];SHA256(bytes.data(),bytes.size(),digest);std::string result;
 for(uint8_t c:digest){result+="0123456789abcdef"[c>>4];result+="0123456789abcdef"[c&15];}return result;
}
static void compactNative(const std::string& mode,const char* bootloader,const char* directory){
 using namespace RiscBankStore;namespace fs=std::filesystem;
 std::ifstream input(bootloader,std::ios::binary);std::vector<uint8_t> boot((std::istreambuf_iterator<char>(input)),{});assert(boot.size()==15104);
 memcpy(flash.data(),boot.data(),boot.size());otaState=ESP_OTA_IMG_VALID;firmware(0,RISC_BUILD_VERSION);
 std::fill(flash.begin()+0x9000,flash.begin()+0xf000,0x6b);
#ifdef RISC_PAIRED_APP_DATA
 std::fill(flash.begin()+0x270000,flash.begin()+0x2f0000,0x5a);
#endif
 uint8_t firmwareHash[32],storeHash[32];SHA256(flash.data()+0x10000,imageSize,firmwareHash);
 SHA256(flash.data()+RiscUpdate::StoreOffset[0],RiscUpdate::StoreBytes,storeHash);
 auto record=RiscUpdate::makeRecord(0,imageSize,firmwareHash,storeHash);
 memcpy(flash.data()+RiscUpdate::JournalOffset,&record,sizeof(record));assert(prepareBoot(own,restartSafe,safe));
 const auto preserved=flash;modelStageRoot=directory;fs::create_directories(modelStageRoot);stagingRoot=modelStageRoot.c_str();modelProvisionFiles=false;
 auto text=[](const char* s){return std::vector<uint8_t>(s,s+strlen(s));};
 std::map<std::string,std::vector<uint8_t>> files{
  {"boot.json",text(R"({"board":"board.json","default_app":"default.elf","drivers":[]})")},
  {"board.json",text(R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})")},
  {"default.elf",elf("memcpy")}};
 if(mode=="image-bad-board")files["board.json"]=text("{}");
 if(mode=="image-bad-elf")reinterpret_cast<elf32_hdr_t*>(files["default.elf"].data())->machine=3;
 if(mode=="image-native-mismatch"){
  JsonDocument cohort;cohort["schema"]="riscrte.cohort";cohort["schema_version"]=1;
  cohort["product"]="image-test";cohort["version"]="1.0.0";cohort["source_repo"]="test/product";
  cohort["source_revision"]=std::string(40,'a');cohort["runtime_version"]=RISC_BUILD_VERSION;
  cohort["layout"]=RiscUpdate::Layout;cohort["store_abi"]=RiscUpdate::StoreAbi;
  cohort["firmware_size"]=imageSize;cohort["firmware_sha256"]=std::string(64,'0');
  std::string encoded;serializeJson(cohort,encoded);files["cohort.json"]={encoded.begin(),encoded.end()};
 }
 auto image=compactImage(files);
 if(mode=="image-bad-magic")image[252]^=1;
 if(mode=="image-bad-name")std::fill(image.begin()+256+13,image.begin()+256+45,'A');
 if(mode=="image-bad-capacity")for(size_t at=4096;at<image.size();at+=4096){image[at]=0;image[at+1]=0;}
 if(mode=="image-free-page-programmed")image[image.size()-1]=0;
 for(auto& item:files){std::ofstream out(fs::path(modelStageRoot)/item.first,std::ios::binary);out.write(reinterpret_cast<const char*>(item.second.data()),item.second.size());}
 if(mode=="image-extra-file"){std::ofstream out(fs::path(modelStageRoot)/"unexpected.json");out<<"{}";}
 if(mode=="image-file-mismatch"){std::ofstream out(fs::path(modelStageRoot)/"default.elf");out<<"corrupt";}
 bootNet.mode=mode=="image-corrupt"?"bootstrap-corrupt":mode=="image-length"?"bootstrap-length-mismatch":mode=="image-download-fail"?"bootstrap-download-fail":mode;
 bootNet.files["image.bin"]=image;
 bootNet.profile=R"({"schema":"riscrte.provisioning","schema_version":3,"wifi":{"ssid":"test-network","password":"test-only-password"},"image":{"url":"https://example.test/image.bin","bytes":)"+
  std::to_string(image.size())+",\"sha256\":\""+compactHex(image)+"\"},\"files\":[";
 bool first=true;for(auto& item:files){if(!first)bootNet.profile+=',';first=false;
  bootNet.profile+="{\"path\":\""+item.first+"\",\"bytes\":"+std::to_string(item.second.size())+",\"sha256\":\""+compactHex(item.second)+"\"}";
 }bootNet.profile+="]}";
 bootNet.descriptor=R"({"schema":"riscrte.bootstrap","schema_version":1,"profile_key":"profile"})";
 auto hardware=admissionHardware();hardware.sleep=[](uint32_t n){vTaskDelay(n);};
 hardware.radioJoin=[](const char* ssid,const char* password){assert(!strcmp(ssid,"test-network")&&!strcmp(password,"test-only-password"));++bootNet.joins;bootNet.radio=true;return true;};
 hardware.radioState=[](uint8_t* state,int8_t* rssi){*state=bootNet.radio?2:0;*rssi=-30;return true;};
 hardware.radioAddresses=[](uint8_t* station,uint8_t* ap){memset(station,0,12);memset(ap,0,12);station[0]=192;return true;};
 hardware.radioLeave=[](){++bootNet.leaves;bootNet.radio=false;return true;};hardware.radioIdle=[](){return !bootNet.radio;};
 hardware.httpClient=&bootstrapHttp;hardware.httpIdle=[](){return !bootNet.http;};hardware.httpSafe=[](){return !bootNet.broken;};
 RiscBootstrap::Port port{hardware,nullptr,safe,{nullptr,bootstrapInput},{nullptr,bootstrapTime,[](void*){++bootNet.timeStops;return true;}}};
 if(mode=="image-mount-fail")mountFailure=true;
 if(mode=="image-unmount-retained")unmountFailure=true;
 if(mode=="image-admission-close")failAdmissionClose=true;
 if(mode=="image-selection-unknown")selectFailure=true;
 if(mode=="image-receipt-fail")attemptWriteFault=1;
 if(mode=="image-offline-consumed"){
  uint8_t digest[32];SHA256(reinterpret_cast<const uint8_t*>(bootNet.profile.data()),bootNet.profile.size(),digest);
  auto previous=record;previous.bank=1;previous.storeSha[0]^=1;previous.crc=RiscUpdate::crc32(&previous,offsetof(RiscUpdate::Record,crc));
  auto receipt=RiscUpdate::makeAttempt(0,digest,record,previous);
  memcpy(flash.data()+RiscUpdate::JournalOffset+RiscUpdate::AttemptOffset,&receipt,sizeof(receipt));
 }
 auto result=RiscBootstrap::run(port,modelStageRoot.c_str());
 if(mode=="image-offline-consumed"){
  assert(result.outcome==RiscBootstrap::Outcome::Installed && result.reason==RiscBootstrap::Reason::Unchanged);
  assert(!bootNet.opens&&!bootNet.joins&&!writes&&!selectorCalls&&!restarts&&!hardwareCalls);
  puts("Compact image consumed profile: offline without transport or writes PASS");return;
 }
 if(mode=="image-interrupt"){
  assert(result.outcome==RiscBootstrap::Outcome::Installed && !provisionFiles && !provisionToken && !selectorCalls && !restarts);
  assert(std::equal(preserved.begin(),preserved.begin()+0x800000,flash.begin()));
  bootNet.mode="image-success";result=RiscBootstrap::run(port,modelStageRoot.c_str());
 }
 const bool selected=mode=="image-success"||mode=="image-selection-unknown"||mode=="image-odd-chunks"||mode=="image-interrupt";
 const bool retained=mode=="image-unmount-retained"||mode=="image-admission-close";
 assert((result.outcome==RiscBootstrap::Outcome::Stopped)==(selected||retained));
 assert(!hardwareCalls && bootNet.opens==(mode=="image-interrupt"?2u:1u) && !bootNet.radio && !bootNet.http);
 assert(!fs::exists(fs::path(modelStageRoot)/RiscProvision::StoreFiles::DigestFile));
 assert(std::equal(preserved.begin(),preserved.begin()+0x800000,flash.begin()));
 assert(std::equal(preserved.begin()+RiscUpdate::JournalOffset,preserved.begin()+RiscUpdate::JournalOffset+4096,flash.begin()+RiscUpdate::JournalOffset));
 assert(std::equal(preserved.begin()+0xff4000,preserved.end(),flash.begin()+0xff4000));
 if(mode=="image-bad-name"||mode=="image-bad-magic"||mode=="image-bad-capacity"||mode=="image-free-page-programmed")assert(!compactMountCalls);
 if(selected){
  assert(selectorCalls==1 && restarts==1);
  assert(std::equal(image.begin(),image.end(),flash.begin()+RiscUpdate::StoreOffset[1]));
  RiscUpdate::Record target{};RiscUpdate::ProvisionAttempt attempt{};
  memcpy(&target,flash.data()+RiscUpdate::JournalOffset+4096,sizeof(target));
  memcpy(&attempt,flash.data()+RiscUpdate::JournalOffset+4096+RiscUpdate::AttemptOffset,sizeof(attempt));
  assert(RiscUpdate::validAttempt(attempt,target,1)&&RiscUpdate::sameAttemptSource(attempt,record));
  uint8_t digest[32];SHA256(reinterpret_cast<const uint8_t*>(bootNet.profile.data()),bootNet.profile.size(),digest);assert(!memcmp(digest,attempt.profileSha,32));
 }else assert(!selectorCalls&&!restarts);
 if(retained)assert(!RiscBankStore::exitSafe());
 else if(!selected)assert(RiscBankStore::exitSafe()&&!provisionFiles&&!provisionToken);
 std::cout<<"Compact image native download, readback, admission and selection: "<<mode<<" PASS\n";
}
