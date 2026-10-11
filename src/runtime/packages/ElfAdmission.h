#pragma once
#include <private/elf_types.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
// One role/import/lifecycle audit shared by bank updates and removable installs.
// Structural validation MUST precede all section/symbol reads. Callbacks use
// immutable native tables; no image relocation or execution occurs here.
namespace RiscPackages {
inline bool ordinaryImport(const char* name){
  static const char* const names[]={"risc_runtime_get_api","strerror","memset","memcpy","memmove","memcmp","memchr","strlen","strcpy","strncpy","strcmp","strncmp","strchr","strrchr","strstr","strtod","strtol","strtof","strtoul","strcat","strncasecmp","strcasecmp","strnlen","tolower","isalpha","qsort","vsnprintf","setjmp","longjmp","strcspn","strncat","snprintf","malloc","calloc","realloc","free","clock_gettime","strftime","__errno","__getreent","__locale_ctype_ptr","_ctype_","__ltdf2","__fixunsdfsi","__gtdf2","__floatunsidf","__divdf3","getopt_long","optind","opterr","optarg","optopt"};
  for(const char* n:names)if(!strcmp(n,name))return true;
  return false;
}

struct ElfAdmissionChecks {
 bool (*structure)(const uint8_t*,size_t);
 bool (*import)(const char*);
 bool (*safe)();void (*yield)();uint32_t (*now)();
};
template<class ElfRole> bool admitElf(const uint8_t*bytes,size_t n,ElfRole role,bool nativePolicy,const ElfAdmissionChecks&checks){
 if(!checks.safe()||!bytes||!n||n>2097152u)return false;
 const uint32_t started=checks.now();unsigned visited=0;
 if(!checks.structure(bytes,n))return false;
 checks.yield();if(!checks.safe()||uint32_t(checks.now()-started)>30000u)return false;
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
      if(++visited>2097152u/sizeof(*symbols))return false;
      if(!(visited%128)){checks.yield();if(!checks.safe() || uint32_t(checks.now()-started)>30000u)return false;}
      const auto& sym=symbols[j];const char* name=strings+sym.name;
      if(sym.shndx==SHN_UNDEF){
        const elf32_sym_t empty{};if(!j && !memcmp(&sym,&empty,sizeof(sym)))continue;
        if((ELF_ST_BIND(sym.info)!=STB_GLOBAL && ELF_ST_BIND(sym.info)!=STB_WEAK) || !*name ||
           (!nativePolicy && !checks.import(name)))return false;
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
  return role==ElfRole::Application?application:role==ElfRole::Driver?driver==1 && !main && !init && !fini:application || (driver==1 && !main && !init && !fini);
}
}
