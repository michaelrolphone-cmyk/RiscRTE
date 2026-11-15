/* Default-off successor with no internal-port leaves. Actual loader and
 * relocation, modeled ordinary allocation/scope only; no target execution. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_elf.c"
/* Exact validator declaration for this fixture's direct structural check. */
extern bool esp_elf_validate_file(const uint8_t *, size_t);
static unsigned live;
void *esp_elf_malloc(uint32_t n, bool code) { (void)code; void *p=malloc(n); assert(p); ++live; return p; }
void esp_elf_free(void *p) { if(p) { assert(live); --live; free(p); } }
uintptr_t elf_find_sym_default(const char *s) { (void)s; return 0; }
bool esp_elf_privileged_os_cpu_scope_owned_v1(void) { return false; }
bool esp_elf_privileged_os_cpu_relocation_enter_v1(const void *p) { return p!=NULL; }
bool esp_elf_privileged_os_cpu_relocation_leave_v1(const void *p) { return p!=NULL; }
uintptr_t elf_remap_text(esp_elf_t *e, uintptr_t p) { (void)e; return p; }
int esp_elf_arch_flush(esp_elf_t *e) { assert(e); return 0; }
typedef struct {
    elf32_hdr_t h; elf32_shdr_t s[6]; char names[64], strings[32];
    elf32_sym_t symbols[2]; uint32_t text[4],data[4];
} Fixture;
int main(int argc, char **argv) {
    assert(argc == 1 || (argc == 2 && !strcmp(argv[1], "internal")));
    Fixture f={0}; memcpy(f.h.ident,"\177ELF\1\1\1",7);
    f.h.type=3;f.h.machine=94;f.h.version=1;f.h.ehsize=sizeof(f.h);
    f.h.shoff=offsetof(Fixture,s);f.h.shentsize=sizeof(f.s[0]);f.h.shnum=6;f.h.shstrndx=1;
    const char names[]="\0.shstrtab\0.dynstr\0.dynsym\0.text\0.data\0";
    memcpy(f.names,names,sizeof(names));strcpy(f.strings+1,"t5_driver_get");
    f.s[1]=(elf32_shdr_t){.name=1,.type=SHT_STRTAB,.offset=offsetof(Fixture,names),.size=sizeof(f.names)};
    f.s[2]=(elf32_shdr_t){.name=11,.type=SHT_STRTAB,.offset=offsetof(Fixture,strings),.size=sizeof(f.strings)};
    f.s[3]=(elf32_shdr_t){.name=19,.type=SHT_SYNSYM,.offset=offsetof(Fixture,symbols),.size=sizeof(f.symbols),.link=2};
    f.s[4]=(elf32_shdr_t){.name=27,.type=SHT_PROGBITS,.flags=6,.offset=offsetof(Fixture,text),.size=16,.addralign=4};
    f.s[5]=(elf32_shdr_t){.name=33,.type=SHT_PROGBITS,.flags=3,.addr=0x100,.offset=offsetof(Fixture,data),.size=16,.addralign=4};
    f.symbols[1]=(elf32_sym_t){.name=1,.info=ELF_ST_INFO(STB_GLOBAL,STT_FUNC),.shndx=4,.size=4};
    f.text[0]=0x11223344;f.data[0]=0x55667788;
    if (argc == 2) strcpy(f.names + 33, ".risc.internal.data");
    assert(esp_elf_validate_file((const uint8_t *)&f,sizeof(f)));
    esp_elf_t e;assert(!esp_elf_init(&e));
    if (argc == 2) {
        assert(esp_elf_relocate(&e,(const uint8_t *)&f)==-ENOTSUP);
        assert(!live);esp_elf_deinit(&e);assert(!live);
        puts("PASS default-off-internal-refused");return 0;
    }
    assert(!esp_elf_relocate(&e,(const uint8_t *)&f));
    assert(e.num==1&&!strcmp(e.symtab[0].name,"t5_driver_get"));
    /* Check ordinary owned text bytes and getter export in the default-off
     * successor. No target instruction or getter is executed. */
    assert(e.symtab[0].addr==(void*)e.ptext);
    assert(*(uint32_t*)e.ptext==0x11223344);
    assert(*(uint32_t*)e.sec[ELF_SEC_DATA].addr==0x55667788);
    elf32_rela_t r={.offset=0x10c,.info=ELF_R_INFO(0,3)};elf32_sym_t sym={0};
    assert(!esp_elf_arch_relocate(&e,&r,&sym,0x12345678));
    assert(*(uint32_t*)(e.sec[ELF_SEC_DATA].addr+12)==0x12345678);
    esp_elf_deinit(&e);assert(!live);puts("PASS ordinary-loader-equivalent");
}
