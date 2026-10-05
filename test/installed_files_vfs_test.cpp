/* Device-VFS contract model, not hardware execution. The official IDF4.4 SPIFFS
 * stat/fstat callbacks zero dev/ino/ctime and return size/mode/coarse mtime:
 * https://github.com/espressif/esp-idf/blob/v4.4.7/components/spiffs/esp_spiffs.c
 * Linker wrappers reproduce that shape and inject synchronous I/O failures. */
#define ESP_PLATFORM
#include "bootstrap/InstalledFiles.h"
#include <cassert>
#include <fstream>
#include <string>
static unsigned open_count=0,close_count=0;static bool fail_seek=false,fail_read=false,fail_close=false,fail_stat=false;
extern "C" int __real_stat(const char*,struct stat*);
extern "C" int __real_fstat(int,struct stat*);
extern "C" FILE* __real_fopen(const char*,const char*);
extern "C" int __real_fclose(FILE*);
extern "C" int __real_fseek(FILE*,long,int);
extern "C" size_t __real_fread(void*,size_t,size_t,FILE*);
static void coarse(struct stat*s){auto size=s->st_size;auto mode=s->st_mode;auto time=s->st_mtime;memset(s,0,sizeof(*s));s->st_size=size;s->st_mode=mode;s->st_mtime=time;}
extern "C" int __wrap_stat(const char*p,struct stat*s){if(fail_stat)return -1;int r=__real_stat(p,s);if(!r)coarse(s);return r;}
extern "C" int __wrap_fstat(int f,struct stat*s){if(fail_stat)return -1;int r=__real_fstat(f,s);if(!r)coarse(s);return r;}
extern "C" FILE* __wrap_fopen(const char*p,const char*m){assert(!strcmp(m,"rb"));FILE*f=__real_fopen(p,m);if(f)open_count++;return f;}
extern "C" int __wrap_fclose(FILE*f){close_count++;int r=__real_fclose(f);return fail_close?-1:r;}
extern "C" int __wrap_fseek(FILE*f,long o,int m){return fail_seek?-1:__real_fseek(f,o,m);}
extern "C" size_t __wrap_fread(void*b,size_t s,size_t n,FILE*f){if(fail_read)return 0;return __real_fread(b,s,n,f);}
int main(int argc,char**argv){assert(argc==2);std::string root=argv[1];{std::ofstream f(root+"/app.elf");f<<std::string(1100,'X');}
 RiscBoot::InstalledFiles::Name name{};strcpy(name.value,"app.elf");RiscBoot::InstalledFiles files;assert(files.configure(root.c_str(),&name,1));uint64_t size=0;auto handle=files.fileOpen("/app.elf",&size);assert(handle && size==1100);unsigned char out[600];
 assert(files.fileRead(handle,out,sizeof(out))==512);assert(files.fileRead(handle,out,sizeof(out))==512);assert(files.fileRead(handle,out,sizeof(out))==76);assert(open_count==close_count);assert(files.fileClose(handle));
 for(bool*failure:{&fail_seek,&fail_read,&fail_stat,&fail_close}){handle=files.fileOpen("/app.elf",&size);assert(handle);*failure=true;memset(out,0xcc,sizeof(out));assert(!files.fileRead(handle,out,10));for(unsigned n=0;n<sizeof(out);n++)assert(out[n]==0xcc);assert(open_count==close_count);*failure=false;if(failure==&fail_close){assert(files.retained() && !files.refresh() && !files.end() && !files.fileClose(handle));unsigned opened=open_count;assert(!files.fileRead(handle,out,10) && open_count==opened);}else assert(files.fileClose(handle));}
 puts("SPIFFS stat/fstat shape and seek/read/close-failure model: bounded reads, no partial output, no successful callback retains a descriptor; failed close latches retention PASS");}
