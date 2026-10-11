#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
#include <openssl/sha.h>
#include "ports/esp32s3/NativeStorageAdmission.h"
int64_t admission_test_time=0;bool admission_test_oom=false;
static bool worker=true,closeFail=false,readFail=false,present=true;
static unsigned opens,reads,closes;static size_t position;
static const char*missing=nullptr;
namespace RiscCpu {namespace NativeStorageWorker {
static bool isWorker(){return worker;}
static void delayMs(uint32_t n){admission_test_time+=int64_t(n)*1000;}
}}
extern "C" bool esp_elf_public_import_supported_v1(const char*name){return !missing||strcmp(name,missing);}
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

static std::vector<uint8_t> image;
int main(){
 risc_storage_volume_api_v1_ext volume{};auto&v=volume.base;v.api_version=1;v.struct_size=sizeof(volume);
 v.file_open_read=[](void*,const char*,uint64_t*n)->uint32_t{assert(worker);++opens;position=0;*n=image.size();return present?3:0;};
 v.file_read=[](void*,uint32_t,void*out,size_t n)->size_t{assert(worker);++reads;if(readFail)return 0;n=std::min(n,image.size()-position);memcpy(out,image.data()+position,n);position+=n;return n;};
 v.file_close=[](void*,uint32_t f,bool commit){assert(worker&&f==3&&!commit);++closes;return !closeFail;};
 volume.handle_error=[](void*,uint32_t,bool)->uint32_t{return readFail?1:0;};
 auto validate=[&](bool corrupt=false,uint32_t size=0){unsigned char digest[32];SHA256(image.data(),image.size(),digest);if(corrupt)digest[0]^=1;uint32_t held=99;
  int rc=RiscCpu::NativeStorageWorker::admitApplication(&v,"/Apps/new/version.elf",size?size:uint32_t(image.size()),digest,&held);
  assert(held==(closeFail?3u:0u));return rc;};
 image=elf("memcpy");assert(validate()==0&&opens==1&&reads==1&&closes==1);
 assert(nativeHashBytes==image.size());assert(validate(true)==RISC_STORAGE_WORK_INVALID);
 missing="memcpy";assert(validate()==RISC_STORAGE_WORK_INVALID);missing=nullptr;
 image=elf("esp_partition_write");assert(validate()==RISC_STORAGE_WORK_INVALID);
 image=elf("memcpy","t5_driver_get");assert(validate()==RISC_STORAGE_WORK_INVALID);
 image=elf("memcpy");assert(validate(false,uint32_t(image.size()+1))==RISC_STORAGE_WORK_INVALID);
 image[0]=0;assert(validate()==RISC_STORAGE_WORK_INVALID);image=elf("memcpy");
 readFail=true;assert(validate()==RISC_STORAGE_WORK_INVALID);readFail=false;
 present=false;assert(validate()==RISC_STORAGE_WORK_INVALID);present=true;
 unsigned before=opens;worker=false;assert(validate()==RISC_STORAGE_WORK_INVALID&&opens==before);worker=true;
 admission_test_oom=true;assert(validate()==RISC_STORAGE_WORK_BUSY&&opens==before);admission_test_oom=false;
 closeFail=true;assert(validate()==RISC_STORAGE_WORK_RETAINED);closeFail=false;
 puts("Native staged-image admission: exact snapshot SHA, ELF/import/role rejection, worker-only I/O, OOM and retained close PASS");
}
