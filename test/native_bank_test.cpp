// Execute the real native bank adapter against fake flash/IDF I/O. Format/hash,
// marker, partition admission and boot-state logic remain production code.
#include <cstdio>
static int nativeClose(FILE*);
static FILE* nativeOpen(const char*,const char*);
#define fopen nativeOpen
#define fclose nativeClose
#include "ports/esp32s3/NativeBankStore.cpp"
#include "ports/esp32s3/NativeBootstrap.cpp"
#undef fclose
#undef fopen
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
#include <filesystem>
#include <map>
#include <memory>
static const char* unavailableImport=nullptr;
static bool modelProvisionFiles=false;
static FILE* nativeOpen(const char* path,const char* mode){return std::fopen(path,mode);}
static bool failAdmissionClose=false,failNextNativeClose=false;
static unsigned failedAdmissionCloses=0;
static int nativeClose(FILE* file){
 const int result=std::fclose(file);
 if(failNextNativeClose){failNextNativeClose=false;++failedAdmissionCloses;return EOF;}
 if(failAdmissionClose && RiscBankStore::candidateCpu){failAdmissionClose=false;++failedAdmissionCloses;return EOF;}
 return result;
}
static std::string modelStageRoot;
extern "C" uintptr_t elf_find_sym_default(const char* name){return unavailableImport && !strcmp(name,unavailableImport)?0:1;}
static std::vector<uint8_t> flash(0x1000000,0xff);
static uint32_t ticks=1,active=0,imageSize=8192,writes=0,rollbacks=0,confirms=0,restarts=0,delayScale=1,delays=0,unsafeAfterDelay=0;
static bool ownerEnabled=true,rollbackPossible=true,operationEnabled=true,restartEnabled=true,selectFailure=false;
static esp_ota_img_states_t otaState=ESP_OTA_IMG_PENDING_VERIFY;
static bool rejectedCandidate=false,attemptReadFailure=false;
static unsigned attemptWriteFault=0,selectorCalls=0;
static esp_partition_t table[]={
 {ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,0x10000,0x300000,"app0",false},
 {ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,0x310000,0x4f0000,"bootfs0",false},
 {ESP_PARTITION_TYPE_APP,esp_partition_subtype_t(17),0x800000,0x300000,"app1",false},
 {ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,0xb00000,0x4f0000,"bootfs1",false},
 {ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,0xff0000,0x2000,"otadata",false},
 {ESP_PARTITION_TYPE_DATA,esp_partition_subtype_t(0x40),0xff2000,0x2000,"bank_state",false},
 {ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,0x9000,0x6000,"nvs",false}};
FakeEsp ESP;esp_flash_t chip;esp_flash_t* esp_flash_default_chip=&chip;
uint32_t FakeEsp::getFlashChipSize()const{return flash.size();}
void FakeEsp::restart(){++restarts;}
uint32_t millis(){return ticks;}
void vTaskDelay(unsigned n){ticks+=n*delayScale;if(++delays==unsafeAfterDelay)operationEnabled=false;}
esp_err_t esp_flash_read(esp_flash_t*,void* out,uint32_t off,uint32_t n){if(off+n>flash.size())return -1;memcpy(out,flash.data()+off,n);return 0;}
const esp_partition_t* esp_partition_find_first(esp_partition_type_t t,esp_partition_subtype_t st,const char* label){for(auto& p:table)if(p.type==t && p.subtype==st && !strcmp(label,p.label))return &p;return nullptr;}
esp_err_t esp_partition_read(const esp_partition_t* p,size_t off,void* out,size_t n){if(!p || off+n>p->size)return -1;if(attemptReadFailure&&!strcmp(p->label,"bank_state")&&off==4096+RiscUpdate::AttemptOffset)return -1;memcpy(out,flash.data()+p->address+off,n);return 0;}
esp_err_t esp_partition_write(const esp_partition_t* p,size_t off,const void* in,size_t n){if(!p || off+n>p->size)return -1;++writes;
 if(attemptWriteFault&&!strcmp(p->label,"bank_state")&&off==4096+RiscUpdate::AttemptOffset){
   if(attemptWriteFault==1)return -1;
   if(attemptWriteFault==2){memcpy(flash.data()+p->address+off,in,n/2);return -1;}
   memcpy(flash.data()+p->address+off,in,n);flash[p->address+off+12]^=1;return 0;
 }
 memcpy(flash.data()+p->address+off,in,n);return 0;}
esp_err_t esp_partition_erase_range(const esp_partition_t* p,size_t off,size_t n){if(!p || off+n>p->size)return -1;++writes;memset(flash.data()+p->address+off,0xff,n);return 0;}
const esp_partition_t* esp_ota_get_running_partition(){return &table[active*2];}
esp_err_t esp_ota_get_state_partition(const esp_partition_t* p,esp_ota_img_states_t* s){*s=(rejectedCandidate && p->address==0x800000)?ESP_OTA_IMG_ABORTED:otaState;return 0;}
const esp_app_desc_t* esp_ota_get_app_description(){static esp_app_desc_t d{};strcpy(d.project_name,"arduino-lib-builder");return &d;}
esp_err_t esp_ota_get_partition_description(const esp_partition_t*,esp_app_desc_t* out){*out=*esp_ota_get_app_description();return 0;}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*){++selectorCalls;++writes;return selectFailure?-1:0;}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(){++confirms;otaState=ESP_OTA_IMG_VALID;return 0;}
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(){++rollbacks;return 0;}
bool esp_ota_check_rollback_is_possible(){return rollbackPossible;}
esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t* conf){assert(!conf->format_if_mount_failed);return 0;}
esp_err_t esp_vfs_spiffs_unregister(const char*){
 if(modelProvisionFiles){
  // Filesystem model only: serialize the closed candidate deterministically
  // into fake inactive flash, so production raw readback sees actual file data.
  std::map<std::string,std::vector<uint8_t>> files;
  for(auto& e:std::filesystem::directory_iterator(modelStageRoot))if(e.is_regular_file()){
    std::ifstream f(e.path(),std::ios::binary);files[e.path().filename().string()]={std::istreambuf_iterator<char>(f),{}};}
  size_t at=0xb00000;memset(flash.data()+at,0xff,RiscUpdate::StoreBytes);
  for(auto& e:files){memcpy(flash.data()+at,e.first.c_str(),e.first.size()+1);at+=e.first.size()+1;
    memcpy(flash.data()+at,e.second.data(),e.second.size());at+=e.second.size();assert(at<0xff0000);}
 }
 return 0;
}
esp_err_t esp_image_verify(int,const esp_partition_pos_t*,esp_image_metadata_t* out){out->image_len=imageSize;return 0;}
static bool own(){return ownerEnabled;}static bool safe(){return operationEnabled;}
static bool restartSafe(){return restartEnabled && operationEnabled;}
static unsigned hardwareCalls=0;
static RiscCpu::Hardware admissionHardware(){
 RiscCpu::Hardware h{};h.owner=own;h.now=[]()->uint64_t{return ticks;};h.sleep=[](uint32_t){++hardwareCalls;};
 h.gpioOpen=[](uint8_t,bool,bool,bool){++hardwareCalls;return false;};h.gpioWrite=[](uint8_t,bool){++hardwareCalls;return false;};
 h.gpioRead=[](uint8_t,bool*){++hardwareCalls;return false;};h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){++hardwareCalls;return false;};h.gpioClose=[](uint8_t){++hardwareCalls;return false;};
 h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){++hardwareCalls;return false;};h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){++hardwareCalls;return false;};h.i2cClose=[](uint8_t){++hardwareCalls;return false;};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){++hardwareCalls;return false;};h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){++hardwareCalls;return false;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){++hardwareCalls;return false;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){++hardwareCalls;return false;};h.spiClose=[](uint8_t){++hardwareCalls;return false;};return h;
}
struct BootstrapNetwork {
 std::string mode,profile,descriptor,current;
 std::map<std::string,std::vector<uint8_t>> files;
 bool radio=false,http=false,broken=false;uint64_t token=0;size_t offset=0;
 unsigned joins=0,leaves=0,opens=0,closes=0,polls=0,timeStops=0,reads=0,inputReads=0;
};
static BootstrapNetwork bootNet;
static const risc_http_client_v1 bootstrapHttp{1,sizeof(risc_http_client_v1),nullptr,
 [](void*,const risc_http_request_v1* request,uint64_t* token)->int32_t{
   assert(bootNet.radio&&!bootNet.http&&request->utc_seconds>=RiscCpu::HttpBounds::FirstUtc);++bootNet.opens;
   const std::string prefix="https://example.test/";std::string url=request->url;assert(url.find(prefix)==0);bootNet.current=url.substr(prefix.size());
   assert(bootNet.files.count(bootNet.current));assert(request->max_bytes==bootNet.files[bootNet.current].size());
   bootNet.http=true;bootNet.offset=0;*token=++bootNet.token;
   return bootNet.mode=="bootstrap-http-open-fail"?RISC_HTTP_MEMORY:RISC_HTTP_OK;
 },
 [](void*,uint64_t token,void* out,uint32_t capacity,uint32_t* count)->int32_t{
   assert(bootNet.http&&token==bootNet.token&&capacity<=512);*count=0;++bootNet.reads;
   if(bootNet.mode=="bootstrap-download-fail")return RISC_HTTP_TRANSPORT;
   const auto& data=bootNet.files[bootNet.current];if(bootNet.offset==data.size())return RISC_HTTP_EOF;
   const size_t n=std::min<size_t>(capacity,data.size()-bootNet.offset);memcpy(out,data.data()+bootNet.offset,n);
   if(bootNet.mode=="bootstrap-corrupt"&&bootNet.offset==0)static_cast<uint8_t*>(out)[0]^=1;
   bootNet.offset+=n;*count=uint32_t(n);return RISC_HTTP_OK;
 },
 [](void*,uint64_t token,risc_http_response_v1* info)->int32_t{assert(bootNet.http&&token==bootNet.token);info->status_code=200;
   info->received_bytes=bootNet.offset;info->content_length=bootNet.files[bootNet.current].size()+(bootNet.mode=="bootstrap-length-mismatch"?1:0);return RISC_HTTP_OK;},
 [](void*,uint64_t token)->int32_t{assert(bootNet.http&&token==bootNet.token);++bootNet.closes;
   if(bootNet.mode=="bootstrap-http-retained"){bootNet.broken=true;return RISC_HTTP_RETAINED;}
   bootNet.http=false;return RISC_HTTP_OK;}
};
static RiscBootstrap::TimeStatus bootstrapTime(void*,RiscBootstrap::TimeSample* sample){
 ++bootNet.polls;
 if(bootNet.mode=="bootstrap-time-unavailable")return RiscBootstrap::TimeStatus::Unavailable;
 if(bootNet.mode=="bootstrap-time-timeout")return RiscBootstrap::TimeStatus::Pending;
 if(bootNet.mode=="bootstrap-time-pending"&&bootNet.polls<3)return RiscBootstrap::TimeStatus::Pending;
 // Synthetic unit-test UTC, never an actual deployment timestamp.
 *sample={1705000000u,ticks,300000};
 if(bootNet.mode=="bootstrap-time-invalid")sample->utc_seconds=0;
 if(bootNet.mode=="bootstrap-time-future")sample->sampled_monotonic_ms=ticks+1;
 if(bootNet.mode=="bootstrap-time-stale"){sample->sampled_monotonic_ms=ticks-1;sample->max_age_ms=1;}
 return RiscBootstrap::TimeStatus::Ready;
}
static RiscProvision::InputStatus bootstrapInput(void*,const char* key,void* out,uint32_t capacity,uint32_t* size){
 ++bootNet.inputReads;*size=0;if(bootNet.mode=="bootstrap-absent")return RiscProvision::InputStatus::Missing;
 const auto& data=!strcmp(key,"descriptor")?bootNet.descriptor:bootNet.profile;
 if(data.size()>capacity)return RiscProvision::InputStatus::Invalid;
 memcpy(out,data.data(),data.size());*size=data.size();return RiscProvision::InputStatus::Ready;
}
static std::vector<uint8_t> elf(const char* imported,const char* entry="app_main"){
 std::vector<uint8_t> data(1024);auto* h=reinterpret_cast<elf32_hdr_t*>(data.data());
 memcpy(h->ident,"\177ELF\1\1\1",7);h->type=3;h->machine=94;h->version=1;
 h->ehsize=sizeof(*h);h->shentsize=sizeof(elf32_shdr_t);h->shoff=64;h->shnum=5;h->shstrndx=1;
 auto* s=reinterpret_cast<elf32_shdr_t*>(data.data()+64);
 const char names[]="\0.shstrtab\0.text\0.dynsym\0.dynstr\0";
 memcpy(data.data()+300,names,sizeof(names));
 s[1].name=1;s[1].type=SHT_STRTAB;s[1].offset=300;s[1].size=sizeof(names);
 s[2].name=11;s[2].type=SHT_PROGBITS;s[2].flags=SHF_ALLOC|SHF_EXECINSTR;s[2].addr=0x100;s[2].offset=400;s[2].size=4;
 s[3].name=17;s[3].type=SHT_SYNSYM;s[3].offset=416;s[3].size=3*sizeof(elf32_sym_t);s[3].link=4;
 s[4].name=25;s[4].type=SHT_STRTAB;s[4].offset=512;s[4].size=128;
 const unsigned importOffset=unsigned(strlen(entry))+2;
 strcpy(reinterpret_cast<char*>(data.data()+513),entry);strcpy(reinterpret_cast<char*>(data.data()+512+importOffset),imported);
 auto* sym=reinterpret_cast<elf32_sym_t*>(data.data()+416);
 sym[1].name=1;sym[1].value=0x100;sym[1].shndx=2;sym[1].info=(STB_GLOBAL<<4)|STT_FUNC;
 sym[2].name=importOffset;sym[2].shndx=SHN_UNDEF;sym[2].info=(STB_GLOBAL<<4)|STT_FUNC;
 return data;
}
static std::vector<uint8_t> hiddenImport(const char* imported){
 auto data=elf("memcpy");auto* h=reinterpret_cast<elf32_hdr_t*>(data.data());h->shnum=6;
 auto* sections=reinterpret_cast<elf32_shdr_t*>(data.data()+h->shoff);
 sections[5].type=SHT_SYMTAB;sections[5].offset=640;sections[5].size=2*sizeof(elf32_sym_t);sections[5].link=4;
 strcpy(reinterpret_cast<char*>(data.data()+560),imported);
 auto* symbols=reinterpret_cast<elf32_sym_t*>(data.data()+640);
 symbols[1].name=48;symbols[1].info=(STB_GLOBAL<<4)|STT_FUNC;
 return data;
}
static std::vector<uint8_t> lifecycle(bool init,bool fini,bool global=true){
 auto data=elf("memcpy");auto* sections=reinterpret_cast<elf32_shdr_t*>(data.data()+64);
 auto* symbols=reinterpret_cast<elf32_sym_t*>(data.data()+416);unsigned count=3;
 for(auto entry:{std::pair<bool,const char*>(init,"app_module_init"),{fini,"app_module_fini"}})if(entry.first){
   unsigned at=count==3?48:68;strcpy(reinterpret_cast<char*>(data.data()+512+at),entry.second);
   symbols[count].name=at;symbols[count].value=0x100;symbols[count].shndx=2;symbols[count].info=(global?STB_GLOBAL<<4:0)|STT_FUNC;++count;
 }
 sections[3].size=count*sizeof(elf32_sym_t);return data;
}
static std::vector<uint8_t> manySymbols(){
 auto data=elf("memcpy");data.resize(4096);auto* sections=reinterpret_cast<elf32_shdr_t*>(data.data()+64);
 memcpy(data.data()+1024,data.data()+416,3*sizeof(elf32_sym_t));sections[3].offset=1024;sections[3].size=130*sizeof(elf32_sym_t);
 auto* symbols=reinterpret_cast<elf32_sym_t*>(data.data()+1024);
 for(unsigned i=3;i<130;++i)symbols[i]=symbols[2];
 return data;
}
static void firmware(unsigned bank,const char* version,const char* abi="1"){
 auto* data=flash.data()+table[bank*2].address;memset(data,0,imageSize);
 std::string marker=std::string("RISC_PAIRED_STORE_ABI:")+abi;
 memcpy(data+100,marker.c_str(),marker.size()+1);
 marker=std::string("RISC_RUNTIME_VERSION:")+version;
 // Exercise a marker split across the read chunk boundary.
 memcpy(data+4087,marker.c_str(),marker.size()+1);
 const char prefix[]="RISC_RUNTIME_VERSION:";memcpy(data+900,prefix,sizeof(prefix));
}
int main(int argc,char** argv){
 assert(argc>=2);std::string mode=argv[1];
 assert(verifyRollbackLater());
 const bool bootstrapping=mode.find("bootstrap-")==0;
 const bool provisioning=mode=="provision-seed" || mode=="provision" || mode=="provision-abort" || mode=="provision-corrupt" || mode=="provision-unknown" || mode=="provision-admission" || mode=="provision-close-retained" || bootstrapping;
 if(mode=="boot" || mode=="bad-store" || mode=="bad-layout" || mode=="restart" || mode=="restart-unknown" || provisioning){
   const bool restarting=mode=="restart" || mode=="restart-unknown";
   assert(argc==((bootstrapping||mode=="provision-seed")?5:provisioning?4:3));if(provisioning)otaState=mode=="bootstrap-pending-bank"?ESP_OTA_IMG_PENDING_VERIFY:ESP_OTA_IMG_VALID;std::ifstream input(argv[2],std::ios::binary);std::vector<uint8_t> boot((std::istreambuf_iterator<char>(input)),{});assert(boot.size()==15104);
   memcpy(flash.data(),boot.data(),boot.size());firmware(0,RISC_BUILD_VERSION);
   uint8_t fw[32],store[32];SHA256(flash.data()+0x10000,imageSize,fw);SHA256(flash.data()+0x310000,RiscUpdate::StoreBytes,store);
   auto record=RiscUpdate::makeRecord(0,imageSize,fw,store);memcpy(flash.data()+RiscUpdate::JournalOffset,&record,sizeof(record));
   if(mode=="bad-store")flash[0x310010]^=1;
   if(mode=="bad-layout")table[2].size-=4096;
   assert(RiscBankStore::prepareBoot(own,restartSafe,safe)==(mode=="boot" || restarting || provisioning));
   assert(writes==0 && confirms==0);
   if(mode=="boot"){assert(!strcmp(RiscBankStore::bootLabel(),"bootfs0"));assert(!RiscBankStore::confirmBoot());assert(RiscBankStore::exitSafe());}
   if(provisioning){
     using namespace RiscBankStore;namespace fs=std::filesystem;
     modelStageRoot=argv[3];fs::create_directories(modelStageRoot);stagingRoot=modelStageRoot.c_str();modelProvisionFiles=true;
     if(bootstrapping){
       using namespace RiscBootstrap;bootNet.mode=mode;
       auto text=[](const char* s){return std::vector<uint8_t>(s,s+strlen(s));};
       bootNet.files["boot.json"]=text(R"({"board":"board.json","default_app":"default.elf","drivers":[]})");
       bootNet.files["board.json"]=text(R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
       bootNet.files["default.elf"]=elf("memcpy");
       bootNet.profile=R"({"schema":"riscrte.provisioning","schema_version":1,"wifi":{"ssid":"test-network","password":"test-only-password"},"files":[)";
       bool first=true;for(auto& item:bootNet.files){if(!first)bootNet.profile+=',';first=false;
         uint8_t hash[32];SHA256(item.second.data(),item.second.size(),hash);std::string hex;
         for(auto byte:hash){hex+="0123456789abcdef"[byte>>4];hex+="0123456789abcdef"[byte&15];}
         bootNet.profile+="{\"path\":\""+item.first+"\",\"url\":\"https://example.test/"+item.first+"\",\"bytes\":"+std::to_string(item.second.size())+",\"sha256\":\""+hex+"\"}";
       }bootNet.profile+="]}";
       bootNet.descriptor=R"({"schema":"riscrte.bootstrap","schema_version":1,"profile_key":"profile"})";
       const std::string installed=modelStageRoot+"-installed";fs::create_directories(installed);
       for(auto& item:bootNet.files){std::ofstream f(installed+"/"+item.first,std::ios::binary);f.write(reinterpret_cast<const char*>(item.second.data()),item.second.size());}
       fs::copy_file(argv[4],installed+"/default.elf",fs::copy_options::overwrite_existing);
       if(mode=="bootstrap-invalid")bootNet.descriptor.pop_back();
       if(mode=="bootstrap-profile-invalid")bootNet.profile="{";
       if(mode=="bootstrap-unchanged"||mode=="bootstrap-match-close-retained"){
         uint8_t digest[32];SHA256(reinterpret_cast<const uint8_t*>(bootNet.profile.data()),bootNet.profile.size(),digest);
         std::ofstream f(installed+"/"+RiscProvision::StoreFiles::DigestFile,std::ios::binary);f.write(reinterpret_cast<const char*>(digest),32);
         if(mode=="bootstrap-match-close-retained")failNextNativeClose=true;
       }
       const bool hasAttemptFixture=mode=="bootstrap-rolled-back"||mode=="bootstrap-changed-profile"||mode=="bootstrap-changed-source"||mode=="bootstrap-legacy-attempt"||
         mode=="bootstrap-attempt-malformed"||mode=="bootstrap-attempt-torn"||mode=="bootstrap-attempt-bank-mismatch"||
         mode=="bootstrap-attempt-store-mismatch"||mode=="bootstrap-attempt-firmware-mismatch"||mode=="bootstrap-attempt-format"||mode=="bootstrap-attempt-read-fail";
       if(hasAttemptFixture){
         // Fresh boot after the prior desired profile failed default health.
         // Active old pair is VALID; inactive candidate is ABORTED and retains
         // the same desired profile identity in its verified stored contents.
         uint8_t digest[32];SHA256(reinterpret_cast<const uint8_t*>(bootNet.profile.data()),bootNet.profile.size(),digest);
         {std::ofstream f(modelStageRoot+"/"+RiscProvision::StoreFiles::DigestFile,std::ios::binary);f.write(reinterpret_cast<const char*>(digest),32);}
         assert(esp_vfs_spiffs_unregister("bootfs1")==ESP_OK);firmware(1,RISC_BUILD_VERSION);
         uint8_t candidateFirmware[32],candidateStore[32];SHA256(flash.data()+0x800000,imageSize,candidateFirmware);SHA256(flash.data()+0xb00000,RiscUpdate::StoreBytes,candidateStore);
         auto failedRecord=RiscUpdate::makeRecord(1,imageSize,candidateFirmware,candidateStore);memcpy(flash.data()+RiscUpdate::JournalOffset+4096,&failedRecord,sizeof(failedRecord));
         rejectedCandidate=true;
         if(mode=="bootstrap-changed-profile"){
           std::string prior=bootNet.profile;auto pos=prior.find("https://example.test/default.elf");assert(pos!=std::string::npos);
           prior.replace(pos,strlen("https://example.test/default.elf"),"https://example.test/prior-default.elf");
           SHA256(reinterpret_cast<const uint8_t*>(prior.data()),prior.size(),digest);
         }
         auto sourceRecord=record;
         if(mode=="bootstrap-changed-source"){sourceRecord.storeSha[0]^=1;sourceRecord.crc=RiscUpdate::crc32(&sourceRecord,offsetof(RiscUpdate::Record,crc));}
         auto attempt=RiscUpdate::makeAttempt(1,digest,failedRecord,sourceRecord);
         if(mode=="bootstrap-attempt-bank-mismatch")attempt.bank=0;
         if(mode=="bootstrap-attempt-store-mismatch")attempt.storeSha[0]^=1;
         if(mode=="bootstrap-attempt-firmware-mismatch")attempt.firmwareSha[0]^=1;
         if(mode=="bootstrap-attempt-format")attempt.format=2;
         attempt.crc=RiscUpdate::crc32(&attempt,offsetof(RiscUpdate::ProvisionAttempt,crc));
         if(mode=="bootstrap-attempt-malformed")attempt.crc^=1;
         if(mode!="bootstrap-legacy-attempt")memcpy(flash.data()+RiscUpdate::JournalOffset+4096+RiscUpdate::AttemptOffset,&attempt,
           mode=="bootstrap-attempt-torn"?sizeof(attempt)/2:sizeof(attempt));
         if(mode=="bootstrap-attempt-read-fail")attemptReadFailure=true;
       }
       auto hardware=admissionHardware();hardware.sleep=[](uint32_t n){vTaskDelay(n);};
       hardware.radioJoin=[](const char* ssid,const char* password){assert(!strcmp(ssid,"test-network")&&!strcmp(password,"test-only-password"));++bootNet.joins;bootNet.radio=true;return true;};
       hardware.radioState=[](uint8_t* state,int8_t* rssi){*state=bootNet.radio?2:0;*rssi=-30;return true;};
       hardware.radioAddresses=[](uint8_t* station,uint8_t* ap){memset(station,0,12);memset(ap,0,12);station[0]=192;return true;};
       hardware.radioLeave=[](){++bootNet.leaves;if(bootNet.mode=="bootstrap-radio-retained")return false;bootNet.radio=false;return true;};
       hardware.radioIdle=[](){return !bootNet.radio;};hardware.httpClient=&bootstrapHttp;
       hardware.httpIdle=[](){return !bootNet.http;};hardware.httpSafe=[](){return !bootNet.broken;};
       RiscBootstrap::Port port{hardware,nullptr,safe,{nullptr,bootstrapInput},{nullptr,bootstrapTime,[](void*){++bootNet.timeStops;return bootNet.mode!="bootstrap-time-retained";}}};
       if(mode=="bootstrap-no-time"||mode=="bootstrap-unchanged")port.time={};
       if(mode=="bootstrap-oom")risc_test_psram_fail_after=0;
       if(mode=="bootstrap-native-unsafe")operationEnabled=false;
       if(mode=="bootstrap-selection-unknown")selectFailure=true;
       if(mode=="bootstrap-time-timeout")delayScale=10000;
       if(mode=="bootstrap-attempt-write-fail")attemptWriteFault=1;
       if(mode=="bootstrap-attempt-write-torn")attemptWriteFault=2;
       if(mode=="bootstrap-attempt-write-readback")attemptWriteFault=3;
       const auto result=RiscBootstrap::run(port,installed.c_str());risc_test_psram_fail_after=-1;delayScale=1;operationEnabled=true;
       const bool selected=mode=="bootstrap-success"||mode=="bootstrap-confirmed-bank"||mode=="bootstrap-time-pending"||mode=="bootstrap-selection-unknown"||mode=="bootstrap-changed-profile"||mode=="bootstrap-changed-source"||mode=="bootstrap-legacy-attempt";
       const bool stopped=selected||mode=="bootstrap-time-retained"||mode=="bootstrap-http-retained"||mode=="bootstrap-radio-retained"||mode=="bootstrap-match-close-retained"||mode=="bootstrap-native-unsafe";
       if(mode=="bootstrap-rolled-back"){
         assert(result.reason==Reason::AttemptHeld&&!bootNet.joins&&!bootNet.opens&&!writes&&!selectorCalls);
         const auto again=RiscBootstrap::run(port,installed.c_str());assert(again.outcome==Outcome::Installed&&again.reason==Reason::AttemptHeld&&!writes);
       }
       if(hasAttemptFixture&&!selected&&mode!="bootstrap-rolled-back")assert(result.reason==Reason::HistoryUnavailable&&!bootNet.joins&&!bootNet.opens&&!writes&&!selectorCalls);
       if(attemptWriteFault){assert(result.reason==Reason::StageFailed&&!selectorCalls&&!restarts);
         RiscUpdate::ProvisionAttempt cleared{};memcpy(&cleared,flash.data()+RiscUpdate::JournalOffset+4096+RiscUpdate::AttemptOffset,sizeof(cleared));assert(RiscUpdate::emptyAttempt(cleared));}
       if(selected){RiscUpdate::Record selectedRecord{};RiscUpdate::ProvisionAttempt attempt{};
         memcpy(&selectedRecord,flash.data()+RiscUpdate::JournalOffset+4096,sizeof(selectedRecord));
         memcpy(&attempt,flash.data()+RiscUpdate::JournalOffset+4096+RiscUpdate::AttemptOffset,sizeof(attempt));
         assert(RiscUpdate::validAttempt(attempt,selectedRecord,1)&&RiscUpdate::sameAttemptSource(attempt,record));uint8_t expected[32];SHA256(reinterpret_cast<const uint8_t*>(bootNet.profile.data()),bootNet.profile.size(),expected);assert(!memcmp(expected,attempt.profileSha,32));}

       if(bootNet.polls&&mode!="bootstrap-http-retained")assert(bootNet.timeStops);
       if(mode=="bootstrap-time-retained")assert(result.reason==Reason::CleanupRetained&&bootNet.radio&&!bootNet.leaves&&!restarts);
       assert((result.outcome==Outcome::Stopped)==stopped);assert(restarts==(selected?1u:0u));
       if(mode=="bootstrap-no-time"||mode=="bootstrap-time-unavailable"||mode=="bootstrap-time-stale"||mode=="bootstrap-time-future"||mode=="bootstrap-time-invalid"||mode=="bootstrap-time-timeout")assert(result.reason==Reason::ClockUnavailable);
       if(mode=="bootstrap-unchanged")assert(result.reason==Reason::Unchanged&&!bootNet.joins&&!bootNet.opens&&!writes);
       if(mode=="bootstrap-absent"||mode=="bootstrap-invalid"||mode=="bootstrap-profile-invalid"||mode=="bootstrap-no-time"||mode=="bootstrap-oom")assert(!bootNet.joins&&!bootNet.opens&&!writes);
       if(!stopped){
         assert(!bootNet.radio&&!bootNet.http&&!provisionFiles&&!provisionToken&&!RiscBootstrap::retainedSession);
         const bool pendingBank=mode=="bootstrap-pending-bank";
         if(pendingBank)assert(result.reason==Reason::PairUnavailable&&!bootNet.inputReads&&!bootNet.joins&&!bootNet.opens&&!writes&&confirms==0);
         RiscBoot::Port fallbackPort{own,[](risc_runtime_health_v1*){return true;},[](uint32_t){},[](const char* line){assert(!strcmp(line,"BOOTSTRAP_INSTALLED_DEFAULT"));return true;}};
         if(pendingBank){fallbackPort.bindPlatforms=[](RiscBoot::Runtime& rt){return RiscBankStore::bind(rt);};fallbackPort.confirmBoot=RiscBankStore::confirmBoot;}
         RiscBoot::Runtime fallback(fallbackPort);
         assert(fallback.prepare(installed.c_str())&&fallback.run());
         if(pendingBank)assert(confirms==1&&otaState==ESP_OTA_IMG_VALID&&!provisionAvailable()); // normal health; next boot can provision

       }
       if(mode=="bootstrap-confirmed-bank")assert(bootNet.opens==3&&bootNet.joins==1&&bootNet.leaves==1);
       if(mode=="bootstrap-http-retained")assert(bootNet.radio&&bootNet.http&&bootNet.closes==1&&bootNet.leaves==0);
       if(mode=="bootstrap-radio-retained")assert(bootNet.radio&&!bootNet.http&&bootNet.leaves==1);
       if(mode=="bootstrap-match-close-retained")assert(failedAdmissionCloses==1&&!bootNet.joins&&!writes);
       uint8_t preserved[32];SHA256(flash.data()+0x310000,RiscUpdate::StoreBytes,preserved);assert(!memcmp(preserved,record.storeSha,32));
       SHA256(flash.data()+0x10000,imageSize,preserved);assert(!memcmp(preserved,record.firmwareSha,32));
       assert(hardwareCalls==0);std::cout<<"Production bootstrap setup flow: "<<mode<<" PASS\n";return 0;
     }
     if(mode=="provision-seed"){
       std::map<std::string,std::vector<uint8_t>> files;
       for(const char* name:{"board.json","boot.json","default.elf"}){
         std::ifstream in(std::string(argv[4])+"/"+name,std::ios::binary);assert(in);
         files[name]=std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),{});assert(!files[name].empty());
       }
       auto profile=std::make_unique<RiscProvision::Profile>();profile->count=files.size();size_t index=0;
       for(auto& item:files){auto& file=profile->files[index++];strcpy(file.path,item.first.c_str());file.bytes=item.second.size();SHA256(item.second.data(),item.second.size(),file.sha256);}
       uint8_t digest[32]{};digest[0]=99;uint64_t token=0;
       assert(provisionBegin(*profile,digest,admissionHardware(),nullptr,&token)==RISC_BANK_OK);
       risc_bank_status_v1 status{};status.struct_size=sizeof(status);
       for(unsigned i=0;i<5000&&!transaction->stagingStore(token);++i)assert(provisionStep(token,&status)==RISC_BANK_OK);
       assert(transaction->stagingStore(token));index=0;
       for(auto& item:files){for(size_t at=0;at<item.second.size();){uint32_t n=std::min<size_t>(4096,item.second.size()-at);assert(provisionWrite(token,index,item.second.data()+at,n)==RISC_BANK_OK);at+=n;}++index;}
       assert(provisionFinish(token)==RISC_BANK_OK);
       for(unsigned i=0;i<5000;++i){assert(transaction->status(&status));if(status.state==RISC_BANK_READY)break;assert(provisionStep(token,&status)==RISC_BANK_OK);}
       assert(status.state==RISC_BANK_READY&&hardwareCalls==0&&!selectorCalls&&!restarts);
       assert(provisionAbort(token)==RISC_BANK_OK);
       std::cout<<"Generic seed: actual graph/default ELF production native admission PASS; no execution or selection\n";
       return 0;
     }
     if(mode=="provision-admission"){
       using Files=std::map<std::string,std::vector<uint8_t>>;
       auto text=[](const char* s){return std::vector<uint8_t>(s,s+strlen(s));};
       const auto ordinaryBoot=text(R"({"board":"board.json","default_app":"default.elf","drivers":[{"manifest":"driver.json"}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"test.probe","api":1,"instance_id":0}]}]})");
       Files base{{"board.json",text(R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})")},
         {"boot.json",ordinaryBoot},{"default.elf",elf("memcpy")},{"driver.elf",elf("memcpy","t5_driver_get")},
         {"driver.json",text(R"({"type":"driver","id":"provision-probe","version":"1.0.0","driver_abi":2,"architecture":"xtensa-esp32s3","file_name":"driver.elf","requires":[{"capability":"platform.clock","api":1},{"capability":"platform.bank-store","api":1}],"provides":[{"capability":"test.probe","api":1}]})")},
         {"app.json",text(R"({"type":"application","id":"provision-default","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"test.probe","api":1}]})")}};
       static const RiscBoot::KeyValueBackend kv{nullptr,
         [](void*,uint32_t,const char*,void*,uint32_t,uint32_t*){++hardwareCalls;return int32_t(RISC_KEY_VALUE_IO);},
         [](void*,uint32_t,const char*,const void*,uint32_t){++hardwareCalls;return int32_t(RISC_KEY_VALUE_IO);},RISC_KEY_VALUE_V2_BLOB_MAX};
       for(unsigned scenario=0;scenario<18;++scenario){Files files=base;bool expected=scenario==0||scenario==10||scenario==13;
         auto hardware=admissionHardware();const RiscBoot::KeyValueBackend* backend=nullptr;
         if(scenario==1)files.erase("driver.elf");
         if(scenario==2)files["driver.elf"]=elf("memcpy");
         if(scenario==3)files["default.elf"]=elf("memcpy","t5_driver_get");
         if(scenario==4)files["driver.elf"]=elf("esp_partition_write","t5_driver_get");
         if(scenario==5)files["default.elf"]=hiddenImport("esp_restart");
         if(scenario==6)reinterpret_cast<elf32_hdr_t*>(files["default.elf"].data())->machine=3;
         if(scenario==7)files["board.json"]=text(R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[{"instance_id":1,"kind":"spi","controller_namespace":"esp32.peripheral","controller":2,"frequency_hz":10000000,"mode":0,"pins":{"sclk":33,"mosi":5,"miso":6}}],"devices":[]})");
         if(scenario==8)files["boot.json"]=text(R"({"board":"board.json","default_app":"absent.elf","drivers":[]})");
         if(scenario==9)files["extra.elf"]={1,2,3,4};
         if(scenario==10)files["extra.elf"]=elf("memcpy","t5_driver_get");
         if(scenario==11)hardware.spiTransfer=nullptr;
         if(scenario==12||scenario==13){
           files["app.json"]=text(R"({"type":"application","id":"provision-default","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"storage.key-value","api":2}]})");
           files["boot.json"]=text(R"({"board":"board.json","default_app":"default.elf","drivers":[],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"storage.key-value","api":2,"instance_id":1}]}]})");
           if(scenario==13)backend=&kv;
         }
         if(scenario==14)unavailableImport="memcpy";
         auto profile=std::make_unique<RiscProvision::Profile>();profile->count=files.size();size_t index=0;
         for(auto& item:files){auto& file=profile->files[index++];strcpy(file.path,item.first.c_str());file.bytes=item.second.size();SHA256(item.second.data(),item.second.size(),file.sha256);}
         uint8_t digest[32]{};digest[0]=uint8_t(scenario+1);uint64_t token=0;
         assert(provisionBegin(*profile,digest,hardware,backend,&token)==RISC_BANK_OK);
         risc_bank_status_v1 status{};status.struct_size=sizeof(status);
         for(unsigned i=0;i<5000&&!transaction->stagingStore(token);++i)assert(provisionStep(token,&status)==RISC_BANK_OK);
         assert(transaction->stagingStore(token));index=0;
         for(auto& item:files){for(size_t at=0;at<item.second.size();){uint32_t n=std::min<size_t>(4096,item.second.size()-at);assert(provisionWrite(token,index,item.second.data()+at,n)==RISC_BANK_OK);at+=n;}++index;}
         if(scenario>=15)risc_test_psram_fail_after=int(scenario-15);
         const int32_t result=provisionFinish(token);risc_test_psram_fail_after=-1;
         assert((result==RISC_BANK_OK)==expected);unavailableImport=nullptr;
         if(expected){for(unsigned i=0;i<5000;++i){assert(transaction->status(&status));if(status.state==RISC_BANK_READY)break;assert(provisionStep(token,&status)==RISC_BANK_OK);}assert(status.state==RISC_BANK_READY);}
         else {assert(provisionActivate(token)==RISC_BANK_STATE);RiscUpdate::Record target{};memcpy(&target,flash.data()+RiscUpdate::JournalOffset+4096,sizeof(target));assert(!RiscUpdate::validRecord(target,1));}
         assert(provisionAbort(token)==RISC_BANK_OK && !candidateCpu && !provisionState && !provisionFiles && hardwareCalls==0);
         uint8_t preserved[32];SHA256(flash.data()+0x310000,RiscUpdate::StoreBytes,preserved);assert(!memcmp(preserved,record.storeSha,32));
       }
       std::cout<<"Production native full-graph/image admission:18 role/import/board/backend/OOM scenarios PASS; zero provider or hardware execution\n";
       return 0;
     }
     auto profile=std::make_unique<RiscProvision::Profile>();profile->count=3;
     const char* names[]={"boot.json","board.json","default.elf"};
     const char* json[]={R"({"board":"board.json","default_app":"default.elf","drivers":[]})",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})"};
     std::vector<uint8_t> payload[3];for(unsigned i=0;i<2;++i)payload[i].assign(json[i],json[i]+strlen(json[i]));payload[2]=elf("memcpy");
     for(unsigned i=0;i<3;++i){strcpy(profile->files[i].path,names[i]);profile->files[i].bytes=payload[i].size();SHA256(payload[i].data(),payload[i].size(),profile->files[i].sha256);}
     uint8_t digest[32]{};digest[0]=0x42;uint64_t token=0;
     auto hardware=admissionHardware();
     assert(provisionBegin(*profile,digest,RiscCpu::Hardware{},nullptr,&token)==RISC_BANK_UNAVAILABLE && !token && writes==0);
     ownerEnabled=false;assert(provisionBegin(*profile,digest,hardware,nullptr,&token)==RISC_BANK_UNAVAILABLE && !token);ownerEnabled=true;
     assert(provisionBegin(*profile,digest,hardware,nullptr,&token)==RISC_BANK_OK && token);
     RiscBoot::Runtime blocked({own,nullptr,nullptr,nullptr});assert(!bind(blocked));
     risc_bank_status_v1 status{};status.struct_size=sizeof(status);
     for(unsigned i=0;i<5000 && !transaction->stagingStore(token);++i)assert(provisionStep(token,&status)==RISC_BANK_OK);
     assert(transaction->stagingStore(token));assert(provisionWrite(token+1,0,payload[0].data(),1)==RISC_BANK_STATE);
     assert(provisionActivate(token)==RISC_BANK_STATE);
     for(size_t i=0;i<3;++i)for(size_t at=0;at<payload[i].size();){size_t n=std::min<size_t>(4096,payload[i].size()-at);
       assert(provisionWrite(token,i,payload[i].data()+at,n)==RISC_BANK_OK);at+=n;}
     if(mode=="provision-abort"){
       assert(provisionAbort(token)==RISC_BANK_OK && exitSafe());assert(!provisionFiles && !provisionProfile && !provisionToken);
       assert(provisionWrite(token,0,payload[0].data(),1)==RISC_BANK_STATE);assert(bind(blocked));
     }else if(mode=="provision-close-retained"){
       failAdmissionClose=true;assert(provisionFinish(token)==RISC_BANK_INTEGRITY);
       assert(provisionReadRetained && !candidateCpu && failedAdmissionCloses==1);
       assert(provisionAbort(token)==RISC_BANK_RETAINED && failedAdmissionCloses==1);
       assert(!bind(blocked) && !exitSafe());assert(!provisionRestart(token) && restarts==0);
       assert(provisionActivate(token)==RISC_BANK_STATE);
     }else{
       assert(provisionFinish(token)==RISC_BANK_OK);
       if(mode=="provision-corrupt")flash[0xb00001]^=1;
       int32_t result=0;for(unsigned i=0;i<5000;++i){assert(transaction->status(&status));if(status.state==RISC_BANK_READY)break;result=provisionStep(token,&status);if(result)break;}
       if(mode=="provision-corrupt"){assert(result==RISC_BANK_INTEGRITY);assert(provisionActivate(token)==RISC_BANK_STATE);assert(provisionAbort(token)==RISC_BANK_OK);}
       else {assert(status.state==RISC_BANK_READY);selectFailure=mode=="provision-unknown";
         assert(provisionActivate(token)==(selectFailure?RISC_BANK_RETAINED:RISC_BANK_OK));assert(provisionAbort(token)==RISC_BANK_STATE);
         restartEnabled=false;assert(!provisionRestart(token) && restarts==0);restartEnabled=true;
         assert(!provisionRestart(token) && restarts==1);}
     }
     assert(hardwareCalls==0 && !candidateCpu);
     uint8_t preserved[32];SHA256(flash.data()+0x310000,RiscUpdate::StoreBytes,preserved);assert(!memcmp(preserved,record.storeSha,32));
     SHA256(flash.data()+0x10000,imageSize,preserved);assert(!memcmp(preserved,record.firmwareSha,32));
   }
   if(restarting){
     using namespace RiscBankStore;
     firmware(1,"0.1.12");std::vector<uint8_t> payload(flash.begin()+0x800000,flash.begin()+0x800000+imageSize);
     risc_bank_image_v1 image{};image.struct_size=sizeof(image);image.size=payload.size();image.store_abi=1;
     memcpy(image.active_store_sha256,record.storeSha,32);SHA256(payload.data(),payload.size(),image.sha256);
     replacingFirmware=true;uint64_t token=0;assert(transaction->begin(false,image,&token)==RISC_BANK_OK);
     assert(!api.restart(nullptr,token) && restarts==0);
     auto advance=[&](uint32_t destination){
       risc_bank_status_v1 status{};status.struct_size=sizeof(status);
       for(unsigned steps=0;steps<4096;++steps){assert(transaction->status(&status));if(status.state==destination)return;assert(transaction->step(token,&status)==RISC_BANK_OK);}
       assert(false);
     };
     advance(RISC_BANK_RECEIVING);
     for(uint32_t at=0;at<payload.size();at+=4096)assert(transaction->write(token,payload.data()+at,std::min(size_t(4096),payload.size()-at))==RISC_BANK_OK);
     assert(transaction->finish(token)==RISC_BANK_OK);advance(RISC_BANK_READY);
     selectFailure=mode=="restart-unknown";
     assert(transaction->activate(token)==(selectFailure?RISC_BANK_RETAINED:RISC_BANK_OK));
     RiscUpdate::ProvisionAttempt ordinaryAttempt{};memcpy(&ordinaryAttempt,flash.data()+RiscUpdate::JournalOffset+4096+RiscUpdate::AttemptOffset,sizeof(ordinaryAttempt));
     assert(RiscUpdate::emptyAttempt(ordinaryAttempt)&&selectorCalls==1); // no provisioning policy added to ordinary updates
     assert(exitSafe()!=selectFailure);assert(!api.restart(nullptr,token+1) && restarts==0);
     restartEnabled=false;assert(!api.restart(nullptr,token) && restarts==0);restartEnabled=true;
     ownerEnabled=false;assert(!api.restart(nullptr,token) && restarts==0);ownerEnabled=true;
     operationEnabled=false;assert(!api.restart(nullptr,token) && restarts==0);operationEnabled=true;
     assert(!api.restart(nullptr,token) && restarts==1); // Fake restart returns; actual ESP restart does not.
     assert(transaction->abort(token)==RISC_BANK_STATE && confirms==0);
   }else if(!provisioning){
     RiscBankStore::rejectBoot();assert(rollbacks==(mode=="bad-layout"?0u:1u) && confirms==0);
   }
 }else if(mode=="unknown-loader"){
   assert(!RiscBankStore::prepareBoot(own,safe,safe));assert(writes==0 && confirms==0);
 }else{
   using namespace RiscBankStore;
   isOwner=own;operationIsSafe=safe;activeBank=0;
   scratch=new Scratch;
   for(unsigned b=0;b<2;++b)for(unsigned r=0;r<2;++r)parts[b][r]=&table[b*2+r];
   firmware(1,"0.1.12");replacingFirmware=true;assert(validateFirmware(nullptr,1,imageSize));
   for(const char* v:{RISC_BUILD_VERSION,"0.1.10","00.1.12","4294967296.0.0","1.0","1.2.3-rc"}){firmware(1,v);assert(!validateFirmware(nullptr,1,imageSize));}
   firmware(1,"0.1.12","2");assert(!validateFirmware(nullptr,1,imageSize));
   firmware(1,RISC_BUILD_VERSION);replacingFirmware=false;assert(validateFirmware(nullptr,1,imageSize));
   assert(!validateFirmware(nullptr,1,imageSize-1));
   uint8_t byte=0;assert(!write(nullptr,0,0,0,&byte,1));assert(!erase(nullptr,0,1,0));
   assert(!write(nullptr,1,1,RiscUpdate::StoreBytes,&byte,1));
   ownerEnabled=false;assert(!write(nullptr,1,0,0,&byte,1));ownerEnabled=true;
   operationEnabled=false;
   assert(!write(nullptr,1,0,0,&byte,1) && !erase(nullptr,1,1,0) && !read(nullptr,1,0,0,&byte,1));
   assert(!validateFirmware(nullptr,1,imageSize));operationEnabled=true;
   assert(writes==0);
   assert(allowedImport("memcpy") && allowedImport("risc_runtime_get_api"));
   assert(!allowedImport("esp_partition_write") && !allowedImport("fopen") && !allowedImport("esp_restart"));
   auto goodElf=elf("memcpy");assert(admitElf(goodElf.data(),goodElf.size()));
   auto goodTables=hiddenImport("memcpy");assert(admitElf(goodTables.data(),goodTables.size()));
   unavailableImport="memcpy";assert(!admitElf(goodElf.data(),goodElf.size()));assert(!admitElf(goodTables.data(),goodTables.size()));unavailableImport=nullptr;
   for(bool init:{false,true})for(bool fini:{false,true}){auto hooks=lifecycle(init,fini);assert(admitElf(hooks.data(),hooks.size())==(init==fini));}
   auto localHooks=lifecycle(true,true,false);assert(!admitElf(localHooks.data(),localHooks.size()));
   for(const char* import:{"esp_partition_write","fopen","xTaskCreate","esp_restart"}){
     auto badElf=elf(import);assert(!admitElf(badElf.data(),badElf.size()));
     badElf=hiddenImport(import);assert(esp_elf_validate_file(badElf.data(),badElf.size()));assert(!admitElf(badElf.data(),badElf.size()));}
   auto localMain=elf("memcpy");reinterpret_cast<elf32_sym_t*>(localMain.data()+416)[1].info=STT_FUNC;
   assert(esp_elf_validate_file(localMain.data(),localMain.size()));assert(!admitElf(localMain.data(),localMain.size()));
   auto unnamedImport=elf("memcpy");reinterpret_cast<elf32_sym_t*>(unnamedImport.data()+416)[2].name=0;
   assert(esp_elf_validate_file(unnamedImport.data(),unnamedImport.size()));assert(!admitElf(unnamedImport.data(),unnamedImport.size()));
   auto duplicateMain=elf("memcpy");reinterpret_cast<elf32_shdr_t*>(duplicateMain.data()+64)[3].size=4*sizeof(elf32_sym_t);
   auto* duplicateSymbols=reinterpret_cast<elf32_sym_t*>(duplicateMain.data()+416);duplicateSymbols[3]=duplicateSymbols[1];
   assert(esp_elf_validate_file(duplicateMain.data(),duplicateMain.size()));assert(!admitElf(duplicateMain.data(),duplicateMain.size()));
   auto staticMain=hiddenImport("app_main");reinterpret_cast<elf32_sym_t*>(staticMain.data()+416)[1].name=0;
   auto* staticSymbols=reinterpret_cast<elf32_sym_t*>(staticMain.data()+640);staticSymbols[1].value=0x100;staticSymbols[1].shndx=2;
   assert(esp_elf_validate_file(staticMain.data(),staticMain.size()));assert(!admitElf(staticMain.data(),staticMain.size()));
   auto many=manySymbols();unsigned before=delays;assert(admitElf(many.data(),many.size()) && delays>=before+2);
   unsafeAfterDelay=delays+2;assert(!admitElf(many.data(),many.size()));operationEnabled=true;unsafeAfterDelay=0;
   delayScale=16000;assert(!admitElf(many.data(),many.size()));delayScale=1;
   goodElf[0]=0;assert(!admitElf(goodElf.data(),goodElf.size()));
   delete scratch;scratch=nullptr;
 }
 std::cout<<"Production native bank adapter: "<<mode<<" PASS\n";
}
