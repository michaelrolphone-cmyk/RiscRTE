// RAM flash only. Actual pinned SPIFFS and unchanged IDF VFS callbacks execute
// beneath the production C++ provider; host wrappers supply mount dispatch.
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include "spiffs.h"
#define ESP_OK 0
#ifdef CONFIG_SPIFFS_USE_MTIME
typedef time_t spiffs_time_t;
#endif
// Host DIR is opaque; its unused embedded IDF field is replaced by a byte.
// Every callback treats the returned pointer solely as this private struct.
typedef struct { char unused; spiffs_DIR d; struct dirent e; long offset;
                 char path[SPIFFS_OBJ_NAME_LEN]; } vfs_spiffs_dir_t;
typedef struct { spiffs* fs; } esp_spiffs_t;
static size_t fixture_strlcpy(char*dst,const char*src,size_t cap){size_t n=strlen(src);if(cap){size_t k=n<cap-1?n:cap-1;memcpy(dst,src,k);dst[k]=0;}return n;}
#define strlcpy fixture_strlcpy
#include "spiffs_vfs_callbacks.inc"
#undef strlcpy
static spiffs fs;
static esp_spiffs_t ctx={&fs};
static unsigned char flash[65536],work[512];
static union {max_align_t alignment;unsigned char bytes[4096];} fds;
static s32_t rd(spiffs*f,u32_t at,u32_t n,u8_t*dst){(void)f;assert(at+n<=sizeof(flash));memcpy(dst,flash+at,n);return 0;}
static s32_t wr(spiffs*f,u32_t at,u32_t n,u8_t*src){(void)f;assert(at+n<=sizeof(flash));for(u32_t i=0;i<n;i++)flash[at+i]&=src[i];return 0;}
static s32_t er(spiffs*f,u32_t at,u32_t n){(void)f;assert(at+n<=sizeof(flash));memset(flash+at,255,n);return 0;}
void spiffs_api_lock(spiffs*f){(void)f;}void spiffs_api_unlock(spiffs*f){(void)f;}
void fixture_mount(void){
 memset(flash,255,sizeof(flash));spiffs_config c={0};c.hal_read_f=rd;c.hal_write_f=wr;c.hal_erase_f=er;c.phys_size=sizeof(flash);c.phys_erase_block=4096;c.log_block_size=4096;c.log_page_size=256;
 assert(SPIFFS_mount(&fs,&c,work,fds.bytes,sizeof(fds.bytes),NULL,0,NULL)<0);
 assert(SPIFFS_format(&fs)==0);assert(SPIFFS_mount(&fs,&c,work,fds.bytes,sizeof(fds.bytes),NULL,0,NULL)==0);
}
void fixture_unmount(void){SPIFFS_unmount(&fs);}
static const char* local(const char*p){assert(!strncmp(p,"/bootfs/",8));return p+7;}
// Independent failure injections, after real operation dispatch where relevant.
int fixture_short_write=0,fixture_fstat_fail=0,fixture_close_fail=0,fixture_truncate_fail=0,fixture_disappear=0;
int __wrap_open(const char*p,int flags,...){int fd=vfs_spiffs_open(&ctx,local(p),flags,0600);if(fixture_disappear && fd<0 && errno==EEXIST){fixture_disappear=0;assert(vfs_spiffs_unlink(&ctx,local(p))==0);errno=EEXIST;}return fd;}
int __wrap_close(int fd){if(fixture_close_fail){errno=EIO;return -1;}return vfs_spiffs_close(&ctx,fd);}
ssize_t __wrap_read(int fd,void*b,size_t n){return vfs_spiffs_read(&ctx,fd,b,n);}
ssize_t __wrap_write(int fd,const void*b,size_t n){if(fixture_short_write && n>2)n=2;return vfs_spiffs_write(&ctx,fd,b,n);}
off_t __wrap_lseek(int fd,off_t n,int whence){return vfs_spiffs_lseek(&ctx,fd,n,whence);}
int __wrap_fstat(int fd,struct stat*s){if(fixture_fstat_fail){errno=EIO;return -1;}return vfs_spiffs_fstat(&ctx,fd,s);}
int __wrap_stat(const char*p,struct stat*s){return vfs_spiffs_stat(&ctx,local(p),s);}
int __wrap_fsync(int fd){return vfs_spiffs_fsync(&ctx,fd);}
int __wrap_ftruncate(int fd,off_t n){if(fixture_truncate_fail){errno=ENOSPC;return -1;}return vfs_spiffs_ftruncate(&ctx,fd,n);}
int __wrap_remove(const char*p){return vfs_spiffs_unlink(&ctx,local(p));}
int __wrap_mkdir(const char*p,mode_t mode){(void)p;(void)mode;errno=ENOTSUP;return -1;}
DIR* __wrap_opendir(const char*p){return vfs_spiffs_opendir(&ctx,local(p));}
struct dirent* __wrap_readdir(DIR*d){return vfs_spiffs_readdir(&ctx,d);}
int __wrap_closedir(DIR*d){return vfs_spiffs_closedir(&ctx,d);}
void __wrap_rewinddir(DIR*d){vfs_spiffs_seekdir(&ctx,d,0);}
long __wrap_telldir(DIR*d){return vfs_spiffs_telldir(&ctx,d);}
void __wrap_seekdir(DIR*d,long n){vfs_spiffs_seekdir(&ctx,d,n);}
