/* Real reader/validator/relocator/dlfcn/cache with packaged Xtensa inputs.
 * Architecture relocation/publication and RTOS are host hooks. No Xtensa code
 * is executed; data bytes, mapping bounds and callable addresses are checked. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_elf.h"
#include "esp_dlfcn.h"
#include "private/esp_dlmod.h"
#include "private/elf_platform.h"
#include "freertos/task.h"
static unsigned allocations,parses,loads,aliases;
static bool expect_schema;
static TickType_t ticks;
static const char descriptor_name[]="risc_resident_app_descriptor_v1";
void risc_perf_loader_event(uint32_t phase,uint32_t value){(void)value;if(phase==45)++parses;if(phase==42)++loads;}
TickType_t xTaskGetTickCount(void){return ticks;}
void vTaskDelay(TickType_t n){ticks+=n;}
void* esp_elf_malloc(uint32_t n,bool executable){(void)executable;void* p=malloc(n);if(p)++allocations;return p;}
void esp_elf_free(void* p){if(p){assert(allocations);--allocations;free(p);}}
void esp_elf_registered_symbol_used(const void* table,const char* name,uintptr_t address){(void)table;(void)name;(void)address;}
uintptr_t elf_find_sym_default(const char* name){(void)name;return 1;}
bool esp_elf_privileged_os_cpu_scope_owned_v1(void){return false;}
bool esp_elf_privileged_os_cpu_relocation_enter_v1(const void* p){(void)p;return true;}
bool esp_elf_privileged_os_cpu_relocation_leave_v1(const void* p){(void)p;return true;}
int esp_elf_arch_relocate(esp_elf_t* elf,const elf32_rela_t* rela,const elf32_sym_t* symbol,uint32_t address){(void)elf;(void)rela;(void)symbol;(void)address;return 0;}
int esp_elf_arch_flush(esp_elf_t* elf){(void)elf;return 0;}
static uintptr_t alias(uintptr_t pointer){return pointer^UINT64_C(0x100000000);}
uintptr_t elf_remap_text(esp_elf_t* elf,uintptr_t pointer){
 const uintptr_t full=(uintptr_t)elf->ptext,shortened=(uint32_t)full;
 assert((pointer>=full && pointer-full<elf->sec[ELF_SEC_TEXT].size) ||
        (pointer>=shortened && pointer-shortened<elf->sec[ELF_SEC_TEXT].size));
 ++aliases;return alias(pointer);
}
static elf32_sym_t* symbol(uint8_t* bytes,const char* wanted){
 const elf32_hdr_t* h=(const elf32_hdr_t*)bytes;const elf32_shdr_t* sections=(const elf32_shdr_t*)(bytes+h->shoff);
 const char* names=(const char*)bytes+sections[h->shstrndx].offset;
 for(unsigned i=0;i<h->shnum;++i)if(!strcmp(names+sections[i].name,ELF_DYNSYM)){
  elf32_sym_t* symbols=(elf32_sym_t*)(bytes+sections[i].offset);const char* strings=(const char*)bytes+sections[sections[i].link].offset;
  for(unsigned j=0;j<sections[i].size/sizeof(*symbols);++j)if(!strcmp(strings+symbols[j].name,wanted))return &symbols[j];
 }
 return NULL;
}
static void lookup(const char* path,esp_dl_image_cache** cache,const uint8_t* bytes,bool present,const char* mode){
 struct dlmod_slist_t* handle=cache?esp_dlopen_cached_instance(cache,path):esp_dlopen_instance(path);
 assert(handle && handle->elf);esp_elf_t* elf=handle->elf;
 void* descriptor=dlsym(handle,descriptor_name);
 if(present){
  const elf32_hdr_t* h=(const elf32_hdr_t*)bytes;const elf32_shdr_t* sections=(const elf32_shdr_t*)(bytes+h->shoff);
  const elf32_sym_t* expected=symbol((uint8_t*)bytes,descriptor_name);assert(expected && expected->size==16);
  const elf32_shdr_t* section=&sections[expected->shndx];const size_t offset=expected->value-section->addr;
  const char* names=(const char*)bytes+sections[h->shstrndx].offset;
  const char* section_name=names+section->name;
  unsigned slot=!strcmp(section_name,ELF_DATA)?ELF_SEC_DATA:!strcmp(section_name,ELF_BSS)?ELF_SEC_BSS:!strcmp(section_name,ELF_DATA_REL_RO)?ELF_SEC_DRLRO:ELF_SEC_RODATA;
  assert(descriptor && (uintptr_t)descriptor==elf->sec[slot].addr+offset);
  if(section->addralign)assert((elf->sec[slot].addr-section->addr)%section->addralign==0);
  const uint8_t zero[16]={0};
  assert(!memcmp(descriptor,section->type==SHT_NOBITS?zero:bytes+section->offset+offset,16));
  const uint32_t* words=descriptor;if(expect_schema)assert(words[0]==1 && words[1]==16 && (words[2]==1 || words[2]==2) && !words[3]);
  printf("%s %s descriptor=",mode,path);for(unsigned i=0;i<16;++i)printf("%02x",((const uint8_t*)descriptor)[i]);
 }else{assert(!descriptor);printf("%s %s descriptor=absent",mode,path);}
 for(unsigned i=0;i<3;++i){
  const char* name=(const char*[]){"app_main","app_module_init","app_module_fini"}[i];
  const elf32_sym_t* expected=symbol((uint8_t*)bytes,name);void* address=dlsym(handle,name);
  if(expected && expected->shndx!=SHN_UNDEF){assert(address && (uintptr_t)address==alias((uintptr_t)elf->ptext+expected->value-elf->sec[ELF_SEC_TEXT].v_addr));}
  else assert(!address);
  printf(" %s=%s",name,address?"resolved":"absent");
 }
 puts(" PASS");assert(!dlclose(handle));
}
int main(int argc,char** argv){
 assert(argc>=4);const bool present=atoi(argv[1])!=0;expect_schema=atoi(argv[1])==1;
 for(int i=3;i<argc;++i){
  elf_file_t input={0};assert(!esp_elf_open(&input,argv[i]));
  const unsigned before=parses,started=loads;
  lookup(argv[i],NULL,input.payload,present,"uncached");
  esp_dl_image_cache* cache=esp_dl_image_cache_create();assert(cache);
  lookup(argv[i],&cache,input.payload,present,"cache-miss");
  lookup(argv[i],&cache,input.payload,present,"cache-hit");
  assert(parses==before+2 && loads==started+3);
  esp_dl_image_cache_destroy(cache);esp_elf_close(&input);assert(!allocations);
 }
 assert(aliases);(void)argv[2];return 0;
}
