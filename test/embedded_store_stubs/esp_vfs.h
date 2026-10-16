#pragma once
#include <sys/types.h>
#include <sys/stat.h>
using esp_err_t=int;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_VFS_FLAG_DEFAULT 0
struct esp_vfs_t {int flags;int(*open)(const char*,int,int);ssize_t(*read)(int,void*,size_t);off_t(*lseek)(int,off_t,int);int(*close)(int);int(*fstat)(int,struct stat*);int(*stat)(const char*,struct stat*);};
int esp_vfs_register(const char*,const esp_vfs_t*,void*);
