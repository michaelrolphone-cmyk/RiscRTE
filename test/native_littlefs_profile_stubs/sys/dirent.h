#pragma once
/* Newlib's DIR is a concrete VFS prefix. Do not use glibc's opaque DIR. */
#include <stdint.h>
typedef struct { uint16_t dd_vfs_idx; uint16_t dd_rsv; } DIR;
struct dirent { unsigned long d_ino; unsigned char d_type; char d_name[256]; };
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
