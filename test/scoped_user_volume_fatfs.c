/* Execute the unchanged production Reader volume/FatFs against a RAM disk.
 * This fixture does not use transport, GPIO, a mount on the host, or a device.
 * FAT32 RAM layout follows Reader test/storage_volume/runtime_test.cpp at
 * 3f599139; MIT, Copyright (c) 2025 Dave Allie (see repository LICENSE). */
#include <RiscStorageVolumeV1.h>
#include <RiscPlatformClockV1.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static unsigned char *image;
static const uint32_t sectors=131072;
static bool started=true,card_ready=true,mounted=false,io_failed=false;
static char error[128];
static uint32_t writes,fail_write;
static uint64_t now(void*c){(void)c;return 0;}
static void sleep_ms(void*c,uint32_t n){(void)c;(void)n;}
static const risc_platform_clock_api_v1 clock_value={1,sizeof(clock_value),0,now,sleep_ms};
static const risc_platform_clock_api_v1 *clock_api=&clock_value;
static void cooperate(uint32_t n){(void)n;}
static bool equal(const char*a,const char*b){return strcmp(a,b)==0;}
static bool fail(const char*s){snprintf(error,sizeof(error),"%s",s);return false;}
static bool read_sector(uint32_t s,unsigned char*b){if(s>=sectors)return false;memcpy(b,image+(size_t)s*512,512);return true;}
static bool write_sector(uint32_t s,const unsigned char*b){if(s>=sectors || (++writes==fail_write))return false;memcpy(image+(size_t)s*512,b,512);return true;}
static bool sync_card(void){return true;}
static bool transport_idle(void){return true;}
static bool mount_filesystem(void);
static bool init_card(void){card_ready=true;return mount_filesystem();}
#define STORAGE_VOLUME_LABEL "Test RAM volume"
#include SCOPED_FATFS_VOLUME_SOURCE
static void put16(unsigned char*p,uint16_t n){p[0]=n;p[1]=n>>8;}
static void put32(unsigned char*p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(n>>(8*i));}
const risc_storage_volume_api_v1 *scoped_fatfs_create(void){
 image=calloc(sectors,512);assert(image);
 unsigned char*b=image;b[0]=0xeb;b[1]=0x58;b[2]=0x90;memcpy(b+3,"MSDOS5.0",8);
 put16(b+11,512);b[13]=1;put16(b+14,32);b[16]=2;b[21]=0xf8;
 put32(b+32,sectors);put32(b+36,1024);put32(b+44,2);put16(b+48,1);
 b[66]=0x29;memcpy(b+82,"FAT32   ",8);b[510]=0x55;b[511]=0xaa;
 for(unsigned f=0;f<2;++f){unsigned char*p=image+(size_t)(32+f*1024)*512;put32(p,0xffffff8);put32(p+4,0xffffffff);put32(p+8,0xfffffff);}
 assert(enter());assert(mount_filesystem());assert(leave());
 return &api.volume.base;
}
bool scoped_fatfs_safe(void*c){(void)c;return !io_failed;}
void scoped_fatfs_fail_write(uint32_t after){fail_write=after?writes+after:0;}
uint32_t scoped_fatfs_writes(void){return writes;}
void scoped_fatfs_destroy(void){assert(!has_handles());free(image);image=0;}
