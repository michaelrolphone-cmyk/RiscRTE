// Execute the real native bank adapter against fake flash/IDF I/O. Format/hash,
// marker, partition admission and boot-state logic remain production code.
#include "ports/esp32s3/NativeBankStore.cpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
#include <filesystem>
#include <map>
#include <memory>
static const char* unavailableImport=nullptr;
static bool modelProvisionFiles=false;
static std::string modelStageRoot;
extern "C" uintptr_t elf_find_sym_default(const char* name){return unavailableImport && !strcmp(name,unavailableImport)?0:1;}
static std::vector<uint8_t> flash(0x1000000,0xff);
static uint32_t ticks=1,active=0,imageSize=8192,writes=0,rollbacks=0,confirms=0,restarts=0,delayScale=1,delays=0,unsafeAfterDelay=0;
static bool ownerEnabled=true,rollbackPossible=true,operationEnabled=true,restartEnabled=true,selectFailure=false;
static esp_ota_img_states_t otaState=ESP_OTA_IMG_PENDING_VERIFY;
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
esp_err_t esp_partition_read(const esp_partition_t* p,size_t off,void* out,size_t n){if(!p || off+n>p->size)return -1;memcpy(out,flash.data()+p->address+off,n);return 0;}
esp_err_t esp_partition_write(const esp_partition_t* p,size_t off,const void* in,size_t n){if(!p || off+n>p->size)return -1;++writes;memcpy(flash.data()+p->address+off,in,n);return 0;}
esp_err_t esp_partition_erase_range(const esp_partition_t* p,size_t off,size_t n){if(!p || off+n>p->size)return -1;++writes;memset(flash.data()+p->address+off,0xff,n);return 0;}
const esp_partition_t* esp_ota_get_running_partition(){return &table[active*2];}
esp_err_t esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t* s){*s=otaState;return 0;}
const esp_app_desc_t* esp_ota_get_app_description(){static esp_app_desc_t d{};strcpy(d.project_name,"arduino-lib-builder");return &d;}
esp_err_t esp_ota_get_partition_description(const esp_partition_t*,esp_app_desc_t* out){*out=*esp_ota_get_app_description();return 0;}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*){++writes;return selectFailure?-1:0;}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(){++confirms;return 0;}
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
static std::vector<uint8_t> elf(const char* imported){
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
 strcpy(reinterpret_cast<char*>(data.data()+513),"app_main");strcpy(reinterpret_cast<char*>(data.data()+522),imported);
 auto* sym=reinterpret_cast<elf32_sym_t*>(data.data()+416);
 sym[1].name=1;sym[1].value=0x100;sym[1].shndx=2;sym[1].info=(STB_GLOBAL<<4)|STT_FUNC;
 sym[2].name=10;sym[2].shndx=SHN_UNDEF;sym[2].info=(STB_GLOBAL<<4)|STT_FUNC;
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
 const bool provisioning=mode=="provision" || mode=="provision-abort" || mode=="provision-corrupt" || mode=="provision-unknown";
 if(mode=="boot" || mode=="bad-store" || mode=="bad-layout" || mode=="restart" || mode=="restart-unknown" || provisioning){
   const bool restarting=mode=="restart" || mode=="restart-unknown";
   assert(argc==(provisioning?4:3));if(provisioning)otaState=ESP_OTA_IMG_VALID;std::ifstream input(argv[2],std::ios::binary);std::vector<uint8_t> boot((std::istreambuf_iterator<char>(input)),{});assert(boot.size()==15104);
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
     auto profile=std::make_unique<RiscProvision::Profile>();profile->count=3;
     const char* names[]={"boot.json","board.json","default.elf"};
     const char* json[]={R"({"board":"board.json","default_app":"default.elf","drivers":[]})",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})"};
     std::vector<uint8_t> payload[3];for(unsigned i=0;i<2;++i)payload[i].assign(json[i],json[i]+strlen(json[i]));payload[2]=elf("memcpy");
     for(unsigned i=0;i<3;++i){strcpy(profile->files[i].path,names[i]);profile->files[i].bytes=payload[i].size();SHA256(payload[i].data(),payload[i].size(),profile->files[i].sha256);}
     uint8_t digest[32]{};digest[0]=0x42;uint64_t token=0;
     auto admission=[](const char* root,const RiscProvision::Profile& p){
       auto rt=std::make_unique<RiscBoot::Runtime>(RiscBoot::Port{own,nullptr,nullptr,nullptr});if(!rt->prepare(root))return false;
       for(size_t i=0;i<p.count;++i){const auto& f=p.files[i];size_t n=strlen(f.path);if(n<4||strcmp(f.path+n-4,".elf"))continue;
         std::ifstream in(std::string(root)+"/"+f.path,std::ios::binary);std::vector<uint8_t> b{std::istreambuf_iterator<char>(in),{}};if(!admitElf(b.data(),b.size()))return false;}
       return true;
     };
     assert(provisionBegin(*profile,digest,nullptr,&token)==RISC_BANK_UNAVAILABLE && !token && writes==0);
     ownerEnabled=false;assert(provisionBegin(*profile,digest,admission,&token)==RISC_BANK_UNAVAILABLE && !token);ownerEnabled=true;
     assert(provisionBegin(*profile,digest,admission,&token)==RISC_BANK_OK && token);
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
