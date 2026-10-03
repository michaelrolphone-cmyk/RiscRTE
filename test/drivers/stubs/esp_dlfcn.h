#pragma once
#include <dlfcn.h>
#include <cstdio>
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
