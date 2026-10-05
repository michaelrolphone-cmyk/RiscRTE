#include "NativeBankStore.h"
#ifdef RISC_PAIRED_BANKS
#include "bootstrap/Runtime.h"
#include "runtime/update/PairedBank.h"
#include "runtime/update/Version.h"
#include "runtime/update/StoreAudit.h"
#include "runtime/provisioning/StoreFiles.h"
#include "CpuPort.h"
#include "NativeBoard.h"
#include <Arduino.h>
#include <RiscBuildIdentity.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_spiffs.h>
#include <esp_image_format.h>
#include <mbedtls/sha256.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <private/elf_types.h>
#include <private/elf_symbol.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <esp_flash.h>
extern "C" bool esp_elf_validate_file(const uint8_t*,size_t);
/* This literal is inspected in staged native images. It states the generic
 * paired bootstrap-store contract, independently of product/release URLs. */
extern "C" __attribute__((used)) const char risc_paired_store_abi[]="RISC_PAIRED_STORE_ABI:1";
extern "C" __attribute__((used)) const char risc_runtime_update_version[]="RISC_RUNTIME_VERSION:" RISC_BUILD_VERSION;
// Arduino's weak default confirms before setup(), which is too early.
extern "C" bool verifyRollbackLater(void){return true;}
namespace RiscBankStore {
namespace {
using namespace RiscUpdate;
const esp_partition_t* parts[2][2]{};
const esp_partition_t* journal=nullptr;
const char* labels[2]={"bootfs0","bootfs1"};
RiscBoot::Runtime* runtime=nullptr;
bool (*isOwner)()=nullptr;bool (*restartIsSafe)()=nullptr;
bool (*operationIsSafe)()=nullptr;
bool prepared=false,mounted=false,pending=false,confirmed=false;
bool rollbackTrusted=false;
bool replacingFirmware=false;
unsigned activeBank=0;
FILE* appFile=nullptr;
struct Scratch {
  Record verifiedActiveRecord{};
  RiscBoot::Runtime::UpdateApp appPaths{};
  char manifest[RISC_BANK_MANIFEST_MAX]{};
  uint32_t manifestSize=0,appSize=0;
  uint8_t appDigest[32]{};
  mbedtls_sha256_context hashContext{};
  uint8_t buffer[4096+96]{};
};
Scratch* scratch=nullptr;
Transaction* transaction=nullptr;
RiscProvision::StoreFiles* provisionFiles=nullptr;
const RiscProvision::Profile* provisionProfile=nullptr;
struct ProvisionState {
  RiscProvision::StoreFiles files;
  RiscCpu::Hardware hardware;
  const RiscBoot::KeyValueBackend* keyValue;
  ProvisionState(RiscProvision::FileBackend io,const RiscCpu::Hardware& h,const RiscBoot::KeyValueBackend* k):files(io),hardware(h),keyValue(k){}
};
ProvisionState* provisionState=nullptr;
RiscCpu::Port* candidateCpu=nullptr;
bool provisionReadRetained=false;
bool bindProvisioningCandidate(RiscBoot::Runtime&);
uint8_t provisionDigest[32]{};
uint64_t provisionToken=0;
const char* stagingRoot="/updatefs";
constexpr uint32_t ProvisionCapacity=(StoreBytes/4)*3;
#ifndef CONFIG_SPIFFS_OBJ_NAME_LEN
#define CONFIG_SPIFFS_OBJ_NAME_LEN 32
#endif
bool owner(){return isOwner && isOwner();}
bool operationSafe(){return owner() && operationIsSafe && operationIsSafe();}
uint32_t now(void*){return millis();}
bool hashBegin(void*){return mbedtls_sha256_starts_ret(&scratch->hashContext,0)==0;}
bool hashAdd(void*,const void* p,uint32_t n){return mbedtls_sha256_update_ret(&scratch->hashContext,static_cast<const uint8_t*>(p),n)==0;}
bool hashEnd(void*,uint8_t* digest){return mbedtls_sha256_finish_ret(&scratch->hashContext,digest)==0;}
bool range(unsigned bank,unsigned region,uint32_t off,uint32_t n){return bank<2 && region<2 && parts[bank][region] && n && n<=4096 && off<=parts[bank][region]->size && n<=parts[bank][region]->size-off;}
bool read(void*,unsigned b,unsigned r,uint32_t o,void* p,uint32_t n){return operationSafe() && p && range(b,r,o,n) && esp_partition_read(parts[b][r],o,p,n)==ESP_OK;}
bool erase(void*,unsigned b,unsigned r,uint32_t o){return operationSafe() && b!=activeBank && !(o%4096) && range(b,r,o,4096) && esp_partition_erase_range(parts[b][r],o,4096)==ESP_OK;}
bool write(void*,unsigned b,unsigned r,uint32_t o,const void* p,uint32_t n){return operationSafe() && b!=activeBank && p && range(b,r,o,n) && esp_partition_write(parts[b][r],o,p,n)==ESP_OK;}
bool invalidate(void*,unsigned b){return operationSafe() && b<2 && b!=activeBank && journal && esp_partition_erase_range(journal,b*4096,4096)==ESP_OK;}
bool record(void*,unsigned b,const Record& value){
  if(!operationSafe() || b==activeBank || !validRecord(value,b) || !journal)return false;
  if(esp_partition_write(journal,b*4096,&value,sizeof(value))!=ESP_OK)return false;
  Record check{};return esp_partition_read(journal,b*4096,&check,sizeof(check))==ESP_OK && !memcmp(&value,&check,sizeof(check));
}
bool cleanup(void*){
  if(!owner() || provisionReadRetained)return false;
  if(provisionFiles && !provisionFiles->close())return false;
  if(appFile){FILE* f=appFile;appFile=nullptr;if(fclose(f)!=0)return false;}
  if(mounted){if(esp_vfs_spiffs_unregister(labels[1-activeBank])!=ESP_OK)return false;mounted=false;}
  return true;
}
bool absolute(char* out,size_t size,const char* relative){return RiscBoot::path("/updatefs",relative,out,size);}
bool mountInactive(unsigned b){
  if(!operationSafe() || b==activeBank || mounted || appFile)return false;
  esp_vfs_spiffs_conf_t config{};config.base_path=stagingRoot;config.partition_label=labels[b];config.max_files=4;config.format_if_mount_failed=false;
  if(esp_vfs_spiffs_register(&config)!=ESP_OK)return false;
  mounted=true;return true;
}
bool openWholeStore(void*,unsigned b){
  return provisionFiles && provisionProfile && !runtime && confirmed && !pending && mountInactive(b) &&
    provisionFiles->begin(stagingRoot,*provisionProfile,provisionDigest,ProvisionCapacity);
}
bool finishWholeStore(void*,unsigned b,uint8_t* digest){
  if(!operationSafe() || !provisionFiles || !provisionProfile || runtime || b==activeBank || !mounted ||
     !provisionFiles->finish() || !cleanup(nullptr) || !hashBegin(nullptr))return false;
  // Hash the closed, persisted partition after all file writes and filesystem
  // GC. PairedBank independently compares a second raw readback before READY.
  uint32_t started=millis();
  for(uint32_t at=0;at<StoreBytes;){uint32_t n=std::min(4096u,StoreBytes-at);
    if(!read(nullptr,b,1,at,scratch->buffer,n)||!hashAdd(nullptr,scratch->buffer,n))return false;
    at+=n;vTaskDelay(1);if(!operationSafe()||uint32_t(millis()-started)>30000u)return false;}
  return hashEnd(nullptr,digest);
}
bool openApp(void*,unsigned b){
  if(!operationSafe() || b==activeBank || mounted || appFile || !scratch->manifestSize)return false;
  esp_vfs_spiffs_conf_t config{};config.base_path="/updatefs";config.partition_label=labels[b];config.max_files=2;config.format_if_mount_failed=false;
  if(esp_vfs_spiffs_register(&config)!=ESP_OK)return false;
  mounted=true;
  char filename[256];if(!absolute(filename,sizeof(filename),scratch->appPaths.elf))return false;
  appFile=fopen(filename,"wb");return appFile!=nullptr;
}
bool writeApp(void*,const void* data,uint32_t n){return operationSafe() && appFile && fwrite(data,1,n,appFile)==n;}
// An updater cannot smuggle firmware/peripheral imports through a native ELF.
// This is the ordinary loader's side-effect-free public libc + runtime surface.
bool allowedImport(const char* name){
  static const char* const names[]={"risc_runtime_get_api","strerror","memset","memcpy","memmove","memcmp","memchr","strlen","strcpy","strncpy","strcmp","strncmp","strchr","strrchr","strstr","strtod","strtol","strcspn","strncat","snprintf","malloc","calloc","realloc","free","clock_gettime","strftime","__errno","__getreent","__locale_ctype_ptr","_ctype_","__ltdf2","__fixunsdfsi","__gtdf2","__floatunsidf","__divdf3","getopt_long","optind","opterr","optarg","optopt"};
  for(const char* n:names)if(!strcmp(n,name))return true;
  return false;
}
enum class ElfRole {Application,Driver,Either};
bool admitElf(const uint8_t* bytes,size_t n,ElfRole role=ElfRole::Application){
  if(!operationSafe() || n>RISC_BANK_APP_MAX)return false;
  const uint32_t started=millis();unsigned visited=0;
  if(!esp_elf_validate_file(bytes,n))return false;
  // Structural validation is the existing synchronous loader check. Bound its
  // observed duration, then cooperate throughout the additional policy scan.
  vTaskDelay(1);if(!operationSafe() || uint32_t(millis()-started)>30000u)return false;
  const auto* h=reinterpret_cast<const elf32_hdr_t*>(bytes);
  const auto* sections=reinterpret_cast<const elf32_shdr_t*>(bytes+h->shoff);
  unsigned main=0,init=0,fini=0,driver=0;
  const char* sectionNames=reinterpret_cast<const char*>(bytes+sections[h->shstrndx].offset);
  // RELA may reference either symbol-table kind. Audit both, including tables
  // that have no current relocations. Entry points must be actual exports from
  // .dynsym, exactly as the production loader's dlsym path requires.
  for(unsigned i=0;i<h->shnum;++i)if(sections[i].type==SHT_SYNSYM || sections[i].type==SHT_SYMTAB){
    const auto& s=sections[i];const char* strings=reinterpret_cast<const char*>(bytes+sections[s.link].offset);
    const bool exports=s.type==SHT_SYNSYM && !strcmp(sectionNames+s.name,ELF_DYNSYM);
    const auto* symbols=reinterpret_cast<const elf32_sym_t*>(bytes+s.offset);
    for(unsigned j=0;j<s.size/sizeof(*symbols);++j){
      if(++visited>RISC_BANK_APP_MAX/sizeof(*symbols))return false;
      if(!(visited%128)){vTaskDelay(1);if(!operationSafe() || uint32_t(millis()-started)>30000u)return false;}
      const auto& sym=symbols[j];const char* name=strings+sym.name;
      if(sym.shndx==SHN_UNDEF){
        const elf32_sym_t empty{};if(!j && !memcmp(&sym,&empty,sizeof(sym)))continue;
        if((ELF_ST_BIND(sym.info)!=STB_GLOBAL && ELF_ST_BIND(sym.info)!=STB_WEAK) || !*name ||
           !allowedImport(name) || !elf_find_sym_default(name))return false;
        continue;
      }
      if(!exports)continue;
      unsigned* entry=nullptr;
      if(!strcmp(name,"app_main"))entry=&main;
      if(role!=ElfRole::Application && !strcmp(name,"t5_driver_get"))entry=&driver;
      if(!strcmp(name,"app_module_init"))entry=&init;
      if(!strcmp(name,"app_module_fini"))entry=&fini;
      if(entry){
        if(ELF_ST_TYPE(sym.info)!=STT_FUNC || ELF_ST_BIND(sym.info)!=STB_GLOBAL || ++*entry!=1 || sym.shndx>=h->shnum)return false;
        const auto& text=sections[sym.shndx];
        if(strcmp(sectionNames+text.name,ELF_TEXT) || !(text.flags&SHF_EXECINSTR) || sym.value<text.addr || uint64_t(sym.value)>=uint64_t(text.addr)+text.size)return false;
      }
    }
  }
  const bool application=main==1 && init==fini;
  return role==ElfRole::Application?application:role==ElfRole::Driver?driver==1:application || driver==1;
}
struct AdmissionImages {const char* root;uint32_t started;bool seen[RiscProvision::MaxFiles]{};};
bool inspectProvisionedImage(AdmissionImages& context,const char* path,ElfRole role){
  if(!operationSafe() || !provisionState || !provisionProfile || uint32_t(millis()-context.started)>=30000u)return false;
  const size_t rootBytes=strlen(context.root);
  if(strncmp(path,context.root,rootBytes) || path[rootBytes]!='/')return false;
  const char* relative=path+rootBytes+1;size_t index=0;
  for(;index<provisionProfile->count;++index)if(!strcmp(relative,provisionProfile->files[index].path))break;
  if(index==provisionProfile->count)return false;
  const auto& file=provisionProfile->files[index];
  if(!file.bytes || file.bytes>RISC_BANK_APP_MAX)return false;
  auto* bytes=static_cast<uint8_t*>(heap_caps_malloc(file.bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!bytes)return false;
  FILE* input=fopen(path,"rb");bool ok=input && hashBegin(nullptr);uint32_t at=0;
  while(ok && at<file.bytes){uint32_t n=std::min(4096u,file.bytes-at);
    ok=operationSafe() && fread(bytes+at,1,n,input)==n && hashAdd(nullptr,bytes+at,n);at+=n;
    vTaskDelay(1);if(uint32_t(millis()-context.started)>=30000u)ok=false;}
  uint8_t digest[32];ok=ok && fgetc(input)==EOF && !ferror(input) && hashEnd(nullptr,digest) && !memcmp(digest,file.sha256,32);
  if(input && fclose(input)!=0){provisionReadRetained=true;ok=false;}
  ok=ok && admitElf(bytes,file.bytes,role) && operationSafe() && uint32_t(millis()-context.started)<30000u;
  free(bytes);if(ok)context.seen[index]=true;return ok;
}
bool admitProvisionedStore(const char* root,const RiscProvision::Profile& profile){
  if(!operationSafe() || !provisionState || &profile!=provisionProfile || candidateCpu || runtime || provisionReadRetained)return false;
  void* cpuMemory=heap_caps_malloc(sizeof(RiscCpu::Port),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!cpuMemory)return false;
  void* runtimeMemory=heap_caps_malloc(sizeof(RiscBoot::Runtime),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!runtimeMemory){free(cpuMemory);return false;}
  candidateCpu=new(cpuMemory) RiscCpu::Port(provisionState->hardware);
  auto* candidate=new(runtimeMemory) RiscBoot::Runtime({isOwner,nullptr,nullptr,nullptr,bindProvisioningCandidate,provisionState->keyValue});
  RiscCpu::reserveNativePins(candidate->board());AdmissionImages context{root,millis(),{}};
  bool ok=candidate->prepare(root) && candidate->inspectImages([](void* c,const char* path,bool driver){
    return inspectProvisionedImage(*static_cast<AdmissionImages*>(c),path,driver?ElfRole::Driver:ElfRole::Application);},&context);
  // Non-policy child apps and unselected driver files are still native code;
  // every extra .elf must have a valid supported entry and ordinary imports.
  for(size_t i=0;ok && i<profile.count;++i){const char* name=profile.files[i].path;size_t n=strlen(name);
    if(context.seen[i] || n<4 || strcmp(name+n-4,".elf"))continue;
    char path[256];ok=RiscBoot::path(root,name,path,sizeof(path)) && inspectProvisionedImage(context,path,ElfRole::Either);
  }
  ok=ok && operationSafe() && uint32_t(millis()-context.started)<30000u;
  // prepare/inspect never map modules, issue grants, invoke providers or touch
  // peripherals, so metadata can be discarded even when admission fails.
  candidate->~Runtime();free(candidate);candidateCpu->~Port();free(candidateCpu);candidateCpu=nullptr;return ok;
}
bool finishApp(void*,unsigned b){
  if(!operationSafe() || b==activeBank || !appFile || !mounted)return false;
  FILE* f=appFile;appFile=nullptr;if(fclose(f)!=0)return false;
  char filename[256];
  if(!absolute(filename,sizeof(filename),scratch->appPaths.manifest))return false;
  f=fopen(filename,"wb");if(!f)return false;
  bool written=fwrite(scratch->manifest,1,scratch->manifestSize,f)==scratch->manifestSize;
  if(fclose(f)!=0)written=false;
  if(!written)return false;
  // Verify both replacements after all writes/possible filesystem GC.
  f=fopen(filename,"rb");if(!f)return false;
  written=fread(scratch->buffer,1,scratch->manifestSize,f)==scratch->manifestSize &&
    fgetc(f)==EOF && !ferror(f) && !memcmp(scratch->buffer,scratch->manifest,scratch->manifestSize);
  if(fclose(f)!=0)written=false;
  if(!written || !absolute(filename,sizeof(filename),scratch->appPaths.elf))return false;
  f=fopen(filename,"rb");if(!f)return false;
  auto* data=static_cast<uint8_t*>(heap_caps_malloc(scratch->appSize,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!data){fclose(f);return false;}
  uint32_t offset=0,started=millis();bool ok=hashBegin(nullptr);
  while(ok && offset<scratch->appSize){uint32_t n=std::min(4096u,scratch->appSize-offset);
    ok=operationSafe() && fread(data+offset,1,n,f)==n && hashAdd(nullptr,data+offset,n);offset+=n;vTaskDelay(1);
    if(uint32_t(millis()-started)>30000u)ok=false;
  }
  uint8_t digest[32];ok=ok && fgetc(f)==EOF && !ferror(f) && hashEnd(nullptr,digest) && !memcmp(digest,scratch->appDigest,32) && admitElf(data,scratch->appSize);
  if(fclose(f)!=0)ok=false;
  free(data);if(!ok)return false;
  return RiscUpdate::auditStore("/bootfs","/updatefs",scratch->appPaths.elf,scratch->appPaths.manifest,
    scratch->buffer,sizeof(scratch->buffer),nullptr,now,[](void*){vTaskDelay(1);return operationSafe();});
}
bool validateFirmware(void*,unsigned b,uint32_t n){
  if(!operationSafe() || b>1 || n<sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t)+sizeof(esp_app_desc_t))return false;
  esp_image_metadata_t metadata{};esp_partition_pos_t position={parts[b][0]->address,parts[b][0]->size};
  if(esp_image_verify(ESP_IMAGE_VERIFY,&position,&metadata)!=ESP_OK || metadata.image_len!=n)return false;
  esp_app_desc_t description{};if(esp_ota_get_partition_description(parts[b][0],&description)!=ESP_OK)return false;
  const esp_app_desc_t* current=esp_ota_get_app_description();
  if(!current || memcmp(description.project_name,current->project_name,sizeof(description.project_name)))return false;
  // Verify actual compatible-loader marker rather than trusting a caller flag.
  uint8_t* block=scratch->buffer;size_t carry=0;uint32_t started=millis();
  bool abiFound=false,versionFound=false;char version[32]{};
  constexpr char prefix[]="RISC_RUNTIME_VERSION:";
  for(uint32_t at=0;at<n;){uint32_t count=std::min(4096u,n-at);
    if(!operationSafe() || esp_partition_read(parts[b][0],at,block+carry,count)!=ESP_OK)return false;
    size_t available=carry+count;
    for(size_t p=0;p+sizeof(risc_paired_store_abi)<=available;++p)if(!memcmp(block+p,risc_paired_store_abi,sizeof(risc_paired_store_abi)))abiFound=true;
    for(size_t p=0;p+sizeof(prefix)<=available;++p)if(!memcmp(block+p,prefix,sizeof(prefix)-1)){
      const char* value=reinterpret_cast<const char*>(block+p+sizeof(prefix)-1);
      size_t room=available-p-sizeof(prefix)+1;
      const void* end=memchr(value,0,std::min(room,sizeof(version)));
      if(end){size_t len=static_cast<const char*>(end)-value;
        if(!len)continue; // The scan prefix itself is also a linked literal.
        if(versionFound && (strlen(version)!=len || memcmp(version,value,len)))return false;
        memcpy(version,value,len);version[len]=0;versionFound=true;
      }
    }
    carry=std::min(available,size_t(95));memmove(block,block+available-carry,carry);at+=count;
    vTaskDelay(1);if(uint32_t(millis()-started)>30000u)return false;
  }
  if(!abiFound || !versionFound || !RuntimePackages::safeVersion(version))return false;
  uint32_t actual[3]{},currentVersion[3]{};
  if(!RiscUpdate::parseVersion(version,actual) || !RiscUpdate::parseVersion(RISC_BUILD_VERSION,currentVersion))return false;
  int comparison=RiscUpdate::compareVersion(actual,currentVersion);
  return replacingFirmware && b!=activeBank ? comparison>0 : comparison==0;
}
bool writeProvisionAttempt(unsigned b){
  if(!operationSafe() || !provisionProfile || b>=2 || b==activeBank)return false;
  Record pair{};ProvisionAttempt prior{},check{};
  if(!journal || esp_partition_read(journal,b*SectorBytes,&pair,sizeof(pair))!=ESP_OK || !validRecord(pair,b) ||
     esp_partition_read(journal,b*SectorBytes+AttemptOffset,&prior,sizeof(prior))!=ESP_OK || !emptyAttempt(prior))return false;
  const auto attempt=makeAttempt(b,provisionDigest,pair,scratch->verifiedActiveRecord);
  return esp_partition_write(journal,b*SectorBytes+AttemptOffset,&attempt,sizeof(attempt))==ESP_OK &&
    esp_partition_read(journal,b*SectorBytes+AttemptOffset,&check,sizeof(check))==ESP_OK &&
    !memcmp(&attempt,&check,sizeof(attempt)) && validAttempt(check,pair,b);
}
bool select(void*,unsigned b){return operationSafe() && b<2 && b!=activeBank && esp_ota_set_boot_partition(parts[b][0])==ESP_OK;}


bool ready(){return prepared && operationSafe() && runtime && runtime->active() && confirmed;}
int32_t beginFirmware(void*,const risc_bank_image_v1* image,uint64_t* token){
  if(token)*token=0;
  if(!ready() || !image)return RISC_BANK_UNAVAILABLE;
  risc_bank_status_v1 status{};status.struct_size=sizeof(status);
  if(!transaction->status(&status) || status.state!=RISC_BANK_IDLE)return RISC_BANK_STATE;
  scratch->manifestSize=0;replacingFirmware=true;return transaction->begin(false,*image,token);
}
int32_t beginApp(void*,const char* id,const void* bytes,uint32_t n,const risc_bank_image_v1* image,uint64_t* token){
  if(token)*token=0;
  if(!ready() || !image)return RISC_BANK_UNAVAILABLE;
  risc_bank_status_v1 status{};status.struct_size=sizeof(status);
  if(!transaction->status(&status) || status.state!=RISC_BANK_IDLE)return RISC_BANK_STATE;
  if(!runtime->appUpdate(id,bytes,n,scratch->appPaths))return RISC_BANK_INVALID;
  memcpy(scratch->manifest,bytes,n);scratch->manifestSize=n;scratch->appSize=image->size;memcpy(scratch->appDigest,image->sha256,32);
  replacingFirmware=false;
  return transaction->begin(true,*image,token);
}
const risc_bank_store_v1 api={1,sizeof(api),nullptr,
  [](void*,risc_bank_status_v1* out){
    if(!owner() || !transaction->status(out))return false;
    out->store_abi=RISC_BANK_STORE_ABI;out->app_count=runtime?runtime->appCount():0;
    snprintf(out->runtime_version,sizeof(out->runtime_version),"%s",RISC_BUILD_VERSION);
    snprintf(out->layout,sizeof(out->layout),"%s","riscrte-paired-16m-v1");return true;
  },beginFirmware,beginApp,
  [](void*,uint64_t t,risc_bank_status_v1* s){
    if(!ready())return RISC_BANK_UNAVAILABLE;
    int32_t result=transaction->step(t,s);
    if(s && s->struct_size>=sizeof(*s)){
      s->store_abi=RISC_BANK_STORE_ABI;s->app_count=runtime->appCount();
      snprintf(s->runtime_version,sizeof(s->runtime_version),"%s",RISC_BUILD_VERSION);
      snprintf(s->layout,sizeof(s->layout),"%s","riscrte-paired-16m-v1");}
    return result;
  },
  [](void*,uint64_t t,const void* p,uint32_t n){return ready()?transaction->write(t,p,n):RISC_BANK_UNAVAILABLE;},
  [](void*,uint64_t t){return ready()?transaction->finish(t):RISC_BANK_UNAVAILABLE;},
  [](void*,uint64_t t){return ready()?transaction->activate(t):RISC_BANK_UNAVAILABLE;},
  [](void*,uint64_t t){return owner()?transaction->abort(t):RISC_BANK_UNAVAILABLE;},
  [](void*,uint64_t t){if(!owner() || !transaction->activated(t) || !restartIsSafe || !restartIsSafe() || !cleanup(nullptr))return false;ESP.restart();return false;},
  [](void*,uint32_t index,void* output,uint32_t capacity,uint32_t* actual)->int32_t{
    if(actual)*actual=0;
    if(!ready())return RISC_BANK_UNAVAILABLE;
    if(index>=runtime->appCount())return RISC_BANK_NOT_FOUND;
    return runtime->appInventory(index,output,capacity,actual)?RISC_BANK_OK:RISC_BANK_INVALID;
  }};
bool bindProvisioningCandidate(RiscBoot::Runtime& candidate){
  return candidateCpu && provisionState && prepared && confirmed && !pending && !runtime && operationSafe() &&
    candidateCpu->bind(candidate) && candidate.registerPlatform(RISC_BANK_STORE_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&api);
}
bool partition(const esp_partition_t*& out,esp_partition_type_t type,esp_partition_subtype_t subtype,const char* label,uint32_t offset,uint32_t size){
  out=esp_partition_find_first(type,subtype,label);return out && !out->encrypted && out->address==offset && out->size==size;
}
bool checkHash(unsigned b,unsigned r,uint32_t n,const uint8_t* expected){
  uint8_t* bytes=scratch->buffer;uint8_t digest[32];uint32_t start=millis();if(!hashBegin(nullptr))return false;
  for(uint32_t at=0;at<n;){uint32_t count=std::min(4096u,n-at);if(!read(nullptr,b,r,at,bytes,count) || !hashAdd(nullptr,bytes,count))return false;
    at+=count;vTaskDelay(1);if(uint32_t(millis()-start)>30000u)return false;}
  return hashEnd(nullptr,digest) && !memcmp(digest,expected,32);
}
bool knownBootloader(){
  // Exact Arduino 2.0.17 qio80/16MiB bootloader, whose executable segments were
  // independently compared to the rollback-enabled upstream ELF. Different
  // prebuilt bootloaders require a reviewed fingerprint, never an app -D flag.
  static const uint8_t expected[32]={0x2a,0x71,0xd6,0x9b,0x47,0x1e,0x20,0xc2,0xba,0xc7,0xfb,0x46,0x9f,0x3c,0x6a,0x80,0x7b,0x3e,0xbe,0xe7,0x80,0xe3,0x48,0xe5,0x88,0x9d,0xb0,0xda,0x84,0x9c,0xa3,0x63};
  uint8_t digest[32];if(!hashBegin(nullptr))return false;
  for(uint32_t at=0;at<15104;){uint32_t n=std::min(4096u,15104u-at);
    if(esp_flash_read(esp_flash_default_chip,scratch->buffer,at,n)!=ESP_OK || !hashAdd(nullptr,scratch->buffer,n))return false;
    at+=n;vTaskDelay(1);}
  return hashEnd(nullptr,digest) && !memcmp(digest,expected,sizeof(expected));
}
}
bool prepareBoot(bool (*own)(),bool (*safe)(),bool (*operation)()){
  if(prepared)return false;
  isOwner=own;restartIsSafe=safe;operationIsSafe=operation;if(!operationSafe() || ESP.getFlashChipSize()<0x1000000)return false;
  if(scratch || transaction)return false;
  void* memory=heap_caps_malloc(sizeof(Scratch),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!memory)return false;
  scratch=new(memory) Scratch;
  memory=heap_caps_malloc(sizeof(Transaction),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!memory){scratch->~Scratch();free(scratch);scratch=nullptr;return false;}
  transaction=new(memory) Transaction({nullptr,now,read,erase,write,invalidate,record,hashBegin,hashAdd,hashEnd,openApp,writeApp,finishApp,cleanup,validateFirmware,select,openWholeStore,finishWholeStore});
  mbedtls_sha256_init(&scratch->hashContext);
  if(!knownBootloader())return false;
  for(unsigned b=0;b<2;++b){const char* appLabel=b?"app1":"app0";
    if(!partition(parts[b][0],ESP_PARTITION_TYPE_APP,esp_partition_subtype_t(ESP_PARTITION_SUBTYPE_APP_OTA_0+b),appLabel,FirmwareOffset[b],FirmwareBytes) ||
       !partition(parts[b][1],ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,labels[b],StoreOffset[b],StoreBytes))return false;}
  const esp_partition_t* ota=nullptr;const esp_partition_t* nvs=nullptr;
  if(!partition(ota,ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,"otadata",0xff0000,0x2000) ||
     !partition(journal,ESP_PARTITION_TYPE_DATA,esp_partition_subtype_t(0x40),"bank_state",JournalOffset,0x2000) ||
     !partition(nvs,ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,"nvs",0x9000,0x6000))return false;
  rollbackTrusted=true;
  const esp_partition_t* running=esp_ota_get_running_partition();
  if(!running || (running->address!=FirmwareOffset[0] && running->address!=FirmwareOffset[1]))return false;
  activeBank=running->address==FirmwareOffset[1];
  esp_ota_img_states_t state;if(esp_ota_get_state_partition(running,&state)!=ESP_OK)return false;
  pending=state==ESP_OTA_IMG_PENDING_VERIFY;confirmed=state==ESP_OTA_IMG_VALID;
  if(!pending && !confirmed)return false;
  Record value{};if(esp_partition_read(journal,activeBank*4096,&value,sizeof(value))!=ESP_OK || !validRecord(value,activeBank) ||
      !checkHash(activeBank,0,value.firmwareSize,value.firmwareSha) || !checkHash(activeBank,1,value.storeSize,value.storeSha) ||
      !validateFirmware(nullptr,activeBank,value.firmwareSize) || !transaction->initialize(activeBank,value))return false;
  scratch->verifiedActiveRecord=value;prepared=true;return true;
}
const char* bootLabel(){return prepared?labels[activeBank]:nullptr;}
bool bind(RiscBoot::Runtime& rt){if(!prepared || !owner() || runtime || provisionFiles)return false;runtime=&rt;
  return rt.registerPlatform(RISC_BANK_STORE_CAPABILITY,1,RiscBoot::Runtime::Scope::Global,0,&api);}
bool confirmBoot(){
  if(!prepared || !owner() || !runtime || !runtime->active())return false;
  if(confirmed)return true;
  if(!pending || esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK)return false;
  confirmed=true;pending=false;return true;
}
void rejectBoot(){
  if(!owner() || !rollbackTrusted)return;
  // On an unverified pair, fail back through IDF only when a valid prior app
  // exists. Never format storage, clear the journal, or mix store banks.
  esp_ota_img_states_t state;const esp_partition_t* running=esp_ota_get_running_partition();
  if(running && esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY && esp_ota_check_rollback_is_possible())
    esp_ota_mark_app_invalid_rollback_and_reboot();
}
bool provisionAvailable(){return prepared && confirmed && !pending && !runtime && !provisionFiles && !provisionReadRetained && operationSafe();}
ProvisionHistory provisionHistory(const uint8_t (&digest)[32]){
  if(!provisionAvailable() || !journal)return ProvisionHistory::Unavailable;
  const unsigned target=1-activeBank;ProvisionAttempt attempt{};
  if(esp_partition_read(journal,target*SectorBytes+AttemptOffset,&attempt,sizeof(attempt))!=ESP_OK)return ProvisionHistory::Unavailable;
  if(emptyAttempt(attempt))return ProvisionHistory::Clear;
  Record pair{};
  if(esp_partition_read(journal,target*SectorBytes,&pair,sizeof(pair))!=ESP_OK || !validAttempt(attempt,pair,target))return ProvisionHistory::Unavailable;
  if(!sameAttemptSource(attempt,scratch->verifiedActiveRecord))return ProvisionHistory::Clear;
  return memcmp(attempt.profileSha,digest,32)?ProvisionHistory::Clear:ProvisionHistory::SameAttempt;
}
bool provisionReady(uint64_t token){return prepared && confirmed && !pending && !runtime && provisionFiles &&
  provisionProfile && token && token==provisionToken && operationSafe();}
int32_t provisionBegin(const RiscProvision::Profile& profile,const uint8_t (&digest)[32],const RiscCpu::Hardware& hardware,const RiscBoot::KeyValueBackend* keyValue,uint64_t* token){
  if(token)*token=0;
  if(!token || !prepared || !confirmed || pending || runtime || provisionFiles || provisionState || !hardware.owner || !hardware.owner() || !operationSafe())return RISC_BANK_UNAVAILABLE;
  if(profile.count<3 || profile.count>RiscProvision::MaxFiles)return RISC_BANK_INVALID;
  if(provisionHistory(digest)!=ProvisionHistory::Clear)return RISC_BANK_STATE;
  uint32_t total=32;
  for(size_t i=0;i<profile.count;++i){const auto& file=profile.files[i];
    // IDF prefixes the relative SPIFFS name with '/'; never allow truncation.
    char path[256];
    if(!absolute(path,sizeof(path),file.path) || !strcmp(file.path,RiscProvision::StoreFiles::DigestFile) ||
       strlen(file.path)+2>CONFIG_SPIFFS_OBJ_NAME_LEN || !file.bytes || file.bytes>ProvisionCapacity-total)return RISC_BANK_INVALID;
    total+=file.bytes;}
  risc_bank_status_v1 status{};status.struct_size=sizeof(status);
  if(!transaction->status(&status)||status.state!=RISC_BANK_IDLE)return RISC_BANK_STATE;
  void* memory=heap_caps_malloc(sizeof(ProvisionState),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!memory)return RISC_BANK_UNAVAILABLE;
  provisionState=new(memory) ProvisionState({nullptr,now,[](void*){vTaskDelay(1);return operationSafe();},hashBegin,hashAdd,hashEnd,
    [](void*,const char* root,const RiscProvision::Profile& p){return admitProvisionedStore(root,p);}},hardware,keyValue);
  provisionFiles=&provisionState->files;provisionProfile=&profile;memcpy(provisionDigest,digest,32);replacingFirmware=false;
  int32_t result=transaction->beginStore(status.active_store_sha256,&provisionToken);*token=provisionToken;
  if(result!=RISC_BANK_OK && !provisionToken){
    provisionState->~ProvisionState();free(provisionState);provisionState=nullptr;provisionFiles=nullptr;provisionProfile=nullptr;
    memset(provisionDigest,0,sizeof(provisionDigest));
  }
  // Even a failed invalidation has a token and uncertain destination state.
  // Keep ownership until explicit abort; no fallthrough to application boot.
  return result;
}
bool provisionStatus(uint64_t t,risc_bank_status_v1* status){return owner() && provisionFiles && t && t==provisionToken && transaction->status(status);}
int32_t provisionStep(uint64_t t,risc_bank_status_v1* status){return provisionReady(t)?transaction->step(t,status):RISC_BANK_UNAVAILABLE;}
int32_t provisionWrite(uint64_t t,size_t file,const void* data,uint32_t n){
  if(!provisionReady(t)||!transaction->stagingStore(t))return RISC_BANK_STATE;
  return provisionFiles->write(file,data,n)?RISC_BANK_OK:RISC_BANK_INTEGRITY;
}
int32_t provisionFinish(uint64_t t){return provisionReady(t)?transaction->finishStore(t):RISC_BANK_UNAVAILABLE;}
int32_t provisionActivate(uint64_t t){
  if(!provisionReady(t))return RISC_BANK_UNAVAILABLE;
  risc_bank_status_v1 status{};status.struct_size=sizeof(status);
  if(!transaction->status(&status)||status.state!=RISC_BANK_READY)return RISC_BANK_STATE;
  // Pre-selection metadata failure is known NOT to have called the OTA
  // selector. The ordinary abort path may clean this invocation's staging.
  // Persisted nonempty malformed history on a later boot is never erased here.
  if(!writeProvisionAttempt(1-activeBank))return RISC_BANK_IO;
  return transaction->activate(t);
}
int32_t provisionAbort(uint64_t t){
  if(!owner()||!provisionFiles||!t||t!=provisionToken||runtime)return RISC_BANK_STATE;
  const int32_t result=transaction->abort(t);if(result!=RISC_BANK_OK)return result;
  provisionState->~ProvisionState();free(provisionState);provisionState=nullptr;provisionFiles=nullptr;provisionProfile=nullptr;
  memset(provisionDigest,0,sizeof(provisionDigest));provisionToken=0;return RISC_BANK_OK;
}
bool provisionRestart(uint64_t t){return provisionReady(t) && api.restart(nullptr,t);}
bool exitSafe(){return !prepared || (transaction->exitSafe() && !appFile && !mounted);}
}
#endif
