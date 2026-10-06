/* Offline empty-image generator. Compile only with the exact reviewed LittleFS
 * source pin in app_data_image.py. No device/VFS/migration functionality. */
#include "lfs.h"
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define BYTES 524288u
static uint8_t image[BYTES];
static int read_block(const struct lfs_config*c,lfs_block_t b,lfs_off_t o,void*p,lfs_size_t n){(void)c;if(b>=128 || o+n>4096)return LFS_ERR_IO;memcpy(p,image+b*4096+o,n);return 0;}
static int write_block(const struct lfs_config*c,lfs_block_t b,lfs_off_t o,const void*p,lfs_size_t n){(void)c;if(b>=128 || o+n>4096)return LFS_ERR_IO;const uint8_t*bytes=p;for(lfs_size_t i=0;i<n;++i){if((image[b*4096+o+i]&bytes[i])!=bytes[i])return LFS_ERR_IO;image[b*4096+o+i]&=bytes[i];}return 0;}
static int erase_block(const struct lfs_config*c,lfs_block_t b){(void)c;if(b>=128)return LFS_ERR_IO;memset(image+b*4096,0xff,4096);return 0;}
static int sync_blocks(const struct lfs_config*c){(void)c;return 0;}
int main(int argc,char**argv){
 if(argc!=2)return 2;
 memset(image,0xff,sizeof(image));struct lfs_config cfg={0};
 cfg.read=read_block;cfg.prog=write_block;cfg.erase=erase_block;cfg.sync=sync_blocks;
 cfg.read_size=128;cfg.prog_size=128;cfg.block_size=4096;cfg.block_count=128;
 cfg.cache_size=512;cfg.lookahead_size=128;cfg.block_cycles=512;
 lfs_t fs={0};if(lfs_format(&fs,&cfg)!=0 || lfs_mount(&fs,&cfg)!=0)return 3;
 struct lfs_fsinfo info;if(lfs_fs_stat(&fs,&info)!=0 || info.disk_version!=0x00020001 || info.block_size!=4096 || info.block_count!=128)return 4;
 lfs_dir_t dir;if(lfs_dir_open(&fs,&dir,"/")!=0)return 5;
 struct lfs_info entry;unsigned count=0;int next;
 while((next=lfs_dir_read(&fs,&dir,&entry))>0){if(strcmp(entry.name,".") && strcmp(entry.name,".."))return 6;++count;}
 if(next<0 || count!=2 || lfs_dir_close(&fs,&dir)!=0 || lfs_unmount(&fs)!=0)return 7;
 int fd=open(argv[1],O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return 8;
 ssize_t result=write(fd,image,sizeof(image));int closed=close(fd);if(result!=sizeof(image) || closed!=0)return 9;
 puts("Created and remounted empty LittleFS disk2.1 image; no device operation.");return 0;
}
