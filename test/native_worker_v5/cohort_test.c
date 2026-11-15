/* Bounded import/cohort consumer regression, not provider execution. */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "private/elf_types.h"
#include "private/esp_privileged_cohort.h"
#include "private/esp_privileged_imports.h"
extern bool esp_elf_validate_file(const uint8_t*,size_t);
struct Image { elf32_hdr_t h;elf32_shdr_t s[5];char names[80],imports[160];elf32_sym_t syms[3];uint32_t text; };
static struct Image image(void) {
 struct Image f={0};memcpy(f.h.ident,"\177ELF\1\1\1",7);f.h.machine=94;f.h.type=3;f.h.version=1;
 f.h.ehsize=sizeof(f.h);f.h.shentsize=sizeof(f.s[0]);f.h.shnum=5;f.h.shstrndx=1;f.h.shoff=offsetof(struct Image,s);
 memcpy(f.names,"\0.shstrtab\0.dynstr\0.dynsym\0.text\0",33);
 f.s[1]=(elf32_shdr_t){.name=1,.type=SHT_STRTAB,.offset=offsetof(struct Image,names),.size=sizeof(f.names)};
 f.s[2]=(elf32_shdr_t){.name=11,.type=SHT_STRTAB,.offset=offsetof(struct Image,imports),.size=sizeof(f.imports)};
 f.s[3]=(elf32_shdr_t){.name=19,.type=SHT_SYNSYM,.offset=offsetof(struct Image,syms),.size=sizeof(f.syms),.link=2};
 f.s[4]=(elf32_shdr_t){.name=27,.type=SHT_PROGBITS,.flags=SHF_ALLOC|SHF_EXECINSTR,.addr=0x1000,.offset=offsetof(struct Image,text),.size=4};
 strcpy(f.imports+1,"vTaskDelay");f.syms[1].name=1;f.syms[1].info=STB_GLOBAL<<4;
 unsigned at=(unsigned)strlen(f.imports+1)+2;strcpy(f.imports+at,"t5_driver_get");
 f.syms[2]=(elf32_sym_t){.name=at,.value=0x1000,.size=4,.info=(STB_GLOBAL<<4)|STT_FUNC,.shndx=4};
 return f;
}
int main(int argc,char**argv) {
 assert(argc==2);
 if(!strcmp(argv[1],"legacy-candidate"))assert(RISC_CPU_SCOPE_V5_CANDIDATE==1);
 else assert(RISC_CPU_SCOPE_V5_CANDIDATE==0);
 assert(!esp_elf_privileged_os_cpu_abi_supported(0));
 assert(!esp_elf_privileged_os_cpu_abi_supported(3));
 assert(!esp_elf_privileged_os_cpu_abi_supported(4));
 assert(!esp_elf_privileged_os_cpu_abi_supported(5));
#if RISC_NATIVE_ESP_IDF
 assert(!strcmp(argv[1],"default-native"));
 assert(!esp_elf_privileged_os_cpu_abi_supported(1)&&esp_elf_privileged_os_cpu_abi_supported(2));
#else
 assert(!strcmp(argv[1],"legacy")||!strcmp(argv[1],"legacy-candidate"));
 assert(esp_elf_privileged_os_cpu_abi_supported(1)&&!esp_elf_privileged_os_cpu_abi_supported(2));
#endif
 struct Image f=image();assert(esp_elf_validate_file((const uint8_t*)&f,sizeof(f)));
 assert(esp_elf_privileged_imports_valid_v1((const uint8_t*)&f,sizeof(f)));
 assert(esp_elf_privileged_imports_valid_v2((const uint8_t*)&f,sizeof(f)));
 assert(!esp_elf_privileged_imports_valid_v5((const uint8_t*)&f,sizeof(f)));
 strcpy(f.imports+1,"forbidden");
 assert(!esp_elf_privileged_imports_valid_v1((const uint8_t*)&f,sizeof(f)));
 assert(!esp_elf_privileged_imports_valid_v2((const uint8_t*)&f,sizeof(f)));
 printf("PASS %s: default-off cohort and actual v1/v2 import consumer\n",argv[1]);
 return 0;
}
