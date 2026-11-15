#pragma once
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
// Host OS dlopen caches inodes. Unique temporary copies exercise real independent
// host mappings of identical bytes. Target performs independent ELF relocation.
inline void* esp_dlopen_instance(const char* path) {
  FILE* in=fopen(path,"rb"); if (!in) return nullptr;
  char tmp[]="/tmp/riscrte-instance-XXXXXX"; int fd=mkstemp(tmp);
  if (fd<0) { fclose(in); return nullptr; }
  FILE* out=fdopen(fd,"wb"); bool ok=out!=nullptr; size_t total=0;
  if (out) {
    char buf[4096]; size_t n;
    while ((n=fread(buf,1,sizeof(buf),in))!=0) {
      total+=n;
      if (total>8*1024*1024 || fwrite(buf,1,n,out)!=n) { ok=false; break; }
    }
    ok=ok && !ferror(in); if (fclose(out)) ok=false;
  } else close(fd);
  fclose(in);
  void* result=ok?dlopen(tmp,RTLD_NOW|RTLD_LOCAL):nullptr;
  unlink(tmp); return result;
}

#include "../../../lib/elf_loader/include/esp_elf_image.h"
#if !defined(ESP_PLATFORM) && !defined(RISC_REMOVABLE_APP_TEST) && !defined(RISC_APP_IMAGE_CACHE_TEST)
// Host-only instruction mapper: consume the exact provider snapshot, never
// reopen its logical path. Target validation remains separately qualified.
inline bool esp_elf_validate_file(const uint8_t* p,size_t n) {
#if defined(__APPLE__)
  return n>=52 && p[0]==0xcf && p[1]==0xfa && p[2]==0xed && p[3]==0xfe;
#else
  return n>=52 && !std::memcmp(p,"\177ELF",4);
#endif
}
inline bool esp_elf_admit_managed_app(const char*,const uint8_t*,size_t){return true;}
inline void* esp_dlopen_reader_instance(const char* path,esp_dl_image_reader reader,void* context) {
  elf_file_t file{};if(!reader || reader(context,path,&file))return nullptr;
  char tmp[]="/tmp/riscrte-provider-XXXXXX";int fd=mkstemp(tmp);
  if(fd<0){std::free(file.payload);return nullptr;}
  FILE* out=fdopen(fd,"wb");bool ok=out!=nullptr;
  if(out){ok=fwrite(file.payload,1,file.size,out)==file.size;if(fclose(out))ok=false;}
  else close(fd);
  std::free(file.payload);void* result=ok?dlopen(tmp,RTLD_NOW|RTLD_LOCAL):nullptr;
  unlink(tmp);return result;
}
#endif
