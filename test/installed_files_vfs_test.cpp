/* Device-VFS contract model, not hardware execution. The official IDF4.4 SPIFFS
 * stat/fstat callbacks zero dev/ino/ctime and return size/mode/coarse mtime:
 * https://github.com/espressif/esp-idf/blob/v4.4.7/components/spiffs/esp_spiffs.c
 * Linker wrappers reproduce that shape and inject synchronous I/O failures. */
#define ESP_PLATFORM
#include "bootstrap/InstalledFiles.h"
#include "RuntimeFileFixture.h"
#include <cassert>
#include <fstream>
#include <string>
#include <cstdarg>
static unsigned open_count=0,close_count=0;static bool short_read=false;static bool fail_seek=false,fail_read=false,fail_close=false,fail_stat=false;
extern "C" int __real_stat(const char*,struct stat*);
extern "C" int __real_fstat(int,struct stat*);
extern "C" int __real_open(const char*,int,...);
extern "C" int __real_close(int);
extern "C" off_t __real_lseek(int,off_t,int);
extern "C" ssize_t __real_read(int,void*,size_t);
static void coarse(struct stat*s){auto size=s->st_size;auto mode=s->st_mode;auto time=s->st_mtime;memset(s,0,sizeof(*s));s->st_size=size;s->st_mode=mode;s->st_mtime=time;}
extern "C" int __wrap_stat(const char*p,struct stat*s){if(fail_stat)return -1;int r=__real_stat(p,s);if(!r)coarse(s);return r;}
extern "C" int __wrap_fstat(int f,struct stat*s){if(fail_stat)return -1;int r=__real_fstat(f,s);if(!r)coarse(s);return r;}
extern "C" int __wrap_open(const char*p,int flags,...){mode_t mode=0;if(flags&O_CREAT){va_list args;va_start(args,flags);mode=va_arg(args,int);va_end(args);}assert((flags&O_ACCMODE)==O_RDONLY);int f=__real_open(p,flags,mode);if(f>=0)open_count++;return f;}
extern "C" int __wrap_close(int f){close_count++;int r=__real_close(f);if(fail_close){errno=EIO;return -1;}return r;}
extern "C" off_t __wrap_lseek(int f,off_t o,int m){if(fail_seek){errno=EIO;return -1;}return __real_lseek(f,o,m);}
extern "C" ssize_t __wrap_read(int f,void*b,size_t n){if(fail_read){errno=EIO;return -1;}return __real_read(f,b,short_read&&n>7?7:n);}
int main(int argc,char**argv){assert(argc==2);std::string root=argv[1];RiscTest::FileSources sources({root});{std::ofstream f(root+"/app.elf");f<<std::string(1100,'X');}
 RiscBoot::InstalledFiles::Name name{};strcpy(name.value,"app.elf");RiscBoot::InstalledFiles files;assert(files.configure(root.c_str(),&name,1,sources.sources()));uint64_t size=0;auto handle=files.fileOpen("/app.elf",&size);assert(handle && size==1100);unsigned char out[600];
 assert(files.fileRead(handle,out,sizeof(out))==512);assert(files.fileRead(handle,out,sizeof(out))==512);assert(files.fileRead(handle,out,sizeof(out))==76);assert(open_count==close_count);assert(files.fileClose(handle));
 short_read=true;handle=files.fileOpen("/app.elf",&size);assert(handle);assert(files.fileRead(handle,out,sizeof(out))==512);assert(files.fileClose(handle));short_read=false;
 for(bool*failure:{&fail_seek,&fail_read,&fail_stat,&fail_close}){handle=files.fileOpen("/app.elf",&size);assert(handle);*failure=true;memset(out,0xcc,sizeof(out));assert(!files.fileRead(handle,out,10));for(unsigned n=0;n<sizeof(out);n++)assert(out[n]==0xcc);assert(open_count==close_count);*failure=false;if(failure==&fail_close){assert(files.retained() && !files.refresh() && !files.end() && !files.fileClose(handle));unsigned opened=open_count;assert(!files.fileRead(handle,out,10) && open_count==opened);}else assert(files.fileClose(handle));}
 puts("SPIFFS stat/fstat shape and seek/read/close-failure model: bounded reads, no partial output, no successful callback retains a descriptor; failed close latches retention PASS");}
